#include <Arduino.h>
#include <arduinoFFT.h>
#include <math.h>
#include "radar.h"
#include "config.h"

// ─── FFT buffers ──────────────────────────────────────────────────────────────

double vReal[FFT_SIZE];
double vImag[FFT_SIZE];

static ArduinoFFT<double> FFT(vReal, vImag, FFT_SIZE, (double)SAMPLE_RATE);

// ─── Sampling ─────────────────────────────────────────────────────────────────

void sample_radar()
{
    // Uniform sampling at SAMPLE_RATE (40 kHz). A 70 m/s ball is ~11.2 kHz at
    // 24 GHz, so this comfortably clears Nyquist for the full speed range.
    const uint32_t period_us = 1000000UL / SAMPLE_RATE;
    for (int i = 0; i < FFT_SIZE; i++) {
        uint32_t t0 = micros();
        vReal[i] = (double)(analogRead(RADAR_ADC_PIN) - 2048);  // centre on 0
        vImag[i] = 0.0;
        while ((micros() - t0) < period_us) {}
    }
}

// ─── FFT ──────────────────────────────────────────────────────────────────────

void run_fft()
{
    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();
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

// Extract ball_hz and club_hz from the FFT magnitude buffer.
//
// Within one ~25 ms FFT window the spectrum may hold two peaks: the clubhead
// (slower, pre-impact) and the ball (faster, post-impact). We take the two
// strongest peaks at least MIN_PEAK_SEP_HZ apart; if their ratio looks like a
// plausible ball/club pair (≥ SMASH_MIN_RATIO), the lower one is the club and
// the higher one is the ball. Otherwise we report ball speed only.
//
// Returns false if no peak above threshold.
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

// ─── Full detection ───────────────────────────────────────────────────────────

bool detect_speeds(double& ball_hz, double& club_hz, float threshold)
{
    run_fft();

    if (!get_peaks_from_buf(vReal, ball_hz, club_hz, threshold)) return false;

    Serial.printf("[RADAR] ball=%.0f Hz  club=%.0f Hz\n", ball_hz, club_hz);
    return true;
}
