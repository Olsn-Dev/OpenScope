#include <Arduino.h>
#include <TFT_eSPI.h>
#include "display.h"
#include "config.h"
#include "clubs.h"

// The TFT object lives here — all drawing goes through this module.
static TFT_eSPI tft;

// ─── Theme ──────────────────────────────────────────────────────────────────
// Only the metric-label colour changes between the two LM1 themes; everything
// else (black bg, white numbers, grey units) is shared. Default: black theme.
static uint16_t s_label_col = COL_LABEL_BLACK;

void ui_set_theme(bool blue_theme)
{
    s_label_col = blue_theme ? COL_LABEL_BLUE : COL_LABEL_BLACK;
}

// ─── Unit helpers ─────────────────────────────────────────────────────────────

float disp_speed(float kmh, bool use_mph) { return use_mph ? kmh * 0.621371f : kmh; }
float disp_dist(float m,    bool use_mph) { return use_mph ? m   * 1.09361f  : m;   }
const char* speed_unit(bool use_mph)      { return use_mph ? "mph"  : "km/h"; }
const char* dist_unit(bool use_mph)       { return use_mph ? "yds"  : "m";    }

// ─── Initialisation ───────────────────────────────────────────────────────────

void display_init()
{
    tft.init();
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(TFT_BLACK);
}

void display_splash()
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4); tft.setTextSize(2);              // ~52 px wordmark
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("OpenScope", SCR_W / 2, SCR_H / 2 - 30);
    tft.setTextSize(1);                                  // others assume size 1
    tft.drawFastHLine(SCR_W / 2 - 92, SCR_H / 2 + 6, 184, s_label_col);
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("GOLF LAUNCH MONITOR", SCR_W / 2, SCR_H / 2 + 26);
    tft.drawString(FW_VERSION, SCR_W / 2, SCR_H / 2 + 48);
    delay(1000);
}

void display_goodbye()
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(COL_DIM, TFT_BLACK);
    tft.drawString("Goodbye", SCR_W / 2, SCR_H / 2);
    delay(700);
    tft.fillScreen(TFT_BLACK);
}

// ─── Touch ────────────────────────────────────────────────────────────────────

static bool s_prev_touch = false;   // for rising-edge tap detection

void display_set_touch_cal(const uint16_t* cal)
{
    tft.setTouch((uint16_t*)cal);
}

void display_touch_calibrate(uint16_t cal[5])
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Touch each arrow in turn", SCR_W / 2, SCR_H / 2 - 12);
    tft.drawString("to calibrate the screen",  SCR_W / 2, SCR_H / 2 + 12);
    delay(1200);
    tft.fillScreen(TFT_BLACK);
    tft.calibrateTouch(cal, TFT_CYAN, TFT_BLACK, 18);
    tft.setTouch(cal);
}

bool ui_get_tap(int* x, int* y)
{
    uint16_t tx, ty;
    bool pressed = tft.getTouch(&tx, &ty);   // true while a valid press is held
    bool edge    = pressed && !s_prev_touch; // fire once, on press
    s_prev_touch = pressed;
    if (edge) { *x = (int)tx; *y = (int)ty; }
    return edge;
}

// ─── Gestures ───────────────────────────────────────────────────────────────
// A press/drag/release recogniser layered on the same XPT2046 readback.
// It tracks the press-down point and the last point seen while held, then on
// release classifies the motion as a tap or a directional swipe. Tap fires on
// release (not press) so a drag can be distinguished from a tap; on a tap the
// returned (x,y) is the press location, on a swipe it is the *start* location
// (handy for left-edge "swipe back" detection).
//
// NOTE: resistive panels give noisy samples right at lift-off, so we classify
// on the last *held* sample, never on the release frame itself. Momentum/
// inertial scrolling isn't available on TFT_eSPI — lists scroll by swipe steps.

static bool s_g_down = false;
static int  s_g_sx, s_g_sy, s_g_lx, s_g_ly;   // start + last-held coordinates

UiGesture ui_get_gesture(int* x, int* y)
{
    uint16_t tx, ty;
    bool pressed = tft.getTouch(&tx, &ty);

    if (pressed) {
        if (!s_g_down) { s_g_down = true; s_g_sx = s_g_lx = tx; s_g_sy = s_g_ly = ty; }
        else           { s_g_lx = tx; s_g_ly = ty; }
        return GES_NONE;                       // still down — wait for release
    }

    if (!s_g_down) return GES_NONE;            // idle
    s_g_down = false;                          // falling edge: classify

    const int dx = s_g_lx - s_g_sx, dy = s_g_ly - s_g_sy;
    const int adx = abs(dx), ady = abs(dy);

    // Small movement → tap (debounces digitizer jitter).
    if (adx < TAP_MOVE_MAX && ady < TAP_MOVE_MAX) {
        *x = s_g_sx; *y = s_g_sy; return GES_TAP;
    }
    if (adx >= ady) {                          // horizontal-dominant
        if (adx < SWIPE_MIN) { *x = s_g_sx; *y = s_g_sy; return GES_TAP; }
        *x = s_g_sx; *y = s_g_sy;
        return dx > 0 ? GES_SWIPE_R : GES_SWIPE_L;
    }
    if (ady < SWIPE_MIN) { *x = s_g_sx; *y = s_g_sy; return GES_TAP; }
    *x = s_g_sx; *y = s_g_sy;
    return dy > 0 ? GES_SWIPE_D : GES_SWIPE_U;
}

// ─── Hit-testing ──────────────────────────────────────────────────────────────

static inline bool in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// Session "ready" screen (both layouts share a bottom nav strip):
//   1 = club pill (open picker)   2 = Menu/gear (settings)
//   3 = Back (to mode select)     4 = a metric cell tapped (Advanced only)
//   0 = none
int ui_splash_hit(int x, int y)
{
    if (y >= BAR_Y) {                                  // bottom nav strip
        if (x < 150)          return 3;                // Back (bottom-left)
        if (x > SCR_W - 150)  return 2;                // Menu/settings (bottom-right)
        return 0;
    }
    if (in_rect(x, y, PILL_X, PILL_Y, PILL_W, PILL_H)) return 1;   // club pill
    // A tap on a metric cell jumps into Large Digit for that metric: the whole
    // top row (Club/Ball/Smash) plus the bottom-row Carry/Total cells.
    if (y < ROW_H) return 4;
    if (y < ROW_H * 2 && x < COL_W * 2) return 4;
    return 0;
}

// Large-Digit screen hit-test: 1 = club pill, 2 = Menu/settings, 3 = Back,
// 0 = none (the metric area cycles by swipe, not tap).
int ui_large_hit(int x, int y)
{
    if (y >= BAR_Y) {
        if (x < 150)         return 3;
        if (x > SCR_W - 150) return 2;
        return 0;
    }
    if (in_rect(x, y, LPILL_X, LPILL_Y, PILL_W, PILL_H)) return 1;
    return 0;
}

// Map a top-row Advanced cell tap to a UiMetric (0=Club,1=Ball,2=Smash). The
// bottom row's Carry/Total are columns 0/1 in row 1.
int ui_advanced_metric_at(int x, int y)
{
    int col = x / COL_W; if (col > 2) col = 2;
    if (y < ROW_H) return col;                         // Club / Ball / Smash
    if (col == 0)  return MET_CARRY;
    if (col == 1)  return MET_TOTAL;
    return -1;                                         // pill cell
}

int ui_result_hit(int /*x*/, int /*y*/) { return 1; }          // any tap dismisses

// Settings: 0..5 = item rows, 9 = Back (header chevron / exit), -1 = none.
int ui_settings_hit(int x, int y)
{
    if (y < SET_HDR_H) return (x < BACK_W) ? 9 : -1;           // header back
    for (int i = 0; i < SET_N_ROWS; i++) {
        int top = SET_HDR_H + i * SET_ROW_H;
        if (y >= top && y < top + SET_ROW_H) return i;
    }
    return -1;
}

// Main menu: 0=Start Session, 1=Shot History, 2=Settings, 3=Shut Down, -1=none.
int ui_menu_hit(int /*x*/, int y)
{
    for (int i = 0; i < 4; i++) {
        int top = MENU_HDR_H + i * MENU_ROW_H;
        if (y >= top && y < top + MENU_ROW_H - MENU_ROW_GAP) return i;
    }
    return -1;
}

// Mode select: 0=Practice, 1=On Course, 2=Speed Training, 3=Back, -1=none.
int ui_mode_hit(int x, int y)
{
    if (y < MENU_HDR_H && x < BACK_W) return 3;        // back chevron
    for (int i = 0; i < 3; i++) {
        int top = MENU_HDR_H + i * MENU_ROW_H;
        if (y >= top && y < top + MENU_ROW_H - MENU_ROW_GAP) return i;
    }
    return -1;
}

// Club picker: returns the club index tapped, 99 = Back, -1 = none.
// `scroll` is the index of the first visible row.
int ui_picker_hit(int x, int y, int scroll)
{
    if (y < PICK_HDR_H) return (x < BACK_W) ? 99 : -1; // back chevron
    int row = (y - PICK_HDR_H) / PICK_ROW_H;
    int idx = scroll + row;
    if (idx >= 0 && idx < NUM_CLUBS) return idx;
    return -1;
}

// Shot history: 99 = Back (header chevron), 98 = Clear (header right), -1 = none.
// The list rows themselves aren't tappable — scrolling is by swipe.
int ui_history_hit(int x, int y)
{
    if (y >= HIST_HDR_H) return -1;
    if (x < BACK_W)      return 99;
    if (x > SCR_W - 90)  return 98;
    return -1;
}

int ui_cal_hit(int x, int y)
{
    if (y < BAR_Y) return 0;
    if (x < COL_W)      return 1;   // -10
    if (x < COL_W * 2)  return 2;   // save + exit
    return 3;                       // +10
}

// ─── Touch button primitive ───────────────────────────────────────────────────

static void draw_button(int x, int y, int w, int h, const char* label,
                        uint16_t border, uint16_t text_col, bool filled = true)
{
    if (filled) tft.fillRoundRect(x + 1, y + 1, w - 2, h - 2, BTN_RADIUS, COL_BTN_BG);
    tft.drawRoundRect(x, y, w, h, BTN_RADIUS, border);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4); tft.setTextColor(text_col, filled ? COL_BTN_BG : TFT_BLACK);
    tft.drawString(label, x + w / 2, y + h / 2);
}

// ─── Tile grid primitives ─────────────────────────────────────────────────────

static void draw_grid_lines()
{
    tft.drawFastVLine(COL_W,     0, SCR_H, COL_DIV);
    tft.drawFastVLine(COL_W * 2, 0, SCR_H, COL_DIV);
    tft.drawFastHLine(0, ROW_H,     SCR_W, COL_DIV);
    tft.drawFastHLine(0, ROW_H * 2, SCR_W, COL_DIV);  // mini row divider
}

static void draw_tile(int col, int row,
                      const char* label, const char* number, const char* unit,
                      uint16_t num_col, bool dimmed = false)
{
    const int cx = col * COL_W + COL_W / 2;
    const int y0 = row * ROW_H;
    uint16_t lc = dimmed ? COL_DIM : s_label_col;   // themed label colour
    uint16_t nc = dimmed ? COL_DIM : num_col;
    uint16_t uc = dimmed ? COL_DIM : COL_UNIT;

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(4); tft.setTextColor(lc, TFT_BLACK);
    tft.drawString(label, cx, y0 + 28);
    tft.setTextFont(7); tft.setTextColor(nc, TFT_BLACK);
    tft.drawString(number, cx, y0 + 62);
    tft.setTextFont(2); tft.setTextColor(uc, TFT_BLACK);
    tft.drawString(unit, cx, y0 + 118);
}

// LM1-style club selector: a rounded-rectangle *outline* pill holding the
// current club abbreviation. This is the primary interactive control on the
// Advanced screen — tap it to open the club picker. `pressed` draws the
// momentary highlight state required for touch feedback.
static void draw_club_pill(int x, int y, int w, int h, int club_idx,
                           bool tap_hint = false, bool pressed = false)
{
    const int cx = x + w / 2, cy = y + h / 2;
    tft.fillRoundRect(x, y, w, h, BTN_RADIUS, pressed ? COL_BTN_BRD : COL_BTN_BG);
    tft.drawRoundRect(x, y, w, h, BTN_RADIUS, s_label_col);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, BTN_RADIUS, s_label_col);
    tft.setTextDatum(MC_DATUM);
    // Font 4 (full ASCII) — fonts 6/7/8 are numeric-only and can't draw the
    // alphabetic club abbreviations ("PW", "3W", "LW", …).
    tft.setTextFont(4);
    tft.setTextColor(TFT_WHITE, pressed ? COL_BTN_BRD : COL_BTN_BG);
    tft.drawString(CLUBS[club_idx].abbr, cx, cy - 2);
    if (tap_hint) {
        tft.setTextFont(1); tft.setTextColor(COL_UNIT, TFT_BLACK);
        tft.drawString("TAP TO CHANGE", cx, y + h + 8);
    }
}

// Convenience: draw the club pill centred in the bottom-right grid cell.
static void draw_club_tile(int /*col*/, int /*row*/, int club_idx,
                           bool tap_hint = false)
{
    draw_club_pill(PILL_X, PILL_Y, PILL_W, PILL_H, club_idx, tap_hint);
}

// Gear glyph in the top-right corner of the session screens (opens Settings).
static void draw_gear(int cx, int cy)
{
    tft.drawCircle(cx, cy, 9, s_label_col);
    tft.fillCircle(cx, cy, 3, s_label_col);
    for (int a = 0; a < 360; a += 45) {       // eight teeth
        float r = a * 0.0174533f;
        int x0 = cx + (int)(cosf(r) * 9),  y0 = cy + (int)(sinf(r) * 9);
        int x1 = cx + (int)(cosf(r) * 13), y1 = cy + (int)(sinf(r) * 13);
        tft.drawLine(x0, y0, x1, y1, s_label_col);
    }
}

// ─── Screens ──────────────────────────────────────────────────────────────────
//
// Tile layout (all screens):
//   ┌──────────┬──────────┬──────────┐
//   │  CLUB    │  BALL    │  SMASH   │  ← row 0
//   ├──────────┼──────────┼──────────┤
//   │  CARRY   │  TOTAL   │  [Club]  │  ← row 1
//   └──────────┴──────────┴──────────┘
//   (mini row below: tap-to-continue hint)

// Bottom navigation strip shared by the session "ready" screens (both layouts).
// Left = Back (to mode select), centre = a hint, right = Menu/Settings (gear).
// These map to ui_splash_hit() codes 3 / 0 / 2.
static void draw_session_nav(const char* hint)
{
    const int by = BAR_Y + BAR_H / 2 + 1;

    // Filled "dock" strip — makes the tappable bar read as a control surface.
    tft.fillRect(0, BAR_Y + 1, SCR_W, BAR_H - 1, COL_TILE_BG);
    tft.drawFastHLine(0, BAR_Y, SCR_W, COL_DIV);

    // Back chevron + label (bottom-left)
    tft.drawLine(24, by - 9, 14, by, s_label_col);
    tft.drawLine(14, by, 24, by + 9, s_label_col);
    tft.drawLine(25, by - 9, 15, by, s_label_col);
    tft.drawLine(15, by, 25, by + 9, s_label_col);
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2); tft.setTextColor(s_label_col, COL_TILE_BG);
    tft.drawString("Back", 34, by);

    // Centre hint
    tft.setTextDatum(MC_DATUM); tft.setTextColor(COL_UNIT, COL_TILE_BG);
    tft.drawString(hint, SCR_W / 2, by);

    // Menu / settings gear (bottom-right)
    draw_gear(SCR_W - 24, by);
    tft.setTextDatum(MR_DATUM); tft.setTextColor(s_label_col, COL_TILE_BG);
    tft.drawString("Menu", SCR_W - 42, by);
}

// ── Advanced layout ────────────────────────────────────────────────────────

void ui_splash(int club_idx, const ClubStats* stats, bool use_mph)
{
    tft.fillScreen(TFT_BLACK);
    draw_grid_lines();

    draw_tile(0, 0, "Club",  "--", speed_unit(use_mph), TFT_WHITE, true);
    draw_tile(1, 0, "Ball",  "--", speed_unit(use_mph), TFT_WHITE, true);
    draw_tile(2, 0, "Smash", "--", "",                  TFT_WHITE, true);

    const ClubStats& s = stats[club_idx];
    char avg[8], best[8];
    if (s.count > 0) {
        snprintf(avg,  sizeof(avg),  "%.0f", disp_dist(s.sum / s.count, use_mph));
        snprintf(best, sizeof(best), "%.0f", disp_dist(s.max_c,          use_mph));
    } else {
        snprintf(avg,  sizeof(avg),  "--");
        snprintf(best, sizeof(best), "--");
    }
    draw_tile(0, 1, "Avg",  avg,  dist_unit(use_mph), TFT_WHITE);
    draw_tile(1, 1, "Best", best, dist_unit(use_mph), TFT_GREEN);
    draw_club_tile(2, 1, club_idx);

    draw_session_nav("SWING WHEN READY    SWIPE L/R: LAYOUT");
}

void ui_result(float ball_kmh, float club_kmh, float smash,
               float carry_m,  float total_m,
               int club_idx, bool use_mph)
{
    tft.fillScreen(TFT_BLACK);
    draw_grid_lines();

    char buf[12];

    // Row 0 — club speed, ball speed, smash factor.
    if (club_kmh > 0.0f) {
        snprintf(buf, sizeof(buf), "%.0f", disp_speed(club_kmh, use_mph));
        draw_tile(0, 0, "Club", buf, speed_unit(use_mph), TFT_WHITE);
    } else {
        draw_tile(0, 0, "Club", "--", speed_unit(use_mph), TFT_WHITE, true);
    }

    snprintf(buf, sizeof(buf), "%.0f", disp_speed(ball_kmh, use_mph));
    draw_tile(1, 0, "Ball", buf, speed_unit(use_mph), TFT_WHITE);

    if (smash > 0.0f) {
        snprintf(buf, sizeof(buf), "%.2f", smash);
        draw_tile(2, 0, "Smash", buf, "", TFT_GREEN);
    } else {
        draw_tile(2, 0, "Smash", "--", "", TFT_WHITE, true);
    }

    // Row 1 — carry, total, selected club.
    snprintf(buf, sizeof(buf), "%.0f", disp_dist(carry_m, use_mph));
    draw_tile(0, 1, "Carry", buf, dist_unit(use_mph), TFT_WHITE);

    snprintf(buf, sizeof(buf), "%.0f", disp_dist(total_m, use_mph));
    draw_tile(1, 1, "Total", buf, dist_unit(use_mph), TFT_WHITE);

    draw_club_tile(2, 1, club_idx);

    // Mini row — tap-to-continue hint (any tap dismisses), same dock styling
    // as the session nav so the bottom strip is consistent across screens.
    tft.fillRect(0, BAR_Y + 1, SCR_W, BAR_H - 1, COL_TILE_BG);
    tft.drawFastHLine(0, BAR_Y, SCR_W, COL_DIV);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2); tft.setTextColor(s_label_col, COL_TILE_BG);
    tft.drawString("TAP TO CONTINUE", SCR_W / 2, ROW_H * 2 + MINI_ROW_H / 2);
}

// ── Large Digit layout ─────────────────────────────────────────────────────
// One metric, huge. Label on top, big white value centred in the left area,
// unit beneath, club pill on the right. `hint` drives the bottom nav strip.

static void draw_large(const char* label, const char* value, const char* unit,
                       int club_idx, const char* hint, uint16_t value_col)
{
    tft.fillScreen(TFT_BLACK);

    const int area_w = SCR_W - PILL_W - 40;        // space left of the pill
    const int cx     = area_w / 2 + 8;

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(4); tft.setTextColor(s_label_col, TFT_BLACK);
    tft.drawString(label, cx, 28);

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(8); tft.setTextColor(value_col, TFT_BLACK);   // ~75 px digits
    tft.drawString(value, cx, 130);

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(4); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString(unit, cx, 188);

    // Club pill on the right, vertically centred in the metric area.
    draw_club_pill(LPILL_X, LPILL_Y, PILL_W, PILL_H, club_idx, true);

    draw_session_nav(hint);
}

// Format one metric's value+unit into caller buffers, returning its label and
// colour. Mirrors the physics in main.cpp. club/smash may be "unset" (≤0).
static const char* large_metric_strings(UiMetric m,
        float ball_kmh, float club_kmh, float smash,
        float carry_m, float total_m, bool use_mph,
        char* val, size_t vn, const char** unit, uint16_t* col)
{
    *col = TFT_WHITE;
    switch (m) {
        case MET_CLUB:
            *unit = speed_unit(use_mph);
            if (club_kmh > 0.0f) snprintf(val, vn, "%.0f", disp_speed(club_kmh, use_mph));
            else                 snprintf(val, vn, "--");
            return "Club Speed";
        case MET_BALL:
            *unit = speed_unit(use_mph);
            snprintf(val, vn, "%.0f", disp_speed(ball_kmh, use_mph));
            return "Ball Speed";
        case MET_SMASH:
            *unit = "Factor"; *col = TFT_GREEN;
            if (smash > 0.0f) snprintf(val, vn, "%.2f", smash);
            else            { snprintf(val, vn, "--"); *col = TFT_WHITE; }
            return "Smash";
        case MET_CARRY:
            *unit = dist_unit(use_mph);
            snprintf(val, vn, "%.0f", disp_dist(carry_m, use_mph));
            return "Carry";
        default: /* MET_TOTAL */
            *unit = dist_unit(use_mph);
            snprintf(val, vn, "%.0f", disp_dist(total_m, use_mph));
            return "Total Distance";
    }
}

// Large-Digit "ready" — shows the focused metric with placeholder dashes.
void ui_large_ready(UiMetric metric, int club_idx, bool use_mph)
{
    char val[12]; const char* unit; uint16_t col;
    const char* label = large_metric_strings(metric, 0, 0, 0, 0, 0, use_mph,
                                             val, sizeof(val), &unit, &col);
    snprintf(val, sizeof(val), "--");
    draw_large(label, val, unit, club_idx, "SWIPE U/D: METRIC   L/R: LAYOUT",
               TFT_WHITE);
}

// Large-Digit result — shows the focused metric's measured/modeled value.
void ui_large_result(UiMetric metric,
                     float ball_kmh, float club_kmh, float smash,
                     float carry_m, float total_m,
                     int club_idx, bool use_mph)
{
    char val[12]; const char* unit; uint16_t col;
    const char* label = large_metric_strings(metric, ball_kmh, club_kmh, smash,
                                             carry_m, total_m, use_mph,
                                             val, sizeof(val), &unit, &col);
    draw_large(label, val, unit, club_idx, "TAP TO CONTINUE   SWIPE U/D: METRIC", col);
}

// ── Speed Training ─────────────────────────────────────────────────────────
// A single huge swing-speed number, full width (no club pill).

void ui_speed(float swing_kmh, bool use_mph, bool have)
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(4); tft.setTextColor(s_label_col, TFT_BLACK);
    tft.drawString("SWING SPEED", SCR_W / 2, 30);

    char val[12];
    if (have) snprintf(val, sizeof(val), "%.0f", disp_speed(swing_kmh, use_mph));
    else      snprintf(val, sizeof(val), "--");
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(8); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(val, SCR_W / 2, 140);

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(4); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString(speed_unit(use_mph), SCR_W / 2, 198);

    // Back-only nav (no club / layout switching in speed mode) — dock styling.
    const int by = BAR_Y + BAR_H / 2 + 1;
    tft.fillRect(0, BAR_Y + 1, SCR_W, BAR_H - 1, COL_TILE_BG);
    tft.drawFastHLine(0, BAR_Y, SCR_W, COL_DIV);
    tft.drawLine(24, by - 9, 14, by, s_label_col);
    tft.drawLine(14, by, 24, by + 9, s_label_col);
    tft.drawLine(25, by - 9, 15, by, s_label_col);
    tft.drawLine(15, by, 25, by + 9, s_label_col);
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2); tft.setTextColor(s_label_col, COL_TILE_BG);
    tft.drawString("Back", 34, by);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(COL_UNIT, COL_TILE_BG);
    tft.drawString("SWING TO MEASURE", SCR_W / 2, by);
}

// ── Main menu & mode select ────────────────────────────────────────────────

// Shared full-width list-row renderer for the menu / mode screens. `pressed`
// draws the momentary touch-feedback state (lighter fill).
static void draw_menu_row(int i, const char* label, uint16_t accent,
                          bool pressed = false)
{
    const int y  = MENU_HDR_H + i * MENU_ROW_H;
    const int h  = MENU_ROW_H - MENU_ROW_GAP;
    const int cy = y + h / 2 + 2;
    const uint16_t bg = pressed ? COL_BTN_BRD : COL_BTN_BG;
    tft.fillRoundRect(8, y + 4, SCR_W - 16, h - 4, BTN_RADIUS, bg);
    tft.fillRoundRect(8, y + 4, 6, h - 4, BTN_RADIUS, accent);   // accent stripe
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE, bg);
    tft.drawString(label, 28, cy);
    // "›" affordance chevron on the right — these rows navigate somewhere.
    tft.drawLine(SCR_W - 34, cy - 8, SCR_W - 26, cy, COL_UNIT);
    tft.drawLine(SCR_W - 26, cy, SCR_W - 34, cy + 8, COL_UNIT);
    tft.drawLine(SCR_W - 35, cy - 8, SCR_W - 27, cy, COL_UNIT);
    tft.drawLine(SCR_W - 27, cy, SCR_W - 35, cy + 8, COL_UNIT);
}

// Press-feedback flashes: redraw the tapped control highlighted and hold it
// just long enough to register. The next screen redraw restores it.

// Labels/accents mirrored from ui_menu_draw / ui_mode_draw — the flash has to
// redraw the full row since the fill repaints label and stripe.
static const struct { const char* label; uint16_t accent; } MENU_ROWS[4] = {
    { "Start Session", TFT_GREEN  },
    { "Shot History",  TFT_YELLOW },
    { "Settings",      TFT_CYAN   },
    { "Shut Down",     TFT_RED    },
};
static const struct { const char* label; uint16_t accent; } MODE_ROWS[3] = {
    { "Practice Range", TFT_GREEN  },
    { "On Course",      TFT_CYAN   },
    { "Speed Training", TFT_YELLOW },
};

void ui_menu_flash(int row)
{
    if (row < 0 || row > 3) return;
    draw_menu_row(row, MENU_ROWS[row].label, MENU_ROWS[row].accent, true);
    delay(80);
}

void ui_mode_flash(int row)
{
    if (row < 0 || row > 2) return;
    draw_menu_row(row, MODE_ROWS[row].label, MODE_ROWS[row].accent, true);
    delay(80);
}

void ui_pill_flash(bool large_layout, int club_idx)
{
    if (large_layout) draw_club_pill(LPILL_X, LPILL_Y, PILL_W, PILL_H,
                                     club_idx, true, true);
    else              draw_club_pill(PILL_X,  PILL_Y,  PILL_W, PILL_H,
                                     club_idx, false, true);
    delay(80);
}

// Shared sub-screen header: dark card strip with a themed accent underline,
// optional back chevron on the left and an optional action label on the right.
// Used by the menu, mode-select, club-picker, settings and history screens.
static void draw_screen_header(const char* title, bool back, int h,
                               const char* right = nullptr,
                               uint16_t right_col = TFT_CYAN)
{
    tft.fillRect(0, 0, SCR_W, h - 2, COL_TILE_BG);
    tft.drawFastHLine(0, h - 2, SCR_W, s_label_col);     // accent underline
    tft.drawFastHLine(0, h - 1, SCR_W, COL_DIV);
    const int y = (h - 2) / 2;
    if (back) {
        tft.drawLine(24, y - 9, 14, y, s_label_col);
        tft.drawLine(14, y, 24, y + 9, s_label_col);
        tft.drawLine(25, y - 9, 15, y, s_label_col);
        tft.drawLine(15, y, 25, y + 9, s_label_col);
        tft.setTextDatum(ML_DATUM);
        tft.setTextFont(2); tft.setTextColor(s_label_col, COL_TILE_BG);
        tft.drawString("Back", 34, y);
    }
    if (right) {
        tft.setTextDatum(MR_DATUM);
        tft.setTextFont(2); tft.setTextColor(right_col, COL_TILE_BG);
        tft.drawString(right, SCR_W - 16, y);
    }
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE, COL_TILE_BG);
    tft.drawString(title, SCR_W / 2, y);
}

void ui_menu_draw()
{
    tft.fillScreen(TFT_BLACK);
    draw_screen_header("OpenScope", false, MENU_HDR_H);
    for (int i = 0; i < 4; i++)
        draw_menu_row(i, MENU_ROWS[i].label, MENU_ROWS[i].accent);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(1); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString(FW_VERSION, SCR_W / 2, SCR_H - 5);
}

void ui_mode_draw()
{
    tft.fillScreen(TFT_BLACK);
    draw_screen_header("Select Mode", true, MENU_HDR_H);
    for (int i = 0; i < 3; i++)
        draw_menu_row(i, MODE_ROWS[i].label, MODE_ROWS[i].accent);
}

// ── Club picker ────────────────────────────────────────────────────────────
// A vertical, swipe-scrollable list. `scroll` is the first visible club index;
// the active club gets a filled highlight pill. (Momentum scrolling isn't
// available on TFT_eSPI; the list pages by swipe-up/down — see main.cpp.)

void ui_picker_draw(int club_idx, int scroll)
{
    tft.fillScreen(TFT_BLACK);

    draw_screen_header("Select Club", true, PICK_HDR_H);

    // Visible rows.
    for (int r = 0; r < PICK_ROWS; r++) {
        int idx = scroll + r;
        if (idx >= NUM_CLUBS) break;
        const int y      = PICK_HDR_H + r * PICK_ROW_H;
        const bool active = (idx == club_idx);
        if (active)
            tft.fillRoundRect(6, y + 3, SCR_W - 12, PICK_ROW_H - 6, BTN_RADIUS, COL_BTN_BG);
        tft.drawFastHLine(0, y, SCR_W, COL_DIV);

        tft.setTextDatum(ML_DATUM);
        tft.setTextFont(4);
        tft.setTextColor(active ? s_label_col : TFT_WHITE,
                         active ? COL_BTN_BG : TFT_BLACK);
        tft.drawString(CLUBS[idx].abbr, 20, y + PICK_ROW_H / 2);
        tft.drawString(CLUBS[idx].name, 90, y + PICK_ROW_H / 2);
        if (active) {
            tft.setTextDatum(MR_DATUM);
            tft.setTextFont(2); tft.setTextColor(TFT_GREEN, COL_BTN_BG);
            tft.drawString("current", SCR_W - 16, y + PICK_ROW_H / 2);
        }
    }

    // Scroll hint arrows when there is more above/below.
    tft.setTextDatum(MC_DATUM); tft.setTextFont(2);
    tft.setTextColor(COL_UNIT, TFT_BLACK);
    if (scroll > 0)                        tft.drawString("swipe up", SCR_W - 60, PICK_HDR_H + 10);
    if (scroll + PICK_ROWS < NUM_CLUBS)    tft.drawString("swipe down", SCR_W - 60, SCR_H - 12);
}

// ─── Shot history ─────────────────────────────────────────────────────────────
// Newest-first table of the persisted shot log. Like the club picker the list
// pages by swipe ↕ (no inertial scrolling on TFT_eSPI); `scroll` is the
// newest-first index of the top visible row.

// Right edges of the four value columns (font 2, MR datum).
#define HCOL_BALL   216
#define HCOL_SMASH  300
#define HCOL_CARRY  384
#define HCOL_TOTAL  (SCR_W - 12)

void ui_history_draw(const ShotRecord* shots, int count, int scroll,
                     bool use_mph)
{
    tft.fillScreen(TFT_BLACK);

    draw_screen_header("Shot History", true, HIST_HDR_H,
                       count > 0 ? "Clear" : nullptr, TFT_RED);

    if (count == 0) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(4); tft.setTextColor(COL_UNIT, TFT_BLACK);
        tft.drawString("No shots yet", SCR_W / 2, SCR_H / 2 - 10);
        tft.setTextFont(2);
        tft.drawString("Hit some balls and they'll show up here",
                       SCR_W / 2, SCR_H / 2 + 18);
        return;
    }

    // Column labels.
    char hdr[16];
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    const int cy = HIST_HDR_H + HIST_COL_H / 2;
    tft.setTextDatum(ML_DATUM);
    tft.drawString("#",    16, cy);
    tft.drawString("CLUB", 60, cy);
    tft.setTextDatum(MR_DATUM);
    snprintf(hdr, sizeof(hdr), "BALL %s", speed_unit(use_mph));
    tft.drawString(hdr, HCOL_BALL, cy);
    tft.drawString("SMASH", HCOL_SMASH, cy);
    snprintf(hdr, sizeof(hdr), "CARRY %s", dist_unit(use_mph));
    tft.drawString(hdr, HCOL_CARRY, cy);
    snprintf(hdr, sizeof(hdr), "TOTAL %s", dist_unit(use_mph));
    tft.drawString(hdr, HCOL_TOTAL, cy);
    tft.drawFastHLine(0, HIST_HDR_H + HIST_COL_H - 1, SCR_W, COL_DIV);

    // Rows, newest first: visible row r shows shots[count - 1 - (scroll + r)].
    char buf[12];
    for (int r = 0; r < HIST_ROWS; r++) {
        const int n = scroll + r;            // newest-first index
        if (n >= count) break;
        const ShotRecord& s = shots[count - 1 - n];
        const int y  = HIST_HDR_H + HIST_COL_H + r * HIST_ROW_H;
        const int ry = y + HIST_ROW_H / 2;
        // Zebra striping — much easier to track a row across six columns.
        const uint16_t bg = (r & 1) ? TFT_BLACK : COL_TILE_BG;
        if (bg != TFT_BLACK) tft.fillRect(0, y, SCR_W, HIST_ROW_H, bg);

        tft.setTextFont(2);
        tft.setTextDatum(ML_DATUM);
        snprintf(buf, sizeof(buf), "%d", count - n);     // shot number, 1 = oldest
        tft.setTextColor(COL_UNIT, bg);
        tft.drawString(buf, 16, ry);
        tft.setTextColor(s_label_col, bg);
        tft.drawString(CLUBS[s.club].abbr, 60, ry);

        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(TFT_WHITE, bg);
        snprintf(buf, sizeof(buf), "%.0f", disp_speed(s.ball_kmh, use_mph));
        tft.drawString(buf, HCOL_BALL, ry);

        if (s.club_kmh > 0.0f) {
            snprintf(buf, sizeof(buf), "%.2f", s.ball_kmh / s.club_kmh);
            tft.setTextColor(TFT_GREEN, bg);
        } else {
            snprintf(buf, sizeof(buf), "--");
            tft.setTextColor(COL_UNIT, bg);
        }
        tft.drawString(buf, HCOL_SMASH, ry);

        tft.setTextColor(TFT_WHITE, bg);
        snprintf(buf, sizeof(buf), "%.0f", disp_dist(s.carry_m, use_mph));
        tft.drawString(buf, HCOL_CARRY, ry);
        snprintf(buf, sizeof(buf), "%.0f", disp_dist(s.total_m, use_mph));
        tft.drawString(buf, HCOL_TOTAL, ry);
    }

    // Scroll hint arrows when there is more above/below.
    tft.setTextDatum(MC_DATUM); tft.setTextFont(2);
    tft.setTextColor(COL_UNIT, TFT_BLACK);
    if (scroll > 0)
        tft.drawString("swipe down", 60, HIST_HDR_H + HIST_COL_H + 10);
    if (scroll + HIST_ROWS < count)
        tft.drawString("swipe up", 60, SCR_H - 10);
}

// ─── Settings screen ──────────────────────────────────────────────────────────

void ui_settings_draw(int club_idx, bool use_mph, bool blue_theme,
                      UiLayout layout, bool reset_done)
{
    tft.fillScreen(TFT_BLACK);

    draw_screen_header("Settings", true, SET_HDR_H);

    char reset_val[12];
    snprintf(reset_val, sizeof(reset_val), "%s",
             reset_done ? "Done!" : CLUBS[club_idx].name);

    // Values mirror the toggles handled in main.cpp's settings_loop().
    const char* labels[SET_N_ROWS] = {
        "Units", "Color", "Layout", "Reset Stats", "Radar Cal.", "Touch Cal."
    };
    const char* values[SET_N_ROWS] = {
        use_mph ? "Mph/Yds" : "Kmh/m",
        blue_theme ? "Blue" : "Black",
        layout == LAYOUT_LARGE_DIGIT ? "Large Digit" : "Advanced",
        reset_val,
        "\x10",                 // ▶  enter radar calibration
        "\x10",                 // ▶  enter touch calibration
    };

    // Tappable item rows — rounded card strips with a themed accent stripe.
    for (int i = 0; i < SET_N_ROWS; i++) {
        const int y = SET_HDR_H + i * SET_ROW_H;
        tft.fillRoundRect(4, y + 3, SCR_W - 8, SET_ROW_H - 6, BTN_RADIUS / 2, COL_BTN_BG);
        tft.fillRoundRect(4, y + 3, 6, SET_ROW_H - 6, BTN_RADIUS / 2, s_label_col);

        tft.setTextDatum(ML_DATUM);
        tft.setTextFont(4); tft.setTextColor(TFT_WHITE, COL_BTN_BG);
        tft.drawString(labels[i], 22, y + SET_ROW_H / 2);

        uint16_t vc = (i == 3 && reset_done) ? TFT_GREEN : TFT_CYAN;
        tft.setTextColor(vc, COL_BTN_BG);
        tft.setTextDatum(MR_DATUM);
        tft.drawString(values[i], SCR_W - 16, y + SET_ROW_H / 2);
    }
}

// Settings press feedback — a bright outline is enough here (re-rendering the
// row needs the value strings, which live in ui_settings_draw's caller state).
void ui_settings_flash(int row)
{
    if (row < 0 || row >= SET_N_ROWS) return;
    const int y = SET_HDR_H + row * SET_ROW_H;
    tft.drawRoundRect(4, y + 3, SCR_W - 8,  SET_ROW_H - 6, BTN_RADIUS / 2, TFT_WHITE);
    tft.drawRoundRect(5, y + 4, SCR_W - 10, SET_ROW_H - 8, BTN_RADIUS / 2, TFT_WHITE);
    delay(80);
}

// ─── Calibration screen ───────────────────────────────────────────────────────

#define CAL_SPEC_Y  34
#define CAL_SPEC_H  120
#define CAL_SPEC_X  10
#define CAL_SPEC_W  (SCR_W - 20)

void ui_cal_header()
{
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, SCR_W, 30, COL_CAL_HDR);
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2); tft.setTextColor(TFT_YELLOW, COL_CAL_HDR);
    tft.drawString("CALIBRATION MODE", 10, 15);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_WHITE, COL_CAL_HDR);
    tft.drawString("tap the buttons below", SCR_W - 8, 15);

    // Frequency axis tick labels
    tft.setTextFont(1); tft.setTextColor(COL_DIM, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    const struct { const char* l; int hz; } ticks[] =
        { {"2k",2000}, {"5k",5000}, {"8k",8000}, {"12k",12000} };
    for (auto& t : ticks) {
        int xp = CAL_SPEC_X + (int)((float)(t.hz - MIN_DETECT_HZ) /
                               (MAX_DETECT_HZ - MIN_DETECT_HZ) * CAL_SPEC_W);
        tft.drawString(t.l, xp, CAL_SPEC_Y + CAL_SPEC_H + 4);
    }

    // Bottom action bar — three touch buttons: [-10] [SAVE] [+10].
    draw_button(0,         BAR_Y, COL_W, BAR_H, "-10",  COL_BTN_BRD, TFT_CYAN);
    draw_button(COL_W,     BAR_Y, COL_W, BAR_H, "SAVE", COL_BTN_BRD, TFT_GREEN);
    draw_button(COL_W * 2, BAR_Y, COL_W, BAR_H, "+10",  COL_BTN_BRD, TFT_CYAN);
}

void ui_cal_update(const double* spectrum,
                   double peak_hz, double peak_mag,
                   float  noise_ema, float max_seen,
                   float  threshold, bool use_mph)
{
    tft.fillRect(CAL_SPEC_X, CAL_SPEC_Y, CAL_SPEC_W, CAL_SPEC_H, TFT_BLACK);

    const int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    const int bin_hi = min((int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE),
                           FFT_SIZE/2 - 1);
    const float scale  = (max_seen > 1.0f) ? (float)(CAL_SPEC_H - 4) / max_seen : 1.0f;
    const float y_base = (float)(CAL_SPEC_Y + CAL_SPEC_H - 1);

    // Spectrum bars
    for (int x = 0; x < CAL_SPEC_W; x++) {
        int bin = bin_lo + (int)((float)x / CAL_SPEC_W * (bin_hi - bin_lo));
        if (bin > bin_hi) break;
        float mag = (float)spectrum[bin];
        int   h   = min((int)(mag * scale), CAL_SPEC_H - 1);
        if (h <= 0) continue;
        tft.drawFastVLine(CAL_SPEC_X + x, (int)y_base - h, h,
                          (mag >= threshold) ? TFT_RED : (uint16_t)0x0460);
    }

    // Threshold line — clamped to top so it is always visible before first swing
    int ty = CAL_SPEC_Y + CAL_SPEC_H - 1 - (int)(threshold * scale);
    if (ty < CAL_SPEC_Y) ty = CAL_SPEC_Y;
    tft.drawFastHLine(CAL_SPEC_X, ty, CAL_SPEC_W, TFT_YELLOW);

    // Peak frequency tick
    if (peak_hz > 0.0) {
        int px = CAL_SPEC_X + (int)(((float)peak_hz - MIN_DETECT_HZ) /
                              (MAX_DETECT_HZ - MIN_DETECT_HZ) * CAL_SPEC_W);
        if (px >= CAL_SPEC_X && px < CAL_SPEC_X + CAL_SPEC_W)
            tft.drawFastVLine(px, CAL_SPEC_Y, 6, TFT_WHITE);
    }

    // Metrics rows — clear only down to the action bar so its buttons survive.
    char buf[48];
    const int MY = CAL_SPEC_Y + CAL_SPEC_H + 14;   // 168
    tft.fillRect(0, MY - 2, SCR_W, BAR_Y - (MY - 2), TFT_BLACK);
    tft.setTextDatum(TL_DATUM);

    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("NOISE FLOOR", 8, MY);
    snprintf(buf, sizeof(buf), "%.1f", noise_ema);
    tft.setTextFont(4); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(buf, 8, MY + 14);

    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("PEAK MAG", 170, MY);
    snprintf(buf, sizeof(buf), "%.1f", peak_mag);
    tft.setTextFont(4);
    tft.setTextColor((peak_mag >= threshold) ? TFT_RED : TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, 170, MY + 14);

    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("MAX SEEN", 332, MY);
    snprintf(buf, sizeof(buf), "%.1f", max_seen);
    tft.setTextFont(4); tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(buf, 332, MY + 14);

    // Peak-frequency readout (Hz + speed) — useful for verifying calibration.
    const int R2Y = MY + 44;   // 212
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("PEAK", 8, R2Y);
    if (peak_hz > 0.0)
        snprintf(buf, sizeof(buf), "%.0f Hz = %.1f %s",
                 peak_hz, disp_speed((float)peak_hz * HZ_TO_KMH, use_mph),
                 speed_unit(use_mph));
    else
        snprintf(buf, sizeof(buf), "---");
    tft.setTextFont(2); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, 56, R2Y);

    // Threshold + suggested value.
    const int R3Y = R2Y + 24;   // 236
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("THRESHOLD", 8, R3Y);
    snprintf(buf, sizeof(buf), "%.0f", threshold);
    tft.setTextFont(4); tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(buf, 110, R3Y - 6);

    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("SUGGESTED", 230, R3Y);
    snprintf(buf, sizeof(buf), "%.0f", max(noise_ema * 4.0f, noise_ema + 20.0f));
    tft.setTextFont(4); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(buf, 340, R3Y - 6);
}
