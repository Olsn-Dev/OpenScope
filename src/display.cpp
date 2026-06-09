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

// ─── Tile grid primitives ─────────────────────────────────────────────────────

static void draw_grid_lines()
{
    tft.drawFastVLine(COL_W,     0, SCR_H, COL_DIV);
    tft.drawFastVLine(COL_W * 2, 0, SCR_H, COL_DIV);
    tft.drawFastHLine(0, ROW_H,  SCR_W,    COL_DIV);
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

static void draw_club_tile(int col, int row, int club_idx)
{
    const int cx = col * COL_W + COL_W / 2;
    const int cy = row * ROW_H + ROW_H / 2;
    tft.drawCircle(cx, cy, 42, TFT_CYAN);
    tft.drawCircle(cx, cy, 43, TFT_CYAN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(CLUBS[club_idx].abbr, cx, cy);
}

// ─── Screens ──────────────────────────────────────────────────────────────────
//
// Tile layout (all screens):
//   ┌──────────┬──────────┬──────────┐
//   │  CLUB    │  BALL    │  LAUNCH  │  ← row 0
//   ├──────────┼──────────┼──────────┤
//   │  CARRY   │  TOTAL   │  [Club]  │  ← row 1
//   └──────────┴──────────┴──────────┘

void ui_splash(int club_idx, const ClubStats* stats, bool use_mph)
{
    tft.fillScreen(TFT_BLACK);
    draw_grid_lines();

    draw_tile(0, 0, "Club",   "--", speed_unit(use_mph), TFT_WHITE, true);
    draw_tile(1, 0, "Ball",   "--", speed_unit(use_mph), TFT_WHITE, true);
    draw_tile(2, 0, "Launch", "--", "\xb0",               TFT_WHITE, true);

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

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(2); tft.setTextColor(COL_DIM, TFT_BLACK);
    tft.drawString("SWING WHEN READY", SCR_W / 2, ROW_H + 4);
}

void ui_result(float ball_kmh, float club_kmh,
               float carry_m,  float total_m,
               float launch_deg, float side_deg,
               int club_idx, bool use_mph)
{
    tft.fillScreen(TFT_BLACK);
    draw_grid_lines();

    char buf[12];

    if (club_kmh > 0.0f) {
        snprintf(buf, sizeof(buf), "%.0f", disp_speed(club_kmh, use_mph));
        draw_tile(0, 0, "Club", buf, speed_unit(use_mph), TFT_WHITE);
    } else {
        draw_tile(0, 0, "Club", "--", speed_unit(use_mph), TFT_WHITE, true);
    }

    snprintf(buf, sizeof(buf), "%.0f", disp_speed(ball_kmh, use_mph));
    draw_tile(1, 0, "Ball", buf, speed_unit(use_mph), TFT_WHITE);

    if (launch_deg > 0.0f) {
        snprintf(buf, sizeof(buf), "%.1f", launch_deg);
        draw_tile(2, 0, "Launch", buf, "\xb0", TFT_GREEN);
    } else {
        draw_tile(2, 0, "Launch", "--", "\xb0", TFT_WHITE, true);
    }

    snprintf(buf, sizeof(buf), "%.0f", disp_dist(carry_m, use_mph));
    draw_tile(0, 1, "Carry", buf, dist_unit(use_mph), TFT_WHITE);

    snprintf(buf, sizeof(buf), "%.0f", disp_dist(total_m, use_mph));
    draw_tile(1, 1, "Total", buf, dist_unit(use_mph), TFT_WHITE);

    draw_club_tile(2, 1, club_idx);

    // ── Bottom bar: side angle (left) + smash factor (right) ─────────────────
    tft.setTextFont(1);
    tft.setTextColor(COL_UNIT, TFT_BLACK);

    // Side angle — always shown; "STRAIGHT" when < 0.5°
    char side_buf[16];
    if (fabsf(side_deg) >= 0.5f)
        snprintf(side_buf, sizeof(side_buf), "%s %.1f\xb0",
                 side_deg >= 0.0f ? "R" : "L", fabsf(side_deg));
    else
        snprintf(side_buf, sizeof(side_buf), "STRAIGHT");
    tft.setTextDatum(BL_DATUM);
    tft.drawString(side_buf, 4, SCR_H - 2);

    if (club_kmh > 0.0f) {
        char sm[16];
        snprintf(sm, sizeof(sm), "smash %.2f", ball_kmh / club_kmh);
        tft.setTextDatum(BR_DATUM);
        tft.drawString(sm, SCR_W - 4, SCR_H - 2);
    }
}

// ─── Settings screen ──────────────────────────────────────────────────────────

void ui_settings_draw(int sel, int club_idx, bool use_mph, bool reset_done)
{
    tft.fillScreen(TFT_BLACK);

    tft.fillRect(0, 0, SCR_W, 48, TFT_NAVY);
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_CYAN, TFT_NAVY);
    tft.drawString("Settings", 16, 24);
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_NAVY);
    tft.setTextDatum(MR_DATUM);
    tft.drawString("scroll=next  select=choose  power=exit", SCR_W - 12, 24);

    char unit_val[8];
    char reset_val[12];
    snprintf(unit_val,  sizeof(unit_val),  "%s", use_mph ? "mph" : "km/h");
    snprintf(reset_val, sizeof(reset_val), "%s", reset_done ? "Done!" : CLUBS[club_idx].name);

    const char* labels[] = { "Units",    "Reset Stats", "Calibration" };
    const char* values[] = { unit_val,    reset_val,     "\x10"        };

    for (int i = 0; i < 3; i++) {
        const int y  = 68 + i * 74;
        const bool active = (i == sel);
        if (active) {
            tft.fillRect(0, y - 2, SCR_W, 54, COL_SEL_BG);
            tft.fillRect(0, y - 2, 4,     54, TFT_CYAN);
        }
        uint16_t bg = active ? COL_SEL_BG : TFT_BLACK;
        tft.setTextDatum(ML_DATUM);
        tft.setTextFont(4);
        tft.setTextColor(active ? TFT_WHITE : COL_UNIT, bg);
        tft.drawString(labels[i], 20, y + 22);

        uint16_t vc = active ? TFT_CYAN : COL_DIM;
        if (i == 1 && reset_done) vc = TFT_GREEN;
        tft.setTextColor(vc, bg);
        tft.setTextDatum(MR_DATUM);
        tft.drawString(values[i], SCR_W - 20, y + 22);
    }

    tft.drawFastHLine(0, SCR_H - 22, SCR_W, COL_DIV);
    tft.setTextDatum(BC_DATUM);
    tft.setTextFont(2); tft.setTextColor(COL_DIM, TFT_BLACK);
    tft.drawString("OpenScope v0.6", SCR_W / 2, SCR_H - 4);
}

// ─── Calibration screen ───────────────────────────────────────────────────────

#define CAL_SPEC_Y  34
#define CAL_SPEC_H  150
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
    tft.drawString("scroll=+10  select=-10  power=save+exit", SCR_W - 8, 15);

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

    // Metrics rows
    char buf[48];
    const int MY = CAL_SPEC_Y + CAL_SPEC_H + 16;
    tft.fillRect(0, MY - 2, SCR_W, SCR_H - MY + 2, TFT_BLACK);
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

    const int R2Y = MY + 44;
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("PEAK FREQ", 8, R2Y);
    if (peak_hz > 0.0)
        snprintf(buf, sizeof(buf), "%.0f Hz  =  %.1f %s",
                 peak_hz, disp_speed((float)peak_hz * HZ_TO_KMH, use_mph),
                 speed_unit(use_mph));
    else
        snprintf(buf, sizeof(buf), "---");
    tft.setTextFont(2); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, 8, R2Y + 12);

    const int R3Y = R2Y + 34;
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("THRESHOLD", 8, R3Y);
    snprintf(buf, sizeof(buf), "%.0f", threshold);
    tft.setTextFont(7); tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(buf, 8, R3Y + 8);

    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("SUGGESTED", 200, R3Y);
    snprintf(buf, sizeof(buf), "%.0f  (noise x4)",
             max(noise_ema * 4.0f, noise_ema + 20.0f));
    tft.setTextFont(2); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(buf, 200, R3Y + 12);
}
