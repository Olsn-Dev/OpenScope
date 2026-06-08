#pragma once
#include "config.h"

// ─── Public API ───────────────────────────────────────────────────────────────

// Sample both radar channels simultaneously into the FFT input buffers.
// Must be called before run_fft() or detect_speeds().
void sample_radar();

// Run Hamming-windowed FFT on both channels.
// Called internally by detect_speeds(); also exposed for calibration mode.
void run_fft();

// Analyse both FFT magnitude spectra and return:
//   ball_hz   — Doppler frequency [Hz] of the ball (from Radar A)
//   club_hz   — Doppler frequency [Hz] of the club, or 0 if not found
//   launch_deg — computed launch angle [°], or -1 if unavailable
//   threshold  — minimum magnitude to count as a real peak
// Returns false if no hit was detected above threshold.
bool detect_speeds(double& ball_hz, double& club_hz,
                   float& launch_deg, float threshold);

// Primary FFT magnitude buffer (Radar A, horizontal).
// Exposed so that calibration mode can read bin magnitudes for the
// live spectrum display without coupling display code to radar internals.
extern double vReal[FFT_SIZE];
