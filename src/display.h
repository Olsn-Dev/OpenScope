#pragma once
#include "config.h"
#include "clubs.h"

// ─── Unit conversion helpers ──────────────────────────────────────────────────

float       disp_speed(float kmh,   bool use_mph);
float       disp_dist(float m,      bool use_mph);
const char* speed_unit(bool use_mph);
const char* dist_unit(bool use_mph);

// ─── Touch ────────────────────────────────────────────────────────────────────

// Apply a stored 5-value XPT2046 calibration. Call after display_init().
void display_set_touch_cal(const uint16_t* cal);

// Run the interactive 4-corner touch calibration; fills cal[5]. Blocks until done.
void display_touch_calibrate(uint16_t cal[5]);

// Return true on a fresh screen tap (rising edge), writing screen coords to x,y.
// Debounced; returns false while held or released.
bool ui_get_tap(int* x, int* y);

// Per-screen hit-testing — map a tap (x,y) to a logical action.
int ui_splash_hit(int x, int y);    // 1 = next club, 2 = open settings, 0 = none
int ui_result_hit(int x, int y);    // 1 = dismiss, 0 = none (any tap dismisses)
int ui_settings_hit(int x, int y);  // 0..3 = item rows, 9 = DONE/exit, -1 = none
int ui_cal_hit(int x, int y);       // 1 = -10, 2 = save+exit, 3 = +10, 0 = none

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

// Settings menu — touch rows, no highlighted selection.
// reset_done = true shows a brief "Done!" confirmation on the reset item.
void ui_settings_draw(int club_idx, bool use_mph, bool reset_done = false);

// Calibration screen header (call once when entering calibration).
void ui_cal_header();

// Calibration live update — redraws spectrum area and metrics rows.
//   spectrum — pointer to FFT_SIZE magnitude values (vReal from radar module)
void ui_cal_update(const double* spectrum,
                   double peak_hz, double peak_mag,
                   float  noise_ema, float max_seen,
                   float  threshold, bool use_mph);
