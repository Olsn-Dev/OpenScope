#include <Arduino.h>
#include <arduinoFFT.h>
#include <math.h>
#include "radar.h"
#include "config.h"

// ─── FFT buffers ──────────────────────────────────────────────────────────────

double vReal[FFT_SIZE];
double vImag[FFT_SIZE];
double vRealB[FFT_SIZE];
double vImagB[FFT_SIZE];

static ArduinoFFT<double> FFT_A(vReal,  vImag,  FFT_SIZE, (double)SAMPLE_RATE);
static ArduinoFFT<double> FFT_B(vRealB, vImagB, FFT_SIZE, (double)SAMPLE_RATE);

// ─── Sampling ─────────────────────────────────────────────────────────────────

void sample_radar()
{
    // Both channels sampled inside the same 25 µs period so vReal[i] and
    // vRealB[i] represent the same moment in time (< 5 µs offset between reads).
    const uint32_t period_us = 1000000UL / SAMPLE_RATE;
    for (int i = 0; i < FFT_SIZE; i++) {
        uint32_t t0 = micros();
        vReal[i]  = (double)(analogRead(RADAR_ADC_PIN)   - 2048);
        vRealB[i] = (double)(analogRead(RADAR_ADC_PIN_B) - 2048);
        vImag[i]  = 0.0;
        vImagB[i] = 0.0;
        while ((micros() - t0) < period_us) {}
    }
}

// ─── FFT ──────────────────────────────────────────────────────────────────────

void run_fft()
{
    FFT_A.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT_A.compute(FFTDirection::Forward);
    FFT_A.complexToMagnitude();

    FFT_B.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT_B.compute(FFTDirection::Forward);
    FFT_B.complexToMagnitude();
}

// ─── Peak detection ───────────────────────────────────────────────────────────

struct Peak { int bin; double mag; double hz; };

// Find the highest-magnitude bin in [bin_lo, bin_hi] excluding a region
// around excl_bin (±excl_r bins). Returns an invalid Peak if the best
// candidate is below threshold.
static Peak find_best_peak(const double* buf,
                           int bin_lo, int bin_hi,
                           int excl_bin, int excl_r,
                           float threshold)
{
    Peak p = { -1, 0.0, 0.0 };
    for (int i = bin_lo; i <= bin_hi; i++) {
        if (excl_bin >= 0 && abs(i - excl_bin) < excl_r) continue;
        if (buf[i] > p.mag) { p.mag = buf[i]; p.bin = i; }
    }
    if (p.bin < 0 || p.mag < (double)threshold) return { -1, 0.0, 0.0 };

    // Parabolic interpolation for sub-bin frequency accuracy.
    // delta = 0.5 * (a - c) / (a - 2b + c)
    double a = (p.bin > 0)          ? buf[p.bin - 1] : 0.0;
    double b = buf[p.bin];
    double c = (p.bin < FFT_SIZE/2) ? buf[p.bin + 1] : 0.0;
    double denom = a - 2.0*b + c;
    double offset = (denom != 0.0) ? 0.5*(a - c)/denom : 0.0;
    p.hz = ((double)p.bin + offset) * SAMPLE_RATE / FFT_SIZE;
    return p;
}

// Extract ball_hz (highest peak) and club_hz (second peak, lower frequency)
// from one FFT magnitude buffer. Returns false if no peak above threshold.
static bool get_peaks_from_buf(const double* buf,
                                double& ball_hz, double& club_hz,
                                float threshold)
{
    const int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    const int bin_hi = min((int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE),
                           FFT_SIZE/2 - 1);
    const int sep    = max(5, (int)((double)MIN_PEAK_SEP_HZ / SAMPLE_RATE * FFT_SIZE));

    Peak p1 = find_best_peak(buf, bin_lo, bin_hi, -1, 0, threshold);
    if (p1.bin < 0) return false;

    Peak p2 = find_best_peak(buf, bin_lo, bin_hi, p1.bin, sep, threshold);

    if (p2.bin >= 0) {
        // Sort by frequency: lower = club, higher = ball
        Peak lo = (p1.hz < p2.hz) ? p1 : p2;
        Peak hi = (p1.hz < p2.hz) ? p2 : p1;
        if (hi.hz >= lo.hz * SMASH_MIN_RATIO) {
            club_hz = lo.hz;
            ball_hz = hi.hz;
            return true;
        }
    }
    // Only one significant peak → ball speed only
    ball_hz = p1.hz;
    club_hz = 0.0;
    return true;
}

// ─── Launch angle solver ──────────────────────────────────────────────────────
//
// Radar A (horizontal, θ = 0°): f_A = 2·v·cos(α)·fc/c
// Radar B (tilted θ upward):    f_B = 2·v·cos(α−θ)·fc/c
//
// Ratio:  f_A / f_B = cos(α) / cos(α−θ)
//
// This ratio is monotonically decreasing in α for any fixed θ > 0,
// so a binary search converges to the unique solution.
//
// Returns the launch angle in degrees, or -1 if the radios are outside
// the physically plausible range [LAUNCH_MIN_DEG, LAUNCH_MAX_DEG].

static float compute_launch_angle(double hz_a, double hz_b)
{
    if (hz_b <= 0.0) return -1.0f;

    const float theta = RADAR_B_ANGLE_DEG * DEG_TO_RAD;
    const float ratio = (float)(hz_a / hz_b);

    // Reject if ratio is outside the range spanned by our search window
    const float ratio_at_min = cosf(LAUNCH_MIN_DEG * DEG_TO_RAD) /
                                cosf((LAUNCH_MIN_DEG - RADAR_B_ANGLE_DEG) * DEG_TO_RAD);
    const float ratio_at_max = cosf(LAUNCH_MAX_DEG * DEG_TO_RAD) /
                                cosf((LAUNCH_MAX_DEG - RADAR_B_ANGLE_DEG) * DEG_TO_RAD);
    if (ratio > ratio_at_min || ratio < ratio_at_max) return -1.0f;

    // Binary search: f(α) = cos(α)/cos(α−θ), monotonically decreasing in α
    float lo = LAUNCH_MIN_DEG * DEG_TO_RAD;
    float hi = LAUNCH_MAX_DEG * DEG_TO_RAD;
    for (int iter = 0; iter < 40; iter++) {
        float mid = (lo + hi) * 0.5f;
        float f   = cosf(mid) / cosf(mid - theta);
        if (f > ratio) lo = mid; else hi = mid;
    }
    return (lo + hi) * 0.5f / DEG_TO_RAD;
}

// ─── Full detection ───────────────────────────────────────────────────────────

bool detect_speeds(double& ball_hz, double& club_hz,
                   float& launch_deg, float threshold)
{
    run_fft();

    double ball_a = 0.0, club_a = 0.0;
    double ball_b = 0.0, club_b = 0.0;
    bool hit_a = get_peaks_from_buf(vReal,  ball_a, club_a, threshold);
    bool hit_b = get_peaks_from_buf(vRealB, ball_b, club_b, threshold);

    if (!hit_a && !hit_b) return false;

    // Primary speed always from Radar A (horizontal → true forward velocity)
    ball_hz    = hit_a ? ball_a : ball_b;
    club_hz    = hit_a ? club_a : (hit_b ? club_b : 0.0);
    launch_deg = -1.0f;

    if (hit_a && hit_b && ball_a > 0.0 && ball_b > 0.0) {
        float alpha = compute_launch_angle(ball_a, ball_b);
        if (alpha >= LAUNCH_MIN_DEG && alpha <= LAUNCH_MAX_DEG) {
            launch_deg = alpha;
            Serial.printf("[RADAR] A=%.0f Hz  B=%.0f Hz  ratio=%.4f  α=%.1f°\n",
                          ball_a, ball_b, ball_a / ball_b, alpha);
        } else {
            Serial.printf("[RADAR] Angle out of range (%.1f°) — empirical carry\n", alpha);
        }
    } else {
        Serial.println("[RADAR] Single radar — empirical carry");
    }
    return true;
}
