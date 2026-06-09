#pragma once
#include "config.h"

// ─── Public API ───────────────────────────────────────────────────────────────

// Sample all three radar channels simultaneously into the FFT input buffers.
// Must be called before run_fft() or detect_speeds().
void sample_radar();

// Run Hamming-windowed FFT on all three channels.
// Called internally by detect_speeds(); also exposed for calibration mode.
void run_fft();

// Analyse all three FFT magnitude spectra and return:
//   ball_hz    — effective Doppler k [Hz] proportional to true ball speed
//   club_hz    — effective k for club, or 0 if not found
//   launch_deg — computed launch angle [°], or -1 if unavailable
//   side_deg   — computed side angle [°]: positive = right, negative = left
//                0.0 if unavailable (single-radar fallback)
//   threshold  — minimum magnitude to count as a real peak
// Returns false if no hit was detected above threshold.
bool detect_speeds(double& ball_hz, double& club_hz,
                   float& launch_deg, float& side_deg,
                   float threshold);

// Primary FFT magnitude buffer (Radar L, left V arm).
// Exposed so calibration mode can display the live spectrum without coupling
// display code to radar internals.
extern double vRealL[FFT_SIZE];
