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

// ─── Screens ──────────────────────────────────────────────────────────────────
// All list screens take `sel` — the index of the highlighted row (moved with
// UP/DOWN, activated with OK). The matching ui_*_select(old,new) helpers
// redraw only the two affected rows so moving the highlight doesn't flicker.

// Initialise the TFT hardware. Call once in setup().
void display_init();

// Boot splash — OpenScope wordmark, shown briefly while starting up.
void display_splash();

// Brief "Goodbye" message shown before deep sleep.
void display_goodbye();

// ── Advanced layout ──
// Ready screen — top row dimmed, bottom row shows per-club stats + club pill.
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

// ── Main menu ──  rows: 0 Start Session, 1 Shot History, 2 Settings,
//                        3 Shut Down
void ui_menu_draw(int sel);
void ui_menu_select(int old_sel, int new_sel);

// ── Mode select ──  rows: 0 Practice, 1 On Course, 2 Speed Training, 3 Back
void ui_mode_draw(int sel);
void ui_mode_select(int old_sel, int new_sel);

// ── Session menu ──  rows: 0 Resume, 1 Change Club, 2 Layout toggle,
//                          3 Settings, 4 End Session
void ui_smenu_draw(int sel, UiLayout layout);
void ui_smenu_select(int old_sel, int new_sel);

// ── Club picker ──  `scroll` = first visible club, `sel` = highlighted club.
void ui_picker_draw(int club_idx, int scroll, int sel);
void ui_picker_select(int old_sel, int new_sel, int scroll, int club_idx);

// Shot history — newest-first list of the persisted shot log. `scroll` is the
// newest-first index of the first visible row; the list pages with UP/DOWN.
void ui_history_draw(const ShotRecord* shots, int count, int scroll,
                     bool use_mph);

// Settings menu — seven rows (Units, Color, Layout, Reset Stats,
// Clear History, Radar Cal., Back). done_row ≥ 0 briefly shows "Done!" as
// that row's value (used by Reset Stats / Clear History).
void ui_settings_draw(int club_idx, bool use_mph, bool blue_theme,
                      UiLayout layout, int sel, int done_row = -1);
void ui_settings_select(int old_sel, int new_sel);

// Calibration screen header (call once when entering calibration).
void ui_cal_header();

// Calibration live update — redraws spectrum area and metrics rows.
//   spectrum — pointer to FFT_SIZE magnitude values (vReal from radar module)
void ui_cal_update(const double* spectrum,
                   double peak_hz, double peak_mag,
                   float  noise_ema, float max_seen,
                   float  threshold, bool use_mph);
