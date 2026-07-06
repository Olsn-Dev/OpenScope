#pragma once

#define FW_VERSION "v0.9"

// ─── Pins ─────────────────────────────────────────────────────────────────────

// Single-radar layout:
//   One CDM324 Doppler module on the ground, ~1.4 m behind the ball, in line
//   with the hitting direction and facing the target (same geometry as the
//   Shot Scope LM1). The ball recedes from the unit; Doppler measures receding
//   speed identically to approaching speed.
//
// Target board: LILYGO T-Energy-S3 (ESP32-S3). On the S3, ADC1 lives on
// GPIO1–10 and every GPIO 0–21 is RTC-capable. Board-reserved pins to avoid:
// IO0 (boot btn), IO3 (battery divider), IO15/16 (32 kHz crystal),
// IO19/20 (USB), IO35–37 (octal PSRAM), IO43/44 (UART0), IO45/46 (straps).
#define RADAR_ADC_PIN    1    // CDM324 IF → LM358 preamp → ADC1_CH0

// Navigation is three physical buttons, each GPIO → GND (internal pull-ups).
// OK doubles as the power button: short press = select, hold = power off,
// press = wake from deep sleep (GPIO2 is RTC-capable — ext0 wake).
#define PIN_BTN_OK       2    // select / confirm; hold = power off; wake pin
#define PIN_BTN_UP       4
#define PIN_BTN_DOWN     5

// Button timing (see buttons.cpp).
#define BTN_DEBOUNCE_MS       25
#define BTN_LONG_MS         1500   // OK hold → power off
#define BTN_REPEAT_DELAY_MS  450   // UP/DOWN auto-repeat after this hold…
#define BTN_REPEAT_MS        140   // …then one event per this interval

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
#define COL_BTN_BG   0x18E3   // dark slate — list-row / button-bar fill
#define COL_BTN_BRD  0x4208   // grey       — button-bar border
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
// On-device display layout for a session (toggled in the session menu or
// Settings).
enum UiLayout { LAYOUT_ADVANCED = 0, LAYOUT_LARGE_DIGIT = 1 };

// The five LM1 metrics, in the order the Large-Digit screen cycles through them.
enum UiMetric {
    MET_CLUB = 0, MET_BALL, MET_SMASH, MET_CARRY, MET_TOTAL, MET_COUNT
};

// Session mode chosen from the mode-select screen. On Course currently behaves
// like Practice (both feed the shared shot-history log); Speed only shows
// swing speed and is not logged (no club context).
enum SessionMode { MODE_PRACTICE = 0, MODE_ONCOURSE, MODE_SPEED };

// ─── Screen layout ────────────────────────────────────────────────────────────
// Geometry used by the draw code (display.cpp).
// A "bar" is a full-width hint strip at the bottom of a screen.

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

// Full-width list rows used by the main menu and mode-select screens.
// 56 + 4×64 = 312 ≤ 320 — four rows fit.
#define MENU_HDR_H    56                 // title bar height
#define MENU_ROW_H    64                 // big list rows
#define MENU_ROW_GAP   8

// Session menu — five rows (Resume / Change Club / Layout / Settings /
// End Session) need tighter rows: 56 + 5×48 = 296 ≤ 320.
#define SMENU_ROW_H   48
#define SMENU_N_ROWS   5

// Club picker — a vertical scrollable list (UP/DOWN move the highlight).
#define PICK_HDR_H    48
#define PICK_ROW_H    52
#define PICK_ROWS     ((SCR_H - PICK_HDR_H) / PICK_ROW_H)   // visible rows

// Settings screen — 7 rows under the header: 48 + 7×38 = 314 ≤ 320.
//   0 Units   1 Color   2 Layout   3 Reset Stats   4 Clear History
//   5 Radar Cal.   6 Back
#define SET_HDR_H    48                  // header height
#define SET_ROW_H    38                  // height of each item row
#define SET_N_ROWS    7

// Shot history — header bar, a column-label strip, then list rows.
#define HIST_HDR_H   48                  // header (title)
#define HIST_COL_H   24                  // column-label strip below the header
#define HIST_ROW_H   34
#define HIST_ROWS    ((SCR_H - HIST_HDR_H - HIST_COL_H) / HIST_ROW_H)  // 7

// Calibration bottom bar shows the three button bindings:
// [DOWN -10] [OK SAVE] [UP +10], each one COL_W wide at y = BAR_Y.
