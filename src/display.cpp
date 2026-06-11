#include <Arduino.h>
#include <TFT_eSPI.h>
#include "display.h"
#include "config.h"
#include "clubs.h"

// The TFT object lives here — all drawing goes through this module.
static TFT_eSPI tft;

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

// ─── Hit-testing ──────────────────────────────────────────────────────────────

static inline bool in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

int ui_splash_hit(int x, int y)
{
    if (y >= BAR_Y) return 2;                                   // settings bar
    if (in_rect(x, y, COL_W * 2, ROW_H, COL_W, ROW_H)) return 1; // club circle
    return 0;
}

int ui_result_hit(int /*x*/, int /*y*/) { return 1; }          // any tap dismisses

int ui_settings_hit(int /*x*/, int y)
{
    if (y >= SET_DONE_Y) return 9;                              // DONE bar
    for (int i = 0; i < SET_N_ROWS; i++) {
        int top = SET_HDR_H + i * SET_ROW_H;
        if (y >= top && y < top + SET_ROW_H) return i;
    }
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
    uint16_t lc = dimmed ? COL_DIM : TFT_CYAN;
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

static void draw_club_tile(int col, int row, int club_idx, bool tap_hint = false)
{
    const int cx = col * COL_W + COL_W / 2;
    const int cy = row * ROW_H + ROW_H / 2;
    tft.fillCircle(cx, cy, 43, COL_BTN_BG);
    tft.drawCircle(cx, cy, 43, TFT_CYAN);
    tft.drawCircle(cx, cy, 44, TFT_CYAN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE, COL_BTN_BG);
    tft.drawString(CLUBS[club_idx].abbr, cx, cy);
    if (tap_hint) {
        tft.setTextFont(1); tft.setTextColor(COL_UNIT, TFT_BLACK);
        tft.drawString("TAP TO CHANGE", cx, cy + 50);
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
    draw_club_tile(2, 1, club_idx, true);

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(2); tft.setTextColor(COL_DIM, TFT_BLACK);
    tft.drawString("SWING WHEN READY", SCR_W / 2, ROW_H + 4);

    // Bottom action bar — full-width SETTINGS touch button.
    draw_button(0, BAR_Y, SCR_W, BAR_H, "SETTINGS", COL_BTN_BRD, TFT_CYAN);
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

    // Mini row — tap-to-continue hint (any tap dismisses).
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("TAP TO CONTINUE", SCR_W / 2, ROW_H * 2 + MINI_ROW_H / 2);
}

// ─── Settings screen ──────────────────────────────────────────────────────────

void ui_settings_draw(int club_idx, bool use_mph, bool reset_done)
{
    tft.fillScreen(TFT_BLACK);

    // Header
    tft.fillRect(0, 0, SCR_W, SET_HDR_H, TFT_NAVY);
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_CYAN, TFT_NAVY);
    tft.drawString("Settings", 16, SET_HDR_H / 2);
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_NAVY);
    tft.setTextDatum(MR_DATUM);
    tft.drawString("tap an item", SCR_W - 12, SET_HDR_H / 2);

    char unit_val[8];
    char reset_val[12];
    snprintf(unit_val,  sizeof(unit_val),  "%s", use_mph ? "mph" : "km/h");
    snprintf(reset_val, sizeof(reset_val), "%s", reset_done ? "Done!" : CLUBS[club_idx].name);

    const char* labels[SET_N_ROWS] = { "Units", "Reset Stats", "Radar Cal.", "Touch Cal." };
    const char* values[SET_N_ROWS] = { unit_val, reset_val,     "\x10",      "\x10"       };

    // Tappable item rows — rounded card strips with cyan accent stripe.
    for (int i = 0; i < SET_N_ROWS; i++) {
        const int y = SET_HDR_H + i * SET_ROW_H;
        tft.drawFastHLine(0, y, SCR_W, COL_DIV);
        tft.fillRoundRect(4, y + 4, SCR_W - 8, SET_ROW_H - 8, BTN_RADIUS / 2, COL_BTN_BG);
        tft.fillRoundRect(4, y + 4, 6, SET_ROW_H - 8, BTN_RADIUS / 2, TFT_CYAN);  // accent

        tft.setTextDatum(ML_DATUM);
        tft.setTextFont(4); tft.setTextColor(TFT_WHITE, COL_BTN_BG);
        tft.drawString(labels[i], 22, y + SET_ROW_H / 2);

        uint16_t vc = (i == 1 && reset_done) ? TFT_GREEN : TFT_CYAN;
        tft.setTextColor(vc, COL_BTN_BG);
        tft.setTextDatum(MR_DATUM);
        tft.drawString(values[i], SCR_W - 16, y + SET_ROW_H / 2);
    }

    // DONE bar — exit settings.
    draw_button(0, SET_DONE_Y, SCR_W, SCR_H - SET_DONE_Y, "DONE",
                COL_BTN_BRD, TFT_GREEN);
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
