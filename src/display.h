#pragma once
#include "config.h"
#include "clubs.h"

// ─── Unit conversion helpers ──────────────────────────────────────────────────

float       disp_speed(float kmh,   bool use_mph);
float       disp_dist(float m,      bool use_mph);
const char* speed_unit(bool use_mph);
const char* dist_unit(bool use_mph);

// ─── Screens ──────────────────────────────────────────────────────────────────

// Initialise the TFT hardware. Call once in setup().
void display_init();

// Brief "Goodbye" message shown before deep sleep.
void display_goodbye();

// Ready screen — top row dimmed, bottom row shows per-club stats.
void ui_splash(int club_idx, const ClubStats* stats, bool use_mph);

// Result screen — all tiles filled with shot data.
//   launch_deg < 0  →  launch tile shows "--" (dimmed)
//   side_deg: positive = right, negative = left; shown as "R 2.3°" / "L 2.3°"
void ui_result(float ball_kmh, float club_kmh,
               float carry_m,  float total_m,
               float launch_deg, float side_deg,
               int club_idx, bool use_mph);

// Settings menu. sel = highlighted row (0–2).
// reset_done = true shows a brief "Done!" confirmation on the reset item.
void ui_settings_draw(int sel, int club_idx, bool use_mph, bool reset_done = false);

// Calibration screen header (call once when entering calibration).
void ui_cal_header();

// Calibration live update — redraws spectrum area and metrics rows.
//   spectrum — pointer to FFT_SIZE magnitude values (vReal from radar module)
void ui_cal_update(const double* spectrum,
                   double peak_hz, double peak_mag,
                   float  noise_ema, float max_seen,
                   float  threshold, bool use_mph);
