// Smoke test for the single-radar speed-from-FFT path.
//
// The firmware turns a Doppler tone into a speed in three steps (radar.cpp):
//   1. windowed FFT magnitude spectrum
//   2. peak bin + parabolic interpolation  →  frequency [Hz]
//   3. Hz × HZ_TO_KMH                       →  speed [km/h]
//
// This test reproduces that exact math against a synthetic signal and the real
// firmware constants from config.h, so it stays in sync with the production
// SAMPLE_RATE / FFT_SIZE / HZ_TO_KMH. It is a native test (no Arduino), run via
//   pio test -e native
//
// We deliberately do NOT link radar.cpp here (it needs the Arduino/arduinoFFT
// toolchain); instead we mirror its algorithm with a small DFT so the test is
// dependency-free and also compilable with a plain host compiler.

#include <cmath>
#include <cstdio>
#include <vector>

#include "../../src/config.h"   // SAMPLE_RATE, FFT_SIZE, HZ_TO_KMH, MIN/MAX_DETECT_HZ

static int g_failures = 0;

#define CHECK(cond, msg) do {                                   \
    if (!(cond)) { printf("FAIL: %s\n", msg); ++g_failures; }   \
    else         { printf("ok:   %s\n", msg); }                 \
} while (0)

// ─── Synthetic signal + magnitude spectrum ────────────────────────────────────

// Fill `out` with FFT_SIZE samples of a sum of cosine tones (Hz, amplitude).
static void synth(std::vector<double>& out,
                  const std::vector<std::pair<double,double>>& tones)
{
    out.assign(FFT_SIZE, 0.0);
    for (int n = 0; n < FFT_SIZE; ++n) {
        double t = (double)n / SAMPLE_RATE;
        for (auto& tone : tones)
            out[n] += tone.second * std::cos(2.0 * M_PI * tone.first * t);
    }
}

// Naive real-input DFT magnitude for bins 0..FFT_SIZE/2 (slow but exact enough
// for a one-off test). Mirrors the magnitude buffer that complexToMagnitude()
// produces in radar.cpp.
static void dft_magnitude(const std::vector<double>& x, std::vector<double>& mag)
{
    const int half = FFT_SIZE / 2;
    mag.assign(FFT_SIZE, 0.0);
    for (int k = 0; k <= half; ++k) {
        double re = 0.0, im = 0.0;
        for (int n = 0; n < FFT_SIZE; ++n) {
            double a = 2.0 * M_PI * k * n / FFT_SIZE;
            re += x[n] * std::cos(a);
            im -= x[n] * std::sin(a);
        }
        mag[k] = std::sqrt(re * re + im * im);
    }
}

// Peak bin within the detection window + parabolic interpolation → Hz.
// Same formula as find_best_peak() in radar.cpp.
static double peak_hz(const std::vector<double>& mag)
{
    const int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    const int bin_hi = (int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);

    int best = bin_lo;
    for (int i = bin_lo; i <= bin_hi && i < FFT_SIZE/2; ++i)
        if (mag[i] > mag[best]) best = i;

    double a = mag[best - 1], b = mag[best], c = mag[best + 1];
    double denom = a - 2.0 * b + c;
    double offset = (denom != 0.0) ? 0.5 * (a - c) / denom : 0.0;
    return ((double)best + offset) * SAMPLE_RATE / FFT_SIZE;
}

// ─── Tests ────────────────────────────────────────────────────────────────────

// A single Doppler tone is recovered to the right frequency and speed.
static void test_single_tone_speed()
{
    const double f_doppler = 6000.0;                  // Hz
    std::vector<double> sig, mag;
    synth(sig, { {f_doppler, 1000.0} });
    dft_magnitude(sig, mag);

    double hz = peak_hz(mag);
    double bin_hz = (double)SAMPLE_RATE / FFT_SIZE;    // ~39 Hz/bin
    CHECK(std::fabs(hz - f_doppler) < bin_hz, "recovered frequency within 1 bin");

    double speed_kmh = hz * HZ_TO_KMH;
    double expected  = f_doppler * HZ_TO_KMH;          // ~134 km/h
    CHECK(std::fabs(speed_kmh - expected) < 1.0, "recovered speed within 1 km/h");
    printf("      f=%.1f Hz  ->  %.1f km/h\n", hz, speed_kmh);
}

// With a club tone and a (stronger, faster) ball tone present, the dominant
// peak is the ball — the higher of the two frequencies.
static void test_ball_is_higher_peak()
{
    const double club = 4000.0, ball = 8000.0;
    std::vector<double> sig, mag;
    synth(sig, { {club, 600.0}, {ball, 1000.0} });
    dft_magnitude(sig, mag);

    double hz = peak_hz(mag);
    CHECK(std::fabs(hz - ball) < std::fabs(hz - club),
          "dominant peak is the ball (higher frequency) tone");
}

int main()
{
    printf("== speed-from-FFT smoke test ==\n");
    printf("   SAMPLE_RATE=%d  FFT_SIZE=%d  HZ_TO_KMH=%.6f\n",
           SAMPLE_RATE, FFT_SIZE, HZ_TO_KMH);
    test_single_tone_speed();
    test_ball_is_higher_peak();
    printf("== %s ==\n", g_failures ? "FAILURES" : "all passed");
    return g_failures ? 1 : 0;
}
