// OpenScope — DIY Golf Launch Monitor  v0.4
// ESP32 + CDM324 24 GHz Doppler Radar + ST7796 3.5" TFT
//
// Signal path:
//   CDM324 IF → LM358 preamp (×100, 300 Hz–18 kHz bandpass) → GPIO34 (ADC)
//
// Controls (GPIO0 / boot button):
//   Short  (<1 s)  → next club
//   Medium (1–3 s) → settings menu
//   Long   (>3 s)  → calibration mode
//
// Settings menu:
//   Short  → next item
//   Medium → activate / toggle
//   Long   → save + exit

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <arduinoFFT.h>
#include <Preferences.h>

// ─── Pins ─────────────────────────────────────────────────────────────────────

#define RADAR_ADC_PIN  34
#define BTN_PIN         0

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

// ─── Display ──────────────────────────────────────────────────────────────────

#define TFT_ROTATION  1
#define SCR_W  480
#define SCR_H  320
#define COL_W  (SCR_W / 3)   // 160 px per column
#define ROW_H  (SCR_H / 2)   // 160 px per row

// Custom RGB565 colors
#define COL_DIV      0x2945   // dark grey divider  ~(40, 40, 45)
#define COL_UNIT     0x7BCF   // medium grey units  ~(120, 120, 128)
#define COL_DIM      0x2965   // very dark for empty tiles
#define COL_CAL_HDR  0x5920   // dark burnt orange  ~(90, 36, 0)

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

// ─── Objects ──────────────────────────────────────────────────────────────────

static ArduinoFFT<double> FFT_obj(vReal, vImag, FFT_SIZE, (double)SAMPLE_RATE);
static TFT_eSPI           tft;
static Preferences        prefs;

// ─── Unit helpers ─────────────────────────────────────────────────────────────

static inline float       disp_speed(float kmh)    { return g_use_mph ? kmh * 0.621371f   : kmh; }
static inline float       disp_dist(float meters)  { return g_use_mph ? meters * 1.09361f : meters; }
static inline const char* speed_unit()              { return g_use_mph ? "mph" : "km/h"; }
static inline const char* dist_unit()               { return g_use_mph ? "yds" : "m"; }

// ─── NVS ──────────────────────────────────────────────────────────────────────

static void nvs_load()
{
    prefs.begin("openscope", false);
    g_threshold = prefs.getFloat("thresh", PEAK_THRESHOLD_DEFAULT);
    g_use_mph   = prefs.getBool("mph", false);
    for (int i = 0; i < NUM_CLUBS; i++) {
        char k[10];
        snprintf(k, sizeof(k), "c%d_n",  i); g_stats[i].count = prefs.getUInt(k,   0);
        snprintf(k, sizeof(k), "c%d_s",  i); g_stats[i].sum   = prefs.getFloat(k,  0.0f);
        snprintf(k, sizeof(k), "c%d_mx", i); g_stats[i].max_c = prefs.getFloat(k,  0.0f);
        snprintf(k, sizeof(k), "c%d_mn", i); g_stats[i].min_c = prefs.getFloat(k,  9999.0f);
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

// ─── UI — Tile grid helpers ────────────────────────────────────────────────────

static void draw_grid_lines()
{
    tft.drawFastVLine(COL_W,     0, SCR_H, COL_DIV);
    tft.drawFastVLine(COL_W * 2, 0, SCR_H, COL_DIV);
    tft.drawFastHLine(0, ROW_H,  SCR_W,    COL_DIV);
}

// Draws a data tile. number must be a pre-formatted string (digits only for font 7).
// Pass dimmed=true for empty/inactive tiles.
static void draw_tile(int col, int row,
                      const char* label, const char* number, const char* unit,
                      uint16_t num_col, bool dimmed = false)
{
    int cx = col * COL_W + COL_W / 2;
    int y0 = row * ROW_H;

    uint16_t lc = dimmed ? COL_DIM  : TFT_CYAN;
    uint16_t nc = dimmed ? COL_DIM  : num_col;
    uint16_t uc = dimmed ? COL_DIM  : COL_UNIT;

    tft.setTextDatum(TC_DATUM);

    // Label (font 4 = 26 px)
    tft.setTextFont(4); tft.setTextColor(lc, TFT_BLACK);
    tft.drawString(label, cx, y0 + 28);

    // Number (font 7 = 48 px 7-segment)
    tft.setTextFont(7); tft.setTextColor(nc, TFT_BLACK);
    tft.drawString(number, cx, y0 + 62);

    // Unit (font 2 = 16 px)
    tft.setTextFont(2); tft.setTextColor(uc, TFT_BLACK);
    tft.drawString(unit, cx, y0 + 118);
}

// Draws the club-selector tile: circle + abbreviation
static void draw_club_tile(int col, int row)
{
    int cx = col * COL_W + COL_W / 2;
    int cy = row * ROW_H + ROW_H / 2;

    tft.drawCircle(cx, cy, 42, TFT_CYAN);
    tft.drawCircle(cx, cy, 43, TFT_CYAN);   // thicker outline

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(CLUBS[g_club].abbr, cx, cy);
}

// ─── UI — Main screens ────────────────────────────────────────────────────────

static void ui_splash()
{
    tft.fillScreen(TFT_BLACK);
    draw_grid_lines();

    // Top row — dimmed (no reading yet)
    draw_tile(0, 0, "Club",  "--", speed_unit(), TFT_WHITE, true);
    draw_tile(1, 0, "Ball",  "--", speed_unit(), TFT_WHITE, true);
    draw_tile(2, 0, "Smash", "--", "Factor",     TFT_WHITE, true);

    // Bottom row — per-club carry stats + club circle
    const ClubStats& s = g_stats[g_club];
    char buf_avg[8], buf_best[8];
    if (s.count > 0) {
        snprintf(buf_avg,  sizeof(buf_avg),  "%.0f", disp_dist(s.sum / s.count));
        snprintf(buf_best, sizeof(buf_best), "%.0f", disp_dist(s.max_c));
    } else {
        snprintf(buf_avg,  sizeof(buf_avg),  "--");
        snprintf(buf_best, sizeof(buf_best), "--");
    }
    draw_tile(0, 1, "Avg",  buf_avg,  dist_unit(), TFT_WHITE);
    draw_tile(1, 1, "Best", buf_best, dist_unit(), TFT_GREEN);
    draw_club_tile(2, 1);

    // Subtle "swing when ready" at the divider line
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COL_DIM, TFT_BLACK);
    tft.drawString("SWING WHEN READY", SCR_W / 2, ROW_H + 4);
}

static void ui_result(float ball_kmh, float club_kmh, float smash,
                      float carry_m,  float total_m)
{
    tft.fillScreen(TFT_BLACK);
    draw_grid_lines();

    char buf[12];

    // Top row
    if (club_kmh > 0.0f) {
        snprintf(buf, sizeof(buf), "%.0f", disp_speed(club_kmh));
        draw_tile(0, 0, "Club", buf, speed_unit(), TFT_WHITE);
    } else {
        draw_tile(0, 0, "Club", "--", speed_unit(), TFT_WHITE, true);
    }

    snprintf(buf, sizeof(buf), "%.0f", disp_speed(ball_kmh));
    draw_tile(1, 0, "Ball", buf, speed_unit(), TFT_WHITE);

    if (smash > 0.0f) {
        snprintf(buf, sizeof(buf), "%.2f", smash);
        draw_tile(2, 0, "Smash", buf, "Factor", TFT_WHITE);
    } else {
        draw_tile(2, 0, "Smash", "--", "Factor", TFT_WHITE, true);
    }

    // Bottom row
    snprintf(buf, sizeof(buf), "%.0f", disp_dist(carry_m));
    draw_tile(0, 1, "Carry", buf, dist_unit(), TFT_WHITE);

    snprintf(buf, sizeof(buf), "%.0f", disp_dist(total_m));
    draw_tile(1, 1, "Total", buf, dist_unit(), TFT_WHITE);

    draw_club_tile(2, 1);
}

// ─── UI — Settings ────────────────────────────────────────────────────────────

static void ui_settings_draw(int selected, bool reset_confirmed = false)
{
    tft.fillScreen(TFT_BLACK);

    // Header
    tft.fillRect(0, 0, SCR_W, 48, TFT_NAVY);
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_CYAN, TFT_NAVY);
    tft.drawString("Settings", 16, 24);
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_NAVY);
    tft.setTextDatum(MR_DATUM);
    tft.drawString("short=next  medium=select  long=exit", SCR_W - 12, 24);

    // Menu items
    struct MenuItem { const char* label; const char* value; };
    char unit_val[8], reset_val[20];
    snprintf(unit_val,  sizeof(unit_val),  "%s", g_use_mph ? "mph" : "km/h");
    snprintf(reset_val, sizeof(reset_val), "%s", reset_confirmed ? "Done!" : CLUBS[g_club].name);

    const MenuItem items[] = {
        { "Units",       unit_val   },
        { "Reset Stats", reset_val  },
        { "Calibration", "\x10"     },  // → arrow (char 0x10 in font 2)
    };
    const int N = 3;

    for (int i = 0; i < N; i++) {
        int y = 72 + i * 72;
        bool sel = (i == selected);
        uint16_t row_col = sel ? TFT_WHITE : COL_UNIT;

        // Selection indicator
        if (sel) {
            tft.fillRect(0, y - 4, SCR_W, 52, 0x1082);  // subtle dark highlight
            tft.fillRect(0, y - 4, 4, 52, TFT_CYAN);    // left accent bar
        }

        tft.setTextDatum(ML_DATUM);
        tft.setTextFont(4); tft.setTextColor(row_col, sel ? 0x1082 : TFT_BLACK);
        tft.drawString(items[i].label, 20, y + 20);

        tft.setTextFont(4);
        tft.setTextDatum(MR_DATUM);
        uint16_t val_col = sel ? TFT_CYAN : COL_DIM;
        if (i == 1 && reset_confirmed) val_col = TFT_GREEN;
        tft.setTextColor(val_col, sel ? 0x1082 : TFT_BLACK);
        tft.drawString(items[i].value, SCR_W - 20, y + 20);
    }

    // Footer
    tft.drawFastHLine(0, SCR_H - 22, SCR_W, COL_DIV);
    tft.setTextDatum(BC_DATUM);
    tft.setTextFont(2); tft.setTextColor(COL_DIM, TFT_BLACK);
    tft.drawString("OpenScope v0.4", SCR_W / 2, SCR_H - 4);
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
    tft.drawString("+short  -long  save=3s", SCR_W - 8, 15);

    // X-axis labels
    const struct { const char* l; int hz; } ticks[] =
        { {"2k",2000},{"5k",5000},{"8k",8000},{"12k",12000} };
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(1);
    for (auto& t : ticks) {
        int xp = CAL_SPEC_X + (int)((float)(t.hz - MIN_DETECT_HZ) /
                               (MAX_DETECT_HZ - MIN_DETECT_HZ) * CAL_SPEC_W);
        tft.setTextColor(COL_DIM, TFT_BLACK);
        tft.drawString(t.l, xp, CAL_SPEC_Y + CAL_SPEC_H + 4);
    }
}

static void ui_cal_update(double peak_hz, double peak_mag,
                          float noise_ema, float max_seen)
{
    // Spectrum area
    tft.fillRect(CAL_SPEC_X, CAL_SPEC_Y, CAL_SPEC_W, CAL_SPEC_H, TFT_BLACK);

    int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    int bin_hi = min((int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE), FFT_SIZE/2-1);
    float scale = (max_seen > 1.0f) ? ((CAL_SPEC_H - 4) / max_seen) : 1.0f;
    float y_base = (float)(CAL_SPEC_Y + CAL_SPEC_H - 1);

    for (int x = 0; x < CAL_SPEC_W; x++) {
        int bin = bin_lo + (int)((float)x / CAL_SPEC_W * (bin_hi - bin_lo));
        if (bin > bin_hi) break;
        float mag = (float)vReal[bin];
        int   h   = min((int)(mag * scale), CAL_SPEC_H - 1);
        if (h <= 0) continue;
        uint16_t col = (mag >= g_threshold) ? TFT_RED : 0x0460;  // dark green
        tft.drawFastVLine(CAL_SPEC_X + x, (int)y_base - h, h, col);
    }

    // Threshold line
    int ty = CAL_SPEC_Y + CAL_SPEC_H - 1 - (int)(g_threshold * scale);
    if (ty >= CAL_SPEC_Y)
        tft.drawFastHLine(CAL_SPEC_X, ty, CAL_SPEC_W, TFT_YELLOW);

    // Peak marker
    if (peak_hz > 0.0) {
        int px = CAL_SPEC_X + (int)(((float)peak_hz - MIN_DETECT_HZ) /
                              (MAX_DETECT_HZ - MIN_DETECT_HZ) * CAL_SPEC_W);
        if (px >= CAL_SPEC_X && px < CAL_SPEC_X + CAL_SPEC_W)
            tft.drawFastVLine(px, CAL_SPEC_Y, 6, TFT_WHITE);
    }

    // Metrics
    char buf[32];
    int MY = CAL_SPEC_Y + CAL_SPEC_H + 16;
    tft.fillRect(0, MY - 2, SCR_W, SCR_H - MY + 2, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);

    // Row 1
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

    // Row 2
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

    // Row 3: threshold + suggested
    int R3Y = R2Y + 34;
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("THRESHOLD", 8, R3Y);
    snprintf(buf, sizeof(buf), "%.0f", g_threshold);
    tft.setTextFont(7); tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(buf, 8, R3Y + 8);

    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("SUGGESTED", 200, R3Y);
    float sug = max(noise_ema * 4.0f, noise_ema + 20.0f);
    snprintf(buf, sizeof(buf), "%.0f  (noise x4)", sug);
    tft.setTextFont(2); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(buf, 200, R3Y + 12);
}

// ─── Radar sampling ───────────────────────────────────────────────────────────

static void sample_radar()
{
    const uint32_t period_us = 1000000UL / SAMPLE_RATE;
    for (int i = 0; i < FFT_SIZE; i++) {
        uint32_t t0 = micros();
        vReal[i] = (double)(analogRead(RADAR_ADC_PIN) - 2048);
        vImag[i] = 0.0;
        while ((micros() - t0) < period_us) {}
    }
}

static void run_fft()
{
    FFT_obj.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT_obj.compute(FFTDirection::Forward);
    FFT_obj.complexToMagnitude();
}

// ─── Peak detection ───────────────────────────────────────────────────────────

struct Peak { int bin; double mag; double hz; };

static Peak find_best_peak(int bin_lo, int bin_hi, int excl_bin, int excl_r)
{
    Peak p = { -1, 0.0, 0.0 };
    for (int i = bin_lo; i <= bin_hi; i++) {
        if (excl_bin >= 0 && abs(i - excl_bin) < excl_r) continue;
        if (vReal[i] > p.mag) { p.mag = vReal[i]; p.bin = i; }
    }
    if (p.bin < 0 || p.mag < (double)g_threshold) return { -1, 0.0, 0.0 };
    double a = (p.bin > 0)            ? vReal[p.bin-1] : 0.0;
    double b = vReal[p.bin];
    double c = (p.bin < FFT_SIZE/2)   ? vReal[p.bin+1] : 0.0;
    double d = a - 2.0*b + c;
    p.hz = ((double)p.bin + (d != 0.0 ? 0.5*(a-c)/d : 0.0)) * SAMPLE_RATE / FFT_SIZE;
    return p;
}

static bool detect_speeds(double& ball_hz, double& club_hz)
{
    run_fft();
    int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    int bin_hi = min((int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE), FFT_SIZE/2-1);
    int sep    = max(5, (int)((double)MIN_PEAK_SEP_HZ / SAMPLE_RATE * FFT_SIZE));

    Peak p1 = find_best_peak(bin_lo, bin_hi, -1, 0);
    if (p1.bin < 0) return false;

    Peak p2 = find_best_peak(bin_lo, bin_hi, p1.bin, sep);

    Serial.printf("[FFT] p1 bin=%d mag=%.1f hz=%.0f", p1.bin, p1.mag, p1.hz);
    if (p2.bin >= 0) Serial.printf(" | p2 bin=%d mag=%.1f hz=%.0f", p2.bin, p2.mag, p2.hz);
    Serial.println();

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

// ─── Button ───────────────────────────────────────────────────────────────────

// 0=nothing  1=short(<1s)  2=medium(1–3s)  3=long(>3s)
static int read_button()
{
    if (digitalRead(BTN_PIN) != LOW) return 0;
    uint32_t t = millis();
    while (digitalRead(BTN_PIN) == LOW) {
        if (millis() - t > 3000) { while (digitalRead(BTN_PIN) == LOW) delay(10); return 3; }
        delay(10);
    }
    uint32_t held = millis() - t;
    if (held < 50)   return 0;
    if (held < 1000) return 1;
    return 2;
}

// ─── Settings loop ────────────────────────────────────────────────────────────

static void calibration_loop();   // forward declaration

static void settings_loop()
{
    int  item = 0;
    bool reset_confirmed = false;
    ui_settings_draw(item);

    while (true) {
        int btn = read_button();

        if (btn == 1) {
            item = (item + 1) % 3;
            reset_confirmed = false;
            ui_settings_draw(item);

        } else if (btn == 2) {
            if (item == 0) {
                g_use_mph = !g_use_mph;
                nvs_save_settings();
                Serial.printf("[SET] Units → %s\n", speed_unit());
                ui_settings_draw(item);

            } else if (item == 1) {
                reset_stats(g_club);
                reset_confirmed = true;
                ui_settings_draw(item, true);
                delay(800);
                reset_confirmed = false;
                ui_settings_draw(item);

            } else if (item == 2) {
                calibration_loop();
                ui_settings_draw(item);
            }

        } else if (btn == 3) {
            nvs_save_settings();
            Serial.println("[SET] Exiting settings");
            return;
        }
    }
}

// ─── Calibration loop ─────────────────────────────────────────────────────────

static void calibration_loop()
{
    Serial.println("[CAL] Entering calibration mode");
    ui_cal_header();

    float noise_ema = 0.0f;
    float max_seen  = 0.0f;
    bool  first     = true;

    int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    int bin_hi = min((int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE), FFT_SIZE/2-1);

    while (true) {
        int btn = read_button();
        if (btn == 1) {
            g_threshold = min(g_threshold + 10.0f, 2000.0f);
            Serial.printf("[CAL] Threshold → %.0f\n", g_threshold);
        } else if (btn == 2) {
            g_threshold = max(g_threshold - 10.0f, 5.0f);
            Serial.printf("[CAL] Threshold → %.0f\n", g_threshold);
        } else if (btn == 3) {
            nvs_save_settings();
            Serial.println("[CAL] Exiting, threshold saved");
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

        Serial.printf("[CAL] mag=%.1f noise=%.1f max=%.1f thresh=%.0f\n",
                      peak_mag, noise_ema, max_seen, g_threshold);
    }
}

// ─── Arduino entry points ─────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    Serial.println("\n[OpenScope] v0.4 booting");

    pinMode(BTN_PIN, INPUT_PULLUP);

    // ADC: 12-bit, 11 dB attenuation → ~0–3.1 V
    // On newer ESP-IDF: replace ADC_11db with ADC_ATTEN_DB_12 if it fails.
    analogReadResolution(12);
    analogSetPinAttenuation(RADAR_ADC_PIN, ADC_11db);

    nvs_load();

    tft.init();
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(TFT_BLACK);

    ui_splash();

    Serial.printf("[OpenScope] Units: %s  |  Threshold: %.1f\n", speed_unit(), g_threshold);
    Serial.printf("[OpenScope] Gate: %.0f–%.0f %s\n",
                  MIN_DETECT_HZ * HZ_TO_KMH, MAX_DETECT_HZ * HZ_TO_KMH, "km/h");
    Serial.println("[OpenScope] Ready.");
}

void loop()
{
    int btn = read_button();

    if (btn == 1) {
        g_club = (g_club + 1) % NUM_CLUBS;
        Serial.printf("[BTN] Club → %s\n", CLUBS[g_club].name);
        ui_splash();
        return;
    }
    if (btn == 2) {
        settings_loop();
        ui_splash();
        return;
    }
    if (btn == 3) {
        calibration_loop();
        ui_splash();
        return;
    }

    sample_radar();
    double ball_hz = 0.0, club_hz = 0.0;
    if (!detect_speeds(ball_hz, club_hz)) return;

    float ball_kmh = (float)(ball_hz * HZ_TO_KMH);
    float club_kmh = (club_hz > 0.0) ? (float)(club_hz * HZ_TO_KMH) : 0.0f;
    float smash    = (club_kmh > 0.0f) ? (ball_kmh / club_kmh) : 0.0f;
    float carry_m  = ball_kmh * CLUBS[g_club].carry_f;
    float total_m  = carry_m  * (1.0f + CLUBS[g_club].roll_f);

    Serial.printf("[HIT] Ball %.1f km/h | Club %.1f km/h | Smash %.2f | Carry %.0f m | Total %.0f m\n",
                  ball_kmh, club_kmh, smash, carry_m, total_m);

    record_carry(g_club, carry_m);
    ui_result(ball_kmh, club_kmh, smash, carry_m, total_m);

    // Hold result; remain responsive to button
    uint32_t t0 = millis();
    while (millis() - t0 < 6000) {
        int b = read_button();
        if (b == 1) { g_club = (g_club + 1) % NUM_CLUBS; break; }
        if (b == 2) { settings_loop(); break; }
        if (b == 3) { calibration_loop(); break; }
        delay(20);
    }

    ui_splash();
}
