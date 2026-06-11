#pragma once

// ─── Pins ─────────────────────────────────────────────────────────────────────

// Single-radar layout:
//   One CDM324 Doppler module on the ground, ~1.4 m behind the ball, in line
//   with the hitting direction and facing the target (same geometry as the
//   Shot Scope LM1). The ball recedes from the unit; Doppler measures receding
//   speed identically to approaching speed.
#define RADAR_ADC_PIN    34   // CDM324 IF → LM358 preamp → ADC1_CH6
#define BTN_POWER        27   // Power (RTC GPIO17 — supports ext0 wake)

// All other navigation is via the touch panel (XPT2046, 4-wire resistive).
// The touch controller shares the display SPI bus; its pins (TOUCH_CS=21,
// MISO=19) are configured in platformio.ini build flags, not here.

// ─── Sampling & FFT ───────────────────────────────────────────────────────────

#define SAMPLE_RATE  40000   // Hz — Nyquist covers 20 kHz (~447 km/h)
#define FFT_SIZE     1024    // bins → 39 Hz/bin ≈ 0.87 km/h resolution

// ─── Doppler physics (CDM324, f_c = 24.125 GHz) ───────────────────────────────

#define HZ_TO_KMH  0.022384f   // km/h per Doppler Hz
#define HZ_TO_MPH  0.013912f

// ─── Detection ────────────────────────────────────────────────────────────────

#define MIN_DETECT_HZ           1800    // ~40 km/h
#define MAX_DETECT_HZ          16000    // ~358 km/h
#define PEAK_THRESHOLD_DEFAULT  80.0f
#define SMASH_MIN_RATIO         1.15f   // ball/club Hz ratio floor for a valid pair
#define MIN_PEAK_SEP_HZ          800    // minimum Hz gap between two peaks

// ─── Display ──────────────────────────────────────────────────────────────────

#define TFT_ROTATION  1         // landscape, USB connector on right
#define SCR_W  480
#define SCR_H      320
#define COL_W      (SCR_W / 3)
#define ROW_H      130   // main tile row height (2×130 + 60 = 320)
#define MINI_ROW_H  60   // mini row at bottom (tap-to-continue hint)

// RGB565 colour palette
#define COL_DIV      0x2945   // dark teal — grid divider lines
#define COL_UNIT     0x7BCF   // mid-grey  — unit labels
#define COL_DIM      0x2104   // near-black — inactive / dimmed tiles
#define COL_CAL_HDR  0x5920   // dark green — calibration header bar
#define COL_SEL_BG   0x1082   // very dark blue — settings selection bg
#define COL_BTN_BG   0x18E3   // dark slate — touch button fill
#define COL_BTN_BRD  0x4208   // grey       — touch button border
#define COL_TILE_BG  0x0841   // very dark grey — card tile background
#define BTN_RADIUS   8        // rounded corner radius for buttons/rows

// ─── Themes (LM1-style) ───────────────────────────────────────────────────────
// Two selectable themes share the same layout; only the metric-label colour
// changes. Background is always black; numbers are white; units grey.
//   Black theme  → labels white
//   Blue theme   → labels cyan
#define COL_LABEL_BLACK  0xFFFF   // white — label colour in the black theme
#define COL_LABEL_BLUE   0x35DE   // ~#36B6F0 cyan — label colour in the blue theme

// ─── UI enums ─────────────────────────────────────────────────────────────────
// On-device display layout for a session (swipe left/right to switch).
enum UiLayout { LAYOUT_ADVANCED = 0, LAYOUT_LARGE_DIGIT = 1 };

// The five LM1 metrics, in the order the Large-Digit screen cycles through them.
enum UiMetric {
    MET_CLUB = 0, MET_BALL, MET_SMASH, MET_CARRY, MET_TOTAL, MET_COUNT
};

// Session mode chosen from the mode-select screen. On Course currently behaves
// like Practice (both feed the shared shot-history log); Speed only shows
// swing speed and is not logged (no club context).
enum SessionMode { MODE_PRACTICE = 0, MODE_ONCOURSE, MODE_SPEED };

// Touch gestures recognised by ui_get_gesture().
enum UiGesture {
    GES_NONE = 0, GES_TAP, GES_SWIPE_L, GES_SWIPE_R, GES_SWIPE_U, GES_SWIPE_D
};

// ─── Gesture tuning ───────────────────────────────────────────────────────────
// Movement (px) under this on release counts as a tap, not a swipe — debounces
// the resistive digitizer so a jittery press doesn't register as a swipe.
#define TAP_MOVE_MAX   24
// Minimum travel (px) on the dominant axis to count as a swipe.
#define SWIPE_MIN      55
// A swipe whose start x is within this margin of the left edge is an
// edge-swipe — used as a "back" gesture on sub-screens.
#define EDGE_BACK_X    40

// ─── Touch layout ─────────────────────────────────────────────────────────────
// Geometry shared between the draw code (display.cpp) and hit-testing.
// A "bar" is a full-width action strip at the bottom of a screen.

#define BAR_H        60                  // bottom action-bar height
#define BAR_Y        (SCR_H - BAR_H)     // 260 — top edge of bottom bars

// Club selector pill (LM1-style rounded outline) — lives in the bottom-right
// cell of the Advanced grid (col 2, row 1).
#define PILL_W       104
#define PILL_H        62
#define PILL_X       (COL_W * 2 + (COL_W - PILL_W) / 2)   // centred in col 2
#define PILL_Y       (ROW_H + (ROW_H - PILL_H) / 2)       // centred in row 1

// In the Large-Digit layout the pill sits on the right, beside the big number.
#define LPILL_X      (SCR_W - PILL_W - 16)
#define LPILL_Y      100

// Back chevron hit area — top-left corner of every sub-screen.
#define BACK_W        56
#define BACK_H        48

// Gear (settings) hit area — top-right corner of the session screens.
#define GEAR_W        56
#define GEAR_H        48
#define GEAR_X       (SCR_W - GEAR_W)

// Full-width list rows used by the main menu and mode-select screens.
// 56 + 4×64 = 312 ≤ 320 — four rows fit since Shot History joined the menu.
#define MENU_HDR_H    56                 // title bar height
#define MENU_ROW_H    64                 // big finger-friendly rows
#define MENU_ROW_GAP   8

// Club picker — a vertical scrollable list.
#define PICK_HDR_H    48
#define PICK_ROW_H    52                 // ≥44 px tap target
#define PICK_ROWS     ((SCR_H - PICK_HDR_H) / PICK_ROW_H)   // visible rows

// Settings screen — 6 rows fit under a header with no separate DONE bar
// (exit via the header back chevron or a swipe). 48 + 6×45 = 318 ≤ 320.
//   0 Units   1 Color   2 Layout   3 Reset Stats   4 Radar Cal.   5 Touch Cal.
#define SET_HDR_H    48                  // header height (holds back chevron)
#define SET_ROW_H    45                  // height of each tappable item row
#define SET_N_ROWS    6

// Shot history — header bar, a column-label strip, then list rows.
// Rows are display-only (not tappable), so they can be tighter than 44 px.
#define HIST_HDR_H   48                  // header (Back / title / Clear)
#define HIST_COL_H   24                  // column-label strip below the header
#define HIST_ROW_H   34
#define HIST_ROWS    ((SCR_H - HIST_HDR_H - HIST_COL_H) / HIST_ROW_H)  // 7

// Calibration bottom bar holds three buttons: [-10] [SAVE] [+10],
// each one COL_W wide at y = BAR_Y.
