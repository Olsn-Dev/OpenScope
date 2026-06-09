#pragma once

// ─── Pins ─────────────────────────────────────────────────────────────────────

// 3-radar layout:
//   Radar L & R — ground level, V-formation (see RADAR_V_HALF_DEG below)
//   Radar T     — mounted on top, tilted upward for launch angle
#define RADAR_ADC_PIN_L  34   // Radar L — left V arm  (ADC1_CH6)
#define RADAR_ADC_PIN_R  35   // Radar R — right V arm (ADC1_CH7)
#define RADAR_ADC_PIN_T  32   // Radar T — top, tilted (ADC1_CH4)
#define BTN_SCROLL       25   // Navigate / increase
#define BTN_SELECT       26   // Confirm / decrease
#define BTN_POWER        27   // Power (RTC GPIO17 — supports ext0 wake)

// ─── Sampling & FFT ───────────────────────────────────────────────────────────

#define SAMPLE_RATE  40000   // Hz — Nyquist covers 20 kHz (~447 km/h)
#define FFT_SIZE     1024    // bins → 39 Hz/bin ≈ 0.87 km/h resolution

// ─── Doppler physics (CDM324, f_c = 24.125 GHz) ───────────────────────────────

#define HZ_TO_KMH  0.022384f   // km/h per Doppler Hz
#define HZ_TO_MPH  0.013912f

// ─── Radar geometry ───────────────────────────────────────────────────────────
// RADAR_V_HALF_DEG: angle between each V arm and the shot direction (top view).
//   The two ground radars form a V with total angle 2×RADAR_V_HALF_DEG.
//   Physical mounting: V tip points toward target; each arm spreads back at this angle.
//   Valid side-angle range: |β| < (90° − RADAR_V_HALF_DEG).
//   CDM324 note: beam half-width ≈ 40°. Stronger signal with smaller RADAR_V_HALF_DEG.
//   Default 75° matches a 150° total V; reduce to 20–30° for stronger signal.
#define RADAR_V_HALF_DEG   75.0f   // degrees — half-angle of V (= 150° total V / 2)
#define RADAR_T_ANGLE_DEG  20.0f   // top radar: degrees above horizontal
#define DEG_TO_RAD         0.017453293f

// ─── Launch and side angle limits ────────────────────────────────────────────
// SIDE_MAX_DEG must be strictly less than (90 − RADAR_V_HALF_DEG).
// For V_HALF=75°: max useful range ≈ ±14°. For V_HALF=20°: up to ±69°.
#define LAUNCH_MIN_DEG     2.0f    // below this → no launch detection
#define LAUNCH_MAX_DEG     55.0f   // above this → no launch detection
#define SIDE_MAX_DEG       14.0f   // |side angle| limit in degrees

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
