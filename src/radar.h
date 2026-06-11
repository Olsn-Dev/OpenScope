#pragma once
#include "config.h"

// ─── Public API ───────────────────────────────────────────────────────────────
//
// Single-Doppler signal chain: ADC sampling → windowed FFT → peak detection.
// One 24 GHz CDM324 module gives us ball speed (directly) and club speed (from
// the pre-impact clubhead signature). It CANNOT give launch angle, spin, or
// side/dispersion — those need extra sensors and are not computed here.

// Sample the radar channel into the FFT input buffer.
// Must be called before run_fft() or detect_speeds().
void sample_radar();

// Run the Hamming-windowed FFT and convert to a magnitude spectrum.
// Called internally by detect_speeds(); also exposed for calibration mode.
void run_fft();

// Analyse the FFT magnitude spectrum and return:
//   ball_hz   — Doppler frequency [Hz] of the ball (post-impact peak)
//   club_hz   — Doppler frequency [Hz] of the clubhead (lower pre-impact peak),
//               or 0 if a distinct club peak was not found
//   threshold — minimum magnitude to count as a real peak
// Returns false if no hit was detected above threshold.
//
// Both frequencies are the raw Doppler shifts; the caller converts to speed
// with HZ_TO_KMH. With a single sensor aligned to the shot direction there is
// no angle to correct for — the radar sees the true line-of-sight speed.
bool detect_speeds(double& ball_hz, double& club_hz, float threshold);

// FFT magnitude buffer. Exposed so calibration mode can display the live
// spectrum without coupling display code to radar internals.
extern double vReal[FFT_SIZE];
