// OpenScope — DIY Golf Launch Monitor  v0.5
// ESP32 + CDM324 24 GHz Doppler Radar + ST7796 3.5" TFT
//
// Signal path:
//   CDM324 IF → LM358 preamp (×100, 300 Hz–18 kHz bandpass) → GPIO34 (ADC)
//
// Buttons:
//   GPIO25  SCROLL  — cycle clubs / navigate menus / threshold +10 in cal
//   GPIO26  SELECT  — confirm / open settings / threshold -10 in cal
//   GPIO27  POWER   — hold 2 s → deep sleep; press to wake (RTC GPIO)
//
// Screen flow:
//   Splash  → Scroll: next club | Select: settings | Power(2s): sleep
//   Result  → Scroll: next club | Select: back to splash | Power(2s): sleep
//   Settings→ Scroll: next item | Select: activate     | Power(2s): exit+save
//   Cal     → Scroll: +10       | Select: -10          | Power(2s): save+exit

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <arduinoFFT.h>
#include <Preferences.h>
#include "esp_sleep.h"

// ─── Pins ─────────────────────────────────────────────────────────────────────

#define RADAR_ADC_PIN    34   // Radar A (primary)
#define RADAR_ADC_PIN_B  35   // Radar B (secondary, 30 mm offset from A)
#define BTN_SCROLL       25   // Navigate / increase
#define BTN_SELECT     26   // Confirm / decrease
#define BTN_POWER      27   // Power (RTC GPIO17 — supports ext0 wake)

// ─── Sampling & FFT ───────────────────────────────────────────────────────────

#define SAMPLE_RATE  40000
#define FFT_SIZE     1024

// ─── Doppler physics (CDM324, f_c = 24.125 GHz) ───────────────────────────────

#define HZ_TO_KMH  0.022384f
#define HZ_TO_MPH  0.013912f

// ─── Detection ────────────────────────────────────────────────────────────────

#define MIN_DETECT_HZ           1800
#define MAX_DETECT_HZ          16000
#define PEAK_THRESHOLD_DEFAULT  80.0f
#define SMASH_MIN_RATIO         1.15f
#define MIN_PEAK_SEP_HZ          800
#define DUAL_AGREE_PCT          0.10f  // max allowed ball-Hz deviation between radars

// ─── Display ──────────────────────────────────────────────────────────────────

#define TFT_ROTATION  1
#define SCR_W  480
#define SCR_H  320
#define COL_W  (SCR_W / 3)
#define ROW_H  (SCR_H / 2)

#define COL_DIV      0x2945
#define COL_UNIT     0x7BCF
#define COL_DIM      0x2104
#define COL_CAL_HDR  0x5920
#define COL_SEL_BG   0x1082

// ─── Clubs ────────────────────────────────────────────────────────────────────

struct Club { const char* name; const char* abbr; float carry_f; float roll_f; };

static const Club CLUBS[] = {
    { "Driver", "D",   1.35f, 0.10f },
    { "3-Wood", "3W",  1.20f, 0.08f },
    { "5-Wood", "5W",  1.10f, 0.08f },
    { "3-Iron", "3I",  0.95f, 0.05f },
    { "4-Iron", "4I",  0.90f, 0.05f },
    { "5-Iron", "5I",  0.82f, 0.05f },
    { "6-Iron", "6I",  0.75f, 0.04f },
    { "7-Iron", "7I",  0.68f, 0.04f },
    { "8-Iron", "8I",  0.60f, 0.03f },
    { "9-Iron", "9I",  0.52f, 0.02f },
    { "PW",     "PW",  0.45f, 0.02f },
    { "GW",     "GW",  0.40f, 0.01f },
    { "SW",     "SW",  0.35f, 0.01f },
    { "LW",     "LW",  0.30f, 0.01f },
};
#define NUM_CLUBS  14

// ─── State ────────────────────────────────────────────────────────────────────

struct ClubStats { uint32_t count; float sum; float max_c; float min_c; };
static ClubStats g_stats[NUM_CLUBS];
static int       g_club      = 0;
static float     g_threshold = PEAK_THRESHOLD_DEFAULT;
static bool      g_use_mph   = false;

// ─── FFT buffers ──────────────────────────────────────────────────────────────

static double vReal[FFT_SIZE];
static double vImag[FFT_SIZE];
static double vRealB[FFT_SIZE];
static double vImagB[FFT_SIZE];

// ─── Objects ──────────────────────────────────────────────────────────────────

static ArduinoFFT<double> FFT_obj(vReal,  vImag,  FFT_SIZE, (double)SAMPLE_RATE);
static ArduinoFFT<double> FFT_obj_B(vRealB, vImagB, FFT_SIZE, (double)SAMPLE_RATE);
static TFT_eSPI           tft;
static Preferences        prefs;

// ─── Unit helpers ─────────────────────────────────────────────────────────────

static inline float       disp_speed(float kmh)   { return g_use_mph ? kmh * 0.621371f   : kmh; }
static inline float       disp_dist(float m)       { return g_use_mph ? m   * 1.09361f    : m; }
static inline const char* speed_unit()             { return g_use_mph ? "mph" : "km/h"; }
static inline const char* dist_unit()              { return g_use_mph ? "yds" : "m"; }

// ─── NVS ──────────────────────────────────────────────────────────────────────

static void nvs_load()
{
    prefs.begin("openscope", false);
    g_threshold = prefs.getFloat("thresh", PEAK_THRESHOLD_DEFAULT);
    g_use_mph   = prefs.getBool("mph", false);
    g_club      = (int)prefs.getUInt("club", 0);
    if (g_club < 0 || g_club >= NUM_CLUBS) g_club = 0;
    for (int i = 0; i < NUM_CLUBS; i++) {
        char k[10];
        snprintf(k, sizeof(k), "c%d_n",  i); g_stats[i].count = prefs.getUInt(k, 0);
        snprintf(k, sizeof(k), "c%d_s",  i); g_stats[i].sum   = prefs.getFloat(k, 0.0f);
        snprintf(k, sizeof(k), "c%d_mx", i); g_stats[i].max_c = prefs.getFloat(k, 0.0f);
        snprintf(k, sizeof(k), "c%d_mn", i); g_stats[i].min_c = prefs.getFloat(k, 9999.0f);
    }
    prefs.end();
}

static void nvs_save_stats(int i)
{
    prefs.begin("openscope", false);
    char k[10];
    snprintf(k, sizeof(k), "c%d_n",  i); prefs.putUInt(k,  g_stats[i].count);
    snprintf(k, sizeof(k), "c%d_s",  i); prefs.putFloat(k, g_stats[i].sum);
    snprintf(k, sizeof(k), "c%d_mx", i); prefs.putFloat(k, g_stats[i].max_c);
    snprintf(k, sizeof(k), "c%d_mn", i); prefs.putFloat(k, g_stats[i].min_c);
    prefs.end();
}

static void nvs_save_settings()
{
    prefs.begin("openscope", false);
    prefs.putFloat("thresh", g_threshold);
    prefs.putBool("mph",     g_use_mph);
    prefs.putUInt("club",    (uint32_t)g_club);
    prefs.end();
}

static void record_carry(int idx, float carry)
{
    ClubStats& s = g_stats[idx];
    s.count++;
    s.sum += carry;
    if (carry > s.max_c) s.max_c = carry;
    if (carry < s.min_c) s.min_c = carry;
    nvs_save_stats(idx);
}

static void reset_stats(int idx)
{
    g_stats[idx] = { 0, 0.0f, 0.0f, 9999.0f };
    nvs_save_stats(idx);
    Serial.printf("[NVS] Stats reset: %s\n", CLUBS[idx].name);
}

// ─── Buttons ──────────────────────────────────────────────────────────────────

static bool s_prev_scroll = false;
static bool s_prev_select = false;

// Returns true on a clean falling-edge press (debounced).
static bool scroll_pressed()
{
    bool cur = (digitalRead(BTN_SCROLL) == LOW);
    if (cur && !s_prev_scroll) {
        delay(25);
        cur = (digitalRead(BTN_SCROLL) == LOW);
    }
    bool edge = cur && !s_prev_scroll;
    s_prev_scroll = cur;
    return edge;
}

static bool select_pressed()
{
    bool cur = (digitalRead(BTN_SELECT) == LOW);
    if (cur && !s_prev_select) {
        delay(25);
        cur = (digitalRead(BTN_SELECT) == LOW);
    }
    bool edge = cur && !s_prev_select;
    s_prev_select = cur;
    return edge;
}

// Returns true if the power button is held for hold_ms.
// Blocks until released or timeout. Does NOT trigger on short taps.
static bool power_held(uint32_t hold_ms = 2000)
{
    if (digitalRead(BTN_POWER) != LOW) return false;
    uint32_t t = millis();
    while (digitalRead(BTN_POWER) == LOW) {
        if (millis() - t >= hold_ms) {
            while (digitalRead(BTN_POWER) == LOW) delay(10);
            return true;
        }
        delay(10);
    }
    return false;   // released before threshold — ignore
}

// ─── Power management ─────────────────────────────────────────────────────────

static void go_to_sleep()
{
    nvs_save_settings();
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(COL_DIM, TFT_BLACK);
    tft.drawString("Goodbye", SCR_W / 2, SCR_H / 2);
    delay(700);
    tft.fillScreen(TFT_BLACK);

    // Wake when BTN_POWER (GPIO27) goes LOW
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_27, 0);
    Serial.println("[PWR] Entering deep sleep");
    esp_deep_sleep_start();
}

// ─── UI — Tile grid ───────────────────────────────────────────────────────────

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
    int cx = col * COL_W + COL_W / 2;
    int y0 = row * ROW_H;
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

static void draw_club_tile(int col, int row)
{
    int cx = col * COL_W + COL_W / 2;
    int cy = row * ROW_H + ROW_H / 2;
    tft.drawCircle(cx, cy, 42, TFT_CYAN);
    tft.drawCircle(cx, cy, 43, TFT_CYAN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(CLUBS[g_club].abbr, cx, cy);
}

// ─── UI — Normal screens ──────────────────────────────────────────────────────

static void ui_splash()
{
    tft.fillScreen(TFT_BLACK);
    draw_grid_lines();

    draw_tile(0, 0, "Club",  "--", speed_unit(), TFT_WHITE, true);
    draw_tile(1, 0, "Ball",  "--", speed_unit(), TFT_WHITE, true);
    draw_tile(2, 0, "Smash", "--", "Factor",     TFT_WHITE, true);

    const ClubStats& s = g_stats[g_club];
    char avg[8], best[8];
    if (s.count > 0) {
        snprintf(avg,  sizeof(avg),  "%.0f", disp_dist(s.sum / s.count));
        snprintf(best, sizeof(best), "%.0f", disp_dist(s.max_c));
    } else {
        snprintf(avg,  sizeof(avg),  "--");
        snprintf(best, sizeof(best), "--");
    }
    draw_tile(0, 1, "Avg",  avg,  dist_unit(), TFT_WHITE);
    draw_tile(1, 1, "Best", best, dist_unit(), TFT_GREEN);
    draw_club_tile(2, 1);

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(2); tft.setTextColor(COL_DIM, TFT_BLACK);
    tft.drawString("SWING WHEN READY", SCR_W / 2, ROW_H + 4);
}

static void ui_result(float ball_kmh, float club_kmh, float smash,
                      float carry_m,  float total_m,  bool dual_ok)
{
    tft.fillScreen(TFT_BLACK);
    draw_grid_lines();

    char buf[12];

    if (club_kmh > 0.0f) {
        snprintf(buf, sizeof(buf), "%.0f", disp_speed(club_kmh));
        draw_tile(0, 0, "Club", buf, speed_unit(), TFT_WHITE);
    } else {
        draw_tile(0, 0, "Club", "--", speed_unit(), TFT_WHITE, true);
    }

    snprintf(buf, sizeof(buf), "%.0f", disp_speed(ball_kmh));
    uint16_t ball_col = dual_ok ? TFT_GREEN : TFT_WHITE;
    draw_tile(1, 0, "Ball", buf, speed_unit(), ball_col);

    if (smash > 0.0f) {
        snprintf(buf, sizeof(buf), "%.2f", smash);
        draw_tile(2, 0, "Smash", buf, "Factor", TFT_WHITE);
    } else {
        draw_tile(2, 0, "Smash", "--", "Factor", TFT_WHITE, true);
    }

    snprintf(buf, sizeof(buf), "%.0f", disp_dist(carry_m));
    draw_tile(0, 1, "Carry", buf, dist_unit(), TFT_WHITE);
    snprintf(buf, sizeof(buf), "%.0f", disp_dist(total_m));
    draw_tile(1, 1, "Total", buf, dist_unit(), TFT_WHITE);
    draw_club_tile(2, 1);

    tft.setTextDatum(BR_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(dual_ok ? TFT_GREEN : COL_DIM, TFT_BLACK);
    tft.drawString(dual_ok ? "DUAL OK" : "SINGLE", SCR_W - 4, SCR_H - 2);
}

// ─── UI — Settings ────────────────────────────────────────────────────────────

static void ui_settings_draw(int sel, bool reset_done = false)
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
    snprintf(unit_val,  sizeof(unit_val),  "%s", g_use_mph ? "mph" : "km/h");
    snprintf(reset_val, sizeof(reset_val), "%s", reset_done ? "Done!" : CLUBS[g_club].name);

    const char* labels[] = { "Units",       "Reset Stats",  "Calibration" };
    const char* values[] = { unit_val,       reset_val,      "\x10"        };

    for (int i = 0; i < 3; i++) {
        int y = 68 + i * 74;
        bool active = (i == sel);

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
    tft.drawString("OpenScope v0.5", SCR_W / 2, SCR_H - 4);
}

// ─── UI — Calibration ─────────────────────────────────────────────────────────

#define CAL_SPEC_Y   34
#define CAL_SPEC_H   150
#define CAL_SPEC_X   10
#define CAL_SPEC_W   (SCR_W - 20)

static void ui_cal_header()
{
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, SCR_W, 30, COL_CAL_HDR);
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2); tft.setTextColor(TFT_YELLOW, COL_CAL_HDR);
    tft.drawString("CALIBRATION MODE", 10, 15);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_WHITE, COL_CAL_HDR);
    tft.drawString("scroll=+10  select=-10  power=save+exit", SCR_W - 8, 15);

    tft.setTextFont(1); tft.setTextColor(COL_DIM, TFT_BLACK);
    const struct { const char* l; int hz; } ticks[] =
        { {"2k",2000},{"5k",5000},{"8k",8000},{"12k",12000} };
    tft.setTextDatum(TC_DATUM);
    for (auto& t : ticks) {
        int xp = CAL_SPEC_X + (int)((float)(t.hz - MIN_DETECT_HZ) /
                               (MAX_DETECT_HZ - MIN_DETECT_HZ) * CAL_SPEC_W);
        tft.drawString(t.l, xp, CAL_SPEC_Y + CAL_SPEC_H + 4);
    }
}

static void ui_cal_update(double peak_hz, double peak_mag,
                          float noise_ema, float max_seen)
{
    tft.fillRect(CAL_SPEC_X, CAL_SPEC_Y, CAL_SPEC_W, CAL_SPEC_H, TFT_BLACK);

    int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    int bin_hi = min((int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE), FFT_SIZE/2-1);
    float scale  = (max_seen > 1.0f) ? ((CAL_SPEC_H - 4) / max_seen) : 1.0f;
    float y_base = (float)(CAL_SPEC_Y + CAL_SPEC_H - 1);

    for (int x = 0; x < CAL_SPEC_W; x++) {
        int bin = bin_lo + (int)((float)x / CAL_SPEC_W * (bin_hi - bin_lo));
        if (bin > bin_hi) break;
        float mag = (float)vReal[bin];
        int   h   = min((int)(mag * scale), CAL_SPEC_H - 1);
        if (h <= 0) continue;
        tft.drawFastVLine(CAL_SPEC_X + x, (int)y_base - h, h,
                          (mag >= g_threshold) ? TFT_RED : (uint16_t)0x0460);
    }

    int ty = CAL_SPEC_Y + CAL_SPEC_H - 1 - (int)(g_threshold * scale);
    if (ty >= CAL_SPEC_Y)
        tft.drawFastHLine(CAL_SPEC_X, ty, CAL_SPEC_W, TFT_YELLOW);

    if (peak_hz > 0.0) {
        int px = CAL_SPEC_X + (int)(((float)peak_hz - MIN_DETECT_HZ) /
                              (MAX_DETECT_HZ - MIN_DETECT_HZ) * CAL_SPEC_W);
        if (px >= CAL_SPEC_X && px < CAL_SPEC_X + CAL_SPEC_W)
            tft.drawFastVLine(px, CAL_SPEC_Y, 6, TFT_WHITE);
    }

    char buf[40];
    int MY = CAL_SPEC_Y + CAL_SPEC_H + 16;
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
    tft.setTextColor((peak_mag >= g_threshold) ? TFT_RED : TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, 170, MY + 14);

    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("MAX SEEN", 332, MY);
    snprintf(buf, sizeof(buf), "%.1f", max_seen);
    tft.setTextFont(4); tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(buf, 332, MY + 14);

    int R2Y = MY + 44;
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("PEAK FREQ", 8, R2Y);
    if (peak_hz > 0.0)
        snprintf(buf, sizeof(buf), "%.0f Hz  =  %.1f %s",
                 peak_hz, disp_speed((float)peak_hz * HZ_TO_KMH), speed_unit());
    else
        snprintf(buf, sizeof(buf), "---");
    tft.setTextFont(2); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, 8, R2Y + 12);

    int R3Y = R2Y + 34;
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("THRESHOLD", 8, R3Y);
    snprintf(buf, sizeof(buf), "%.0f", g_threshold);
    tft.setTextFont(7); tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(buf, 8, R3Y + 8);

    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("SUGGESTED", 200, R3Y);
    snprintf(buf, sizeof(buf), "%.0f  (noise x4)", max(noise_ema * 4.0f, noise_ema + 20.0f));
    tft.setTextFont(2); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(buf, 200, R3Y + 12);
}

// ─── Radar ────────────────────────────────────────────────────────────────────

static void sample_radar()
{
    const uint32_t period_us = 1000000UL / SAMPLE_RATE;
    for (int i = 0; i < FFT_SIZE; i++) {
        uint32_t t0 = micros();
        vReal[i] = (double)(analogRead(RADAR_ADC_PIN) - 2048);
        vImag[i] = 0.0;
        while ((micros() - t0) < period_us) {}
    }
    for (int i = 0; i < FFT_SIZE; i++) {
        uint32_t t0 = micros();
        vRealB[i] = (double)(analogRead(RADAR_ADC_PIN_B) - 2048);
        vImagB[i] = 0.0;
        while ((micros() - t0) < period_us) {}
    }
}

static void run_fft()
{
    FFT_obj.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT_obj.compute(FFTDirection::Forward);
    FFT_obj.complexToMagnitude();
    FFT_obj_B.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT_obj_B.compute(FFTDirection::Forward);
    FFT_obj_B.complexToMagnitude();
}

struct Peak { int bin; double mag; double hz; };

static Peak find_best_peak(const double* buf, int bin_lo, int bin_hi, int excl_bin, int excl_r)
{
    Peak p = { -1, 0.0, 0.0 };
    for (int i = bin_lo; i <= bin_hi; i++) {
        if (excl_bin >= 0 && abs(i - excl_bin) < excl_r) continue;
        if (buf[i] > p.mag) { p.mag = buf[i]; p.bin = i; }
    }
    if (p.bin < 0 || p.mag < (double)g_threshold) return { -1, 0.0, 0.0 };
    double a = (p.bin > 0)          ? buf[p.bin-1] : 0.0;
    double b = buf[p.bin];
    double c = (p.bin < FFT_SIZE/2) ? buf[p.bin+1] : 0.0;
    double d = a - 2.0*b + c;
    p.hz = ((double)p.bin + (d != 0.0 ? 0.5*(a-c)/d : 0.0)) * SAMPLE_RATE / FFT_SIZE;
    return p;
}

// Extract ball_hz and club_hz from one FFT magnitude buffer.
static bool get_peaks_from_buf(const double* buf, double& ball_hz, double& club_hz)
{
    int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    int bin_hi = min((int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE), FFT_SIZE/2-1);
    int sep    = max(5, (int)((double)MIN_PEAK_SEP_HZ / SAMPLE_RATE * FFT_SIZE));

    Peak p1 = find_best_peak(buf, bin_lo, bin_hi, -1, 0);
    if (p1.bin < 0) return false;
    Peak p2 = find_best_peak(buf, bin_lo, bin_hi, p1.bin, sep);

    if (p2.bin >= 0) {
        Peak lo = (p1.hz < p2.hz) ? p1 : p2;
        Peak hi = (p1.hz < p2.hz) ? p2 : p1;
        if (hi.hz >= lo.hz * SMASH_MIN_RATIO) {
            club_hz = lo.hz; ball_hz = hi.hz; return true;
        }
    }
    ball_hz = p1.hz; club_hz = 0.0;
    return true;
}

// Dual-radar detection with cross-validation.
// confidence: 2 = both radars agreed (averaged), 1 = single radar only.
// Returns false if no hit or if radars disagree beyond DUAL_AGREE_PCT (false trigger).
static bool detect_speeds(double& ball_hz, double& club_hz, int& confidence)
{
    run_fft();

    double ball_a = 0.0, club_a = 0.0;
    double ball_b = 0.0, club_b = 0.0;
    bool hit_a = get_peaks_from_buf(vReal,  ball_a, club_a);
    bool hit_b = get_peaks_from_buf(vRealB, ball_b, club_b);

    if (!hit_a && !hit_b) return false;

    if (hit_a && hit_b) {
        float diff_pct = fabsf((float)(ball_a - ball_b)) / (float)ball_a;
        if (diff_pct > DUAL_AGREE_PCT) {
            Serial.printf("[DUAL] Mismatch A=%.0f B=%.0f (%.1f%%) — rejected\n",
                          ball_a, ball_b, diff_pct * 100.0f);
            return false;
        }
        ball_hz    = (ball_a + ball_b) * 0.5;
        if      (club_a > 0.0 && club_b > 0.0) club_hz = (club_a + club_b) * 0.5;
        else if (club_a > 0.0)                  club_hz = club_a;
        else if (club_b > 0.0)                  club_hz = club_b;
        else                                     club_hz = 0.0;
        confidence = 2;
        Serial.printf("[DUAL] A=%.0f B=%.0f avg=%.0f Hz\n", ball_a, ball_b, ball_hz);
    } else if (hit_a) {
        ball_hz = ball_a; club_hz = club_a; confidence = 1;
        Serial.printf("[DUAL] Single A=%.0f Hz\n", ball_hz);
    } else {
        ball_hz = ball_b; club_hz = club_b; confidence = 1;
        Serial.printf("[DUAL] Single B=%.0f Hz\n", ball_hz);
    }
    return true;
}

// ─── Calibration loop ─────────────────────────────────────────────────────────

static void calibration_loop()
{
    Serial.println("[CAL] Enter");
    ui_cal_header();

    float noise_ema = 0.0f, max_seen = 0.0f;
    bool  first = true;
    int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    int bin_hi = min((int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE), FFT_SIZE/2-1);

    while (true) {
        if (scroll_pressed()) {
            g_threshold = min(g_threshold + 10.0f, 2000.0f);
            Serial.printf("[CAL] Threshold → %.0f\n", g_threshold);
        }
        if (select_pressed()) {
            g_threshold = max(g_threshold - 10.0f, 5.0f);
            Serial.printf("[CAL] Threshold → %.0f\n", g_threshold);
        }
        if (power_held(2000)) {
            prefs.begin("openscope", false);
            prefs.putFloat("thresh", g_threshold);
            prefs.end();
            Serial.println("[CAL] Threshold saved, exit");
            return;
        }

        sample_radar();
        run_fft();

        double peak_mag = 0.0; int peak_bin = -1;
        for (int i = bin_lo; i <= bin_hi; i++)
            if (vReal[i] > peak_mag) { peak_mag = vReal[i]; peak_bin = i; }

        double peak_hz = (peak_bin >= 0) ? ((double)peak_bin * SAMPLE_RATE / FFT_SIZE) : 0.0;

        if (first) { noise_ema = (float)peak_mag; first = false; }
        else if (peak_mag < g_threshold)
            noise_ema = 0.92f * noise_ema + 0.08f * (float)peak_mag;

        if ((float)peak_mag > max_seen) max_seen = (float)peak_mag;
        ui_cal_update(peak_hz, peak_mag, noise_ema, max_seen);
    }
}

// ─── Settings loop ────────────────────────────────────────────────────────────

static void settings_loop()
{
    int item = 0;
    ui_settings_draw(item);

    while (true) {
        if (scroll_pressed()) {
            item = (item + 1) % 3;
            ui_settings_draw(item);
        }
        if (select_pressed()) {
            if (item == 0) {
                g_use_mph = !g_use_mph;
                Serial.printf("[SET] Units → %s\n", speed_unit());
                ui_settings_draw(item);
            } else if (item == 1) {
                reset_stats(g_club);
                ui_settings_draw(item, true);
                delay(800);
                ui_settings_draw(item, false);
            } else {
                calibration_loop();
                ui_settings_draw(item);
            }
        }
        if (power_held(2000)) {
            nvs_save_settings();
            Serial.println("[SET] Exit");
            return;
        }
    }
}

// ─── Arduino entry points ─────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    Serial.println("\n[OpenScope] v0.5 booting");

    pinMode(BTN_SCROLL, INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP);
    pinMode(BTN_POWER,  INPUT_PULLUP);

    // ADC: 12-bit, 11 dB attenuation → ~0–3.1 V
    // On newer ESP-IDF: replace ADC_11db with ADC_ATTEN_DB_12 if it fails.
    analogReadResolution(12);
    analogSetPinAttenuation(RADAR_ADC_PIN,   ADC_11db);
    analogSetPinAttenuation(RADAR_ADC_PIN_B, ADC_11db);

    nvs_load();

    tft.init();
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(TFT_BLACK);

    ui_splash();

    Serial.printf("[OpenScope] Club: %s  Units: %s  Threshold: %.1f\n",
                  CLUBS[g_club].name, speed_unit(), g_threshold);
    Serial.println("[OpenScope] Ready.");
}

void loop()
{
    // ── Button handling ──────────────────────────────────────────────────────
    if (scroll_pressed()) {
        g_club = (g_club + 1) % NUM_CLUBS;
        nvs_save_settings();
        Serial.printf("[BTN] Club → %s\n", CLUBS[g_club].name);
        ui_splash();
        return;
    }
    if (select_pressed()) {
        settings_loop();
        ui_splash();
        return;
    }
    if (power_held(2000)) {
        go_to_sleep();
        return;   // never reached (deep sleep restarts from setup)
    }

    // ── Radar detection ──────────────────────────────────────────────────────
    sample_radar();
    double ball_hz = 0.0, club_hz = 0.0; int confidence = 0;
    if (!detect_speeds(ball_hz, club_hz, confidence)) return;

    float ball_kmh = (float)(ball_hz * HZ_TO_KMH);
    float club_kmh = (club_hz > 0.0) ? (float)(club_hz * HZ_TO_KMH) : 0.0f;
    float smash    = (club_kmh > 0.0f) ? (ball_kmh / club_kmh) : 0.0f;
    float carry_m  = ball_kmh * CLUBS[g_club].carry_f;
    float total_m  = carry_m  * (1.0f + CLUBS[g_club].roll_f);
    bool  dual_ok  = (confidence == 2);

    Serial.printf("[HIT] Ball %.1f km/h | Club %.1f km/h | Smash %.2f | Carry %.0f m | Total %.0f m | %s\n",
                  ball_kmh, club_kmh, smash, carry_m, total_m, dual_ok ? "DUAL" : "SINGLE");

    record_carry(g_club, carry_m);
    ui_result(ball_kmh, club_kmh, smash, carry_m, total_m, dual_ok);

    // ── Hold result, remain responsive ───────────────────────────────────────
    uint32_t t0 = millis();
    while (millis() - t0 < 6000) {
        if (scroll_pressed()) {
            g_club = (g_club + 1) % NUM_CLUBS;
            nvs_save_settings();
            break;
        }
        if (select_pressed()) break;   // dismiss result early
        if (power_held(2000)) go_to_sleep();
        delay(20);
    }

    ui_splash();
}
