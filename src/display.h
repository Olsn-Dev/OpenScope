#pragma once
#include "config.h"
#include "clubs.h"
#include "storage.h"   // ShotRecord for the history screen

// ─── Unit conversion helpers ──────────────────────────────────────────────────

float       disp_speed(float kmh,   bool use_mph);
float       disp_dist(float m,      bool use_mph);
const char* speed_unit(bool use_mph);
const char* dist_unit(bool use_mph);

// ─── Theme ──────────────────────────────────────────────────────────────────

// Select the LM1 colour theme: false = Black (white labels), true = Blue (cyan
// labels). Affects subsequently drawn screens; call before redrawing.
void ui_set_theme(bool blue_theme);

// ─── Touch ────────────────────────────────────────────────────────────────────

// Apply a stored 5-value XPT2046 calibration. Call after display_init().
void display_set_touch_cal(const uint16_t* cal);

// Run the interactive 4-corner touch calibration; fills cal[5]. Blocks until done.
void display_touch_calibrate(uint16_t cal[5]);

// Return true on a fresh screen tap (rising edge), writing screen coords to x,y.
// Debounced; returns false while held or released.
bool ui_get_tap(int* x, int* y);

// Recognise a tap or directional swipe on release. On a tap (x,y) is the press
// point; on a swipe (x,y) is the gesture's *start* point. Returns GES_NONE
// while a press is still in progress. Use this instead of ui_get_tap on screens
// that need swipe navigation. (Don't mix both on the same screen in one loop.)
UiGesture ui_get_gesture(int* x, int* y);

// Per-screen hit-testing — map a tap (x,y) to a logical action.
// Session ready (both layouts): 1 = club pill, 2 = Menu/settings, 3 = Back,
//                               4 = metric cell (Advanced), 0 = none.
int ui_splash_hit(int x, int y);
// Which UiMetric an Advanced grid cell maps to (for tap-to-LargeDigit). -1 = pill.
int ui_advanced_metric_at(int x, int y);
// Large-Digit session screen: 1 = club pill, 2 = Menu, 3 = Back, 0 = none.
int ui_large_hit(int x, int y);
int ui_result_hit(int x, int y);    // 1 = dismiss, 0 = none (any tap dismisses)
int ui_settings_hit(int x, int y);  // 0..5 = item rows, 9 = Back/exit, -1 = none
int ui_cal_hit(int x, int y);       // 1 = -10, 2 = save+exit, 3 = +10, 0 = none
int ui_menu_hit(int x, int y);      // 0 Start, 1 History, 2 Settings, 3 Shut Down
int ui_mode_hit(int x, int y);      // 0 Practice, 1 OnCourse, 2 Speed, 3 Back
int ui_picker_hit(int x, int y, int scroll);  // club idx, 99 = Back, -1 = none
int ui_history_hit(int x, int y);   // 99 = Back, 98 = Clear, -1 = none

// ─── Screens ──────────────────────────────────────────────────────────────────

// Initialise the TFT hardware. Call once in setup().
void display_init();

// Boot splash — OpenScope wordmark, shown briefly while starting up.
void display_splash();

// Brief "Goodbye" message shown before deep sleep.
void display_goodbye();

// ── Touch press feedback ──
// Briefly highlight the tapped control before acting on it, so every tap has
// a visible response (resistive panels give no tactile confirmation).
void ui_menu_flash(int row);                       // main-menu rows
void ui_mode_flash(int row);                       // mode-select rows
void ui_settings_flash(int row);                   // settings item rows
void ui_pill_flash(bool large_layout, int club_idx); // club pill (either layout)

// ── Advanced layout ──
// Ready screen — top row dimmed, bottom row shows per-club stats, club pill +
// bottom nav strip (Back / hint / Menu).
void ui_splash(int club_idx, const ClubStats* stats, bool use_mph);

// Result screen — all tiles filled with shot data.
// Shows the five single-Doppler metrics: club speed, ball speed, smash factor,
// carry and total distance.
//   smash <= 0  →  club not detected; club + smash tiles show "--" (dimmed)
void ui_result(float ball_kmh, float club_kmh, float smash,
               float carry_m,  float total_m,
               int club_idx, bool use_mph);

// ── Large Digit layout ──
// One focused metric, huge, with the club pill alongside.
void ui_large_ready(UiMetric metric, int club_idx, bool use_mph);
void ui_large_result(UiMetric metric,
                     float ball_kmh, float club_kmh, float smash,
                     float carry_m, float total_m,
                     int club_idx, bool use_mph);

// ── Speed Training ──  single huge swing-speed number (have=false → "--").
void ui_speed(float swing_kmh, bool use_mph, bool have);

// ── Main menu / mode select / club picker ──
void ui_menu_draw();
void ui_mode_draw();
void ui_picker_draw(int club_idx, int scroll);   // scroll = first visible club

// Shot history — newest-first list of the persisted shot log. `scroll` is the
// newest-first index of the first visible row; the list pages by swipe ↕.
void ui_history_draw(const ShotRecord* shots, int count, int scroll,
                     bool use_mph);

// Settings menu — six tappable rows (Units, Color, Layout, Reset, Radar Cal,
// Touch Cal). reset_done = true briefly shows "Done!" on the Reset row.
void ui_settings_draw(int club_idx, bool use_mph, bool blue_theme,
                      UiLayout layout, bool reset_done = false);

// Calibration screen header (call once when entering calibration).
void ui_cal_header();

// Calibration live update — redraws spectrum area and metrics rows.
//   spectrum — pointer to FFT_SIZE magnitude values (vReal from radar module)
void ui_cal_update(const double* spectrum,
                   double peak_hz, double peak_mag,
                   float  noise_ema, float max_seen,
                   float  threshold, bool use_mph);
