#include <Arduino.h>
#include <arduinoFFT.h>
#include <math.h>
#include "radar.h"
#include "config.h"

// ─── FFT buffers ──────────────────────────────────────────────────────────────

double vRealL[FFT_SIZE];  // Radar L — left V arm
double vImagL[FFT_SIZE];
double vRealR[FFT_SIZE];  // Radar R — right V arm
double vImagR[FFT_SIZE];
double vRealT[FFT_SIZE];  // Radar T — top (launch angle)
double vImagT[FFT_SIZE];

static ArduinoFFT<double> FFT_L(vRealL, vImagL, FFT_SIZE, (double)SAMPLE_RATE);
static ArduinoFFT<double> FFT_R(vRealR, vImagR, FFT_SIZE, (double)SAMPLE_RATE);
static ArduinoFFT<double> FFT_T(vRealT, vImagT, FFT_SIZE, (double)SAMPLE_RATE);

// ─── Sampling ─────────────────────────────────────────────────────────────────

void sample_radar()
{
    // All three channels sampled within the same 25 µs period.
    const uint32_t period_us = 1000000UL / SAMPLE_RATE;
    for (int i = 0; i < FFT_SIZE; i++) {
        uint32_t t0 = micros();
        vRealL[i] = (double)(analogRead(RADAR_ADC_PIN_L) - 2048);
        vRealR[i] = (double)(analogRead(RADAR_ADC_PIN_R) - 2048);
        vRealT[i] = (double)(analogRead(RADAR_ADC_PIN_T) - 2048);
        vImagL[i] = vImagR[i] = vImagT[i] = 0.0;
        while ((micros() - t0) < period_us) {}
    }
}

// ─── FFT ──────────────────────────────────────────────────────────────────────

void run_fft()
{
    FFT_L.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT_L.compute(FFTDirection::Forward);
    FFT_L.complexToMagnitude();

    FFT_R.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT_R.compute(FFTDirection::Forward);
    FFT_R.complexToMagnitude();

    FFT_T.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT_T.compute(FFTDirection::Forward);
    FFT_T.complexToMagnitude();
}

// ─── Peak detection ───────────────────────────────────────────────────────────

struct Peak { int bin; double mag; double hz; };

// Find the highest-magnitude bin in [bin_lo, bin_hi] excluding a region
// around excl_bin (±excl_r bins). Returns an invalid Peak if below threshold.
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
        Peak lo = (p1.hz < p2.hz) ? p1 : p2;
        Peak hi = (p1.hz < p2.hz) ? p2 : p1;
        if (hi.hz >= lo.hz * SMASH_MIN_RATIO) {
            club_hz = lo.hz;
            ball_hz = hi.hz;
            return true;
        }
    }
    ball_hz = p1.hz;
    club_hz = 0.0;
    return true;
}

// ─── 3-radar geometry solvers ─────────────────────────────────────────────────
//
// Coordinate system (top view, horizontal plane):
//   x = target direction (forward)
//   y = left  (positive)
//   z = up
//
// Ball velocity: v at (launch angle α, side angle β), where β > 0 = right.
//   v_x = v·cos(α)·cos(β)
//   v_y = −v·cos(α)·sin(β)   [right = negative y]
//   v_z = v·sin(α)
//
// Radar L boresight (V left arm, faces right-forward):
//   direction = (cos V,  sin V, 0)  →  f_L = k·cos(α)·cos(β − V)
//
// Radar R boresight (V right arm, faces left-forward):
//   direction = (cos V, −sin V, 0)  →  f_R = k·cos(α)·cos(β + V)
//
// Radar T boresight (top, tilted θ_T above horizontal):
//   direction = (cos θ_T, 0, sin θ_T)
//   f_T ≈ k·(cos(α)·cos(β)·cos θ_T + sin(α)·sin θ_T)
//   (exact for β = 0; error < 1.5% for |β| ≤ 10°)
//
// Step 1 — Side angle β from L/R ratio (independent of α):
//   f_L/f_R = cos(β−V)/cos(β+V)  — monotonically increasing in β
//
// Step 2 — Horizontal-forward proxy F = k·cos(α):
//   F = (f_L + f_R) / (2·cos(β)·cos(V))
//
// Step 3 — Launch angle α from F and f_T:
//   r = f_T / F
//   tan(α) = (r − cos(β)·cos(θ_T)) / sin(θ_T)
//   α = atan(tan_α)
//
// Step 4 — True ball speed:
//   k = F / cos(α)   [= 2v/λ]
//   v = k · HZ_TO_KMH

// Returns side angle β in degrees (positive = right), or 0 if undetermined.
static float compute_side_angle(double hz_l, double hz_r)
{
    if (hz_l <= 0.0 || hz_r <= 0.0) return 0.0f;

    const float V      = RADAR_V_HALF_DEG * DEG_TO_RAD;
    const float beta_max = SIDE_MAX_DEG * DEG_TO_RAD;
    const float ratio  = (float)(hz_l / hz_r);

    // Boundary ratios at ±SIDE_MAX_DEG for range guard
    const float ratio_lo = cosf(-beta_max - V) / cosf(-beta_max + V);
    const float ratio_hi = cosf( beta_max - V) / cosf( beta_max + V);
    if (ratio < ratio_lo || ratio > ratio_hi) return 0.0f;

    // Binary search: f(β) = cos(β−V)/cos(β+V), strictly increasing in β
    float lo = -beta_max, hi = beta_max;
    for (int i = 0; i < 40; i++) {
        float mid = (lo + hi) * 0.5f;
        if (cosf(mid - V) / cosf(mid + V) < ratio) lo = mid; else hi = mid;
    }
    return (lo + hi) * 0.5f / DEG_TO_RAD;
}

// Returns launch angle α in degrees, or -1 if undetermined.
// Also sets out_k to the speed proxy k (Hz); caller converts to km/h.
static float compute_launch_angle(double hz_l, double hz_r, double hz_t,
                                  float beta_deg, double& out_k)
{
    const float V     = RADAR_V_HALF_DEG * DEG_TO_RAD;
    const float theta = RADAR_T_ANGLE_DEG * DEG_TO_RAD;
    const float beta  = beta_deg * DEG_TO_RAD;

    // F = k·cos(α)
    float denom = 2.0f * cosf(beta) * cosf(V);
    if (denom <= 0.0f) return -1.0f;
    float F = (float)((hz_l + hz_r) / denom);
    if (F <= 0.0f) return -1.0f;

    if (hz_t <= 0.0) {
        // No top radar reading — cannot solve for α, but k·cos(α) = F
        // Return speed proxy using only ground radars (launch angle unknown)
        out_k = (double)F;   // lower bound on k; slight under-estimate
        return -1.0f;
    }

    float r = (float)(hz_t / F);
    float tan_alpha = (r - cosf(beta) * cosf(theta)) / sinf(theta);
    float alpha_deg = atanf(tan_alpha) / DEG_TO_RAD;

    if (alpha_deg < LAUNCH_MIN_DEG || alpha_deg > LAUNCH_MAX_DEG) {
        out_k = (double)F;
        return -1.0f;
    }

    // k = F / cos(α) — true speed proxy
    float alpha_rad = alpha_deg * DEG_TO_RAD;
    float cos_a = cosf(alpha_rad);
    out_k = (cos_a > 0.01f) ? (double)(F / cos_a) : (double)F;
    return alpha_deg;
}

// ─── Full detection ───────────────────────────────────────────────────────────

bool detect_speeds(double& ball_hz, double& club_hz,
                   float& launch_deg, float& side_deg,
                   float threshold)
{
    run_fft();

    double ball_l = 0.0, club_l = 0.0;
    double ball_r = 0.0, club_r = 0.0;
    double ball_t = 0.0, club_t_dummy = 0.0;

    bool hit_l = get_peaks_from_buf(vRealL, ball_l, club_l, threshold);
    bool hit_r = get_peaks_from_buf(vRealR, ball_r, club_r, threshold);
    bool hit_t = get_peaks_from_buf(vRealT, ball_t, club_t_dummy, threshold);

    if (!hit_l && !hit_r) return false;

    launch_deg = -1.0f;
    side_deg   = 0.0f;

    if (hit_l && hit_r) {
        // ── Full 3-radar solution ─────────────────────────────────────────────
        side_deg = compute_side_angle(ball_l, ball_r);

        double k_ball = 0.0;
        float  alpha  = compute_launch_angle(ball_l, ball_r,
                                              hit_t ? ball_t : 0.0,
                                              side_deg, k_ball);
        ball_hz  = k_ball;
        club_hz  = 0.0;

        if (alpha >= LAUNCH_MIN_DEG && alpha <= LAUNCH_MAX_DEG) {
            launch_deg = alpha;
            Serial.printf("[RADAR] L=%.0f R=%.0f T=%.0f  β=%.1f°  α=%.1f°\n",
                          ball_l, ball_r, ball_t, side_deg, alpha);
        } else {
            Serial.printf("[RADAR] L=%.0f R=%.0f  β=%.1f°  α=N/A\n",
                          ball_l, ball_r, side_deg);
        }

        // Club speed: use average of L and R corrected by V angle
        if (club_l > 0.0 && club_r > 0.0) {
            float denom = 2.0f * cosf(side_deg * DEG_TO_RAD) * cosf(RADAR_V_HALF_DEG * DEG_TO_RAD);
            if (denom > 0.01f) club_hz = (club_l + club_r) / denom;
        } else if (club_l > 0.0) {
            club_hz = club_l / cosf((side_deg - RADAR_V_HALF_DEG) * DEG_TO_RAD);
        } else if (club_r > 0.0) {
            club_hz = club_r / cosf((side_deg + RADAR_V_HALF_DEG) * DEG_TO_RAD);
        }

    } else {
        // ── Single V-radar fallback (no side/launch angle) ────────────────────
        ball_hz = hit_l ? ball_l : ball_r;
        club_hz = hit_l ? club_l : club_r;
        // Correct for the V arm angle so speed isn't under-reported
        float angle_corr = cosf(RADAR_V_HALF_DEG * DEG_TO_RAD);
        if (angle_corr > 0.01f) {
            ball_hz /= angle_corr;
            if (club_hz > 0.0) club_hz /= angle_corr;
        }
        Serial.println("[RADAR] Single V-radar — no side/launch angle");
    }

    return true;
}
