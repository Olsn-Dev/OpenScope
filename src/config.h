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

// ─── Touch layout ─────────────────────────────────────────────────────────────
// Geometry shared between the draw code (display.cpp) and hit-testing.
// A "bar" is a full-width action strip at the bottom of a screen.

#define BAR_H        60                  // bottom action-bar height
#define BAR_Y        (SCR_H - BAR_H)     // 260 — top edge of bottom bars

// Settings screen
#define SET_HDR_H    48                  // header height
#define SET_ROW_H    50                  // height of each tappable item row
#define SET_N_ROWS    4                  // Units, Reset, Radar cal, Touch cal
#define SET_DONE_Y   256                 // top of the DONE bar (256–320)

// Calibration bottom bar holds three buttons: [-10] [SAVE] [+10],
// each one COL_W wide at y = BAR_Y.
