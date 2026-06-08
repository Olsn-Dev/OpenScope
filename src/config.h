#pragma once

// ─── Pins ─────────────────────────────────────────────────────────────────────

#define RADAR_ADC_PIN    34   // Radar A — horizontal (primary speed)
#define RADAR_ADC_PIN_B  35   // Radar B — angled upward (launch angle)
#define BTN_SCROLL       25   // Navigate / increase
#define BTN_SELECT       26   // Confirm / decrease
#define BTN_POWER        27   // Power (RTC GPIO17 — supports ext0 wake)

// ─── Sampling & FFT ───────────────────────────────────────────────────────────

#define SAMPLE_RATE  40000   // Hz — Nyquist covers 20 kHz (~447 km/h)
#define FFT_SIZE     1024    // bins → 39 Hz/bin ≈ 0.87 km/h resolution

// ─── Doppler physics (CDM324, f_c = 24.125 GHz) ───────────────────────────────

#define HZ_TO_KMH  0.022384f   // km/h per Doppler Hz
#define HZ_TO_MPH  0.013912f

// ─── Launch angle ─────────────────────────────────────────────────────────────
// RADAR_B_ANGLE_DEG must match the physical mounting angle of Radar B.

#define RADAR_B_ANGLE_DEG  20.0f   // degrees above horizontal
#define DEG_TO_RAD         0.017453293f
#define LAUNCH_MIN_DEG     2.0f    // below this → treat as no-detection
#define LAUNCH_MAX_DEG     55.0f   // above this → treat as no-detection

// ─── Detection ────────────────────────────────────────────────────────────────

#define MIN_DETECT_HZ           1800    // ~40 km/h
#define MAX_DETECT_HZ          16000    // ~358 km/h
#define PEAK_THRESHOLD_DEFAULT  80.0f
#define SMASH_MIN_RATIO         1.15f   // ball/club Hz ratio floor for a valid pair
#define MIN_PEAK_SEP_HZ          800    // minimum Hz gap between two peaks

// ─── Display ──────────────────────────────────────────────────────────────────

#define TFT_ROTATION  1         // landscape, USB connector on right
#define SCR_W  480
#define SCR_H  320
#define COL_W  (SCR_W / 3)
#define ROW_H  (SCR_H / 2)

// RGB565 colour palette
#define COL_DIV      0x2945   // dark teal — grid divider lines
#define COL_UNIT     0x7BCF   // mid-grey  — unit labels
#define COL_DIM      0x2104   // near-black — inactive / dimmed tiles
#define COL_CAL_HDR  0x5920   // dark green — calibration header bar
#define COL_SEL_BG   0x1082   // very dark blue — settings selection bg
