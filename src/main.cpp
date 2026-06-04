// OpenScope — DIY Golf Launch Monitor  v0.3
// ESP32 + CDM324 24 GHz Doppler Radar + ST7796 3.5" TFT
//
// Signal path:
//   CDM324 IF → LM358 preamp (×100, 300 Hz – 18 kHz bandpass) → GPIO34 (ADC)
//
// Controls (GPIO0 / boot button):
//   Short press  (<1 s)   → [normal] next club  |  [cal] threshold +10
//   Medium press (1–3 s)  → [normal] reset stats |  [cal] threshold -10
//   Long press   (>3 s)   → [normal] enter calibration mode
//                           [cal]    save threshold + exit
//
// Physics:
//   f_d = 2 × v × f_c / c
//   v [km/h] = f_d [Hz] × 0.022384   (CDM324, f_c = 24.125 GHz)
//   carry [m] = ball_speed [km/h] × per-club carry factor  (empirical)
//   smash     = ball_speed / club_speed

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <arduinoFFT.h>
#include <Preferences.h>

// ─── Pins ─────────────────────────────────────────────────────────────────────

#define RADAR_ADC_PIN  34   // GPIO34: input-only, from LM358 output
#define BTN_PIN         0   // GPIO0:  boot button, active-LOW

// ─── Sampling & FFT ───────────────────────────────────────────────────────────

#define SAMPLE_RATE  40000  // Hz — Nyquist covers 20 kHz (~447 km/h)
#define FFT_SIZE     1024

// ─── Doppler physics (CDM324, f_c = 24.125 GHz) ───────────────────────────────

#define HZ_TO_KMH  0.022384f
#define HZ_TO_MPH  0.013912f

// ─── Detection gate ───────────────────────────────────────────────────────────

#define MIN_DETECT_HZ   1800   // ~40 km/h — rejects static clutter
#define MAX_DETECT_HZ  16000   // ~358 km/h

// Default threshold — overridden at runtime by NVS value (calibrated value).
// Raise if spurious triggers; lower if real shots are missed.
#define PEAK_THRESHOLD_DEFAULT  80.0f

// Ball freq must be ≥ this ratio above club freq to count as a smash pair.
#define SMASH_MIN_RATIO  1.15f
// Min Hz gap between two peaks to treat as separate club/ball events.
#define MIN_PEAK_SEP_HZ  800

// ─── Display ──────────────────────────────────────────────────────────────────

#define TFT_ROTATION  1
#define SCR_W  480
#define SCR_H  320

// ─── Club definitions ─────────────────────────────────────────────────────────
// carry_factor: carry[m] = ball_speed[km/h] × carry_factor  (empirical)
// roll_factor:  roll [m] = carry[m] × roll_factor → total = carry + roll

struct Club {
    const char* name;
    float       carry_factor;
    float       roll_factor;
};

static const Club CLUBS[] = {
    { "Driver",  1.35f, 0.10f },
    { "3-Wood",  1.20f, 0.08f },
    { "5-Wood",  1.10f, 0.08f },
    { "3-Iron",  0.95f, 0.05f },
    { "4-Iron",  0.90f, 0.05f },
    { "5-Iron",  0.82f, 0.05f },
    { "6-Iron",  0.75f, 0.04f },
    { "7-Iron",  0.68f, 0.04f },
    { "8-Iron",  0.60f, 0.03f },
    { "9-Iron",  0.52f, 0.02f },
    { "PW",      0.45f, 0.02f },
    { "GW",      0.40f, 0.01f },
    { "SW",      0.35f, 0.01f },
    { "LW",      0.30f, 0.01f },
};
#define NUM_CLUBS  14

// ─── Per-club statistics (RAM + NVS) ──────────────────────────────────────────

struct ClubStats {
    uint32_t count;
    float    sum;
    float    max_c;
    float    min_c;
};
static ClubStats g_stats[NUM_CLUBS];
static int       g_club     = 0;
static float     g_threshold = PEAK_THRESHOLD_DEFAULT;

// ─── FFT buffers ──────────────────────────────────────────────────────────────

static double vReal[FFT_SIZE];
static double vImag[FFT_SIZE];

// ─── Objects ──────────────────────────────────────────────────────────────────

static ArduinoFFT<double> FFT_obj(vReal, vImag, FFT_SIZE, (double)SAMPLE_RATE);
static TFT_eSPI           tft;
static Preferences        prefs;

// ─── NVS ──────────────────────────────────────────────────────────────────────

static void nvs_load()
{
    prefs.begin("openscope", false);
    g_threshold = prefs.getFloat("thresh", PEAK_THRESHOLD_DEFAULT);
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

static void nvs_save_threshold()
{
    prefs.begin("openscope", false);
    prefs.putFloat("thresh", g_threshold);
    prefs.end();
    Serial.printf("[NVS] Threshold saved: %.1f\n", g_threshold);
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

// ─── UI — Normal mode ─────────────────────────────────────────────────────────

static void ui_splash()
{
    tft.fillScreen(TFT_BLACK);

    tft.fillRect(0, 0, SCR_W, 45, TFT_NAVY);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_NAVY); tft.setTextSize(2);
    tft.drawString("OpenScope", 12, 22);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_GREEN, TFT_NAVY);
    tft.drawString(CLUBS[g_club].name, SCR_W - 12, 22);

    const int cx = SCR_W / 2, cy = 155;
    tft.drawCircle(cx, cy, 55, TFT_DARKGREY);
    tft.drawCircle(cx, cy, 36, TFT_DARKGREY);
    tft.drawCircle(cx, cy, 16, TFT_GREEN);
    tft.drawFastHLine(cx - 60, cy, 120, TFT_DARKGREY);
    tft.drawFastVLine(cx, cy - 60, 120, TFT_DARKGREY);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setTextSize(2);
    tft.drawString("Swing when ready...", cx, 228);

    tft.drawFastHLine(10, 267, SCR_W - 20, TFT_DARKGREY);
    const ClubStats& s = g_stats[g_club];
    char buf[20];

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("AVG", 25, 273);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setTextSize(2);
    if (s.count > 0) snprintf(buf, sizeof(buf), "%.0f m", s.sum / s.count);
    else             snprintf(buf, sizeof(buf), "--- m");
    tft.drawString(buf, 25, 284);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("MAX", 185, 273);
    tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setTextSize(2);
    if (s.count > 0) snprintf(buf, sizeof(buf), "%.0f m", s.max_c);
    else             snprintf(buf, sizeof(buf), "--- m");
    tft.drawString(buf, 185, 284);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("MIN", 340, 273);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.setTextSize(2);
    if (s.count > 0) snprintf(buf, sizeof(buf), "%.0f m", s.min_c);
    else             snprintf(buf, sizeof(buf), "--- m");
    tft.drawString(buf, 340, 284);

    snprintf(buf, sizeof(buf), "%lu shots", (unsigned long)s.count);
    tft.setTextDatum(BR_DATUM);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString(buf, SCR_W - 4, SCR_H - 1);
}

static void ui_result(float ball_kmh, float club_kmh, float smash,
                      float carry_m,  float total_m)
{
    tft.fillScreen(TFT_BLACK);

    tft.fillRect(0, 0, SCR_W, 45, TFT_NAVY);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_NAVY); tft.setTextSize(2);
    tft.drawString(CLUBS[g_club].name, 12, 22);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_GREEN, TFT_NAVY);
    tft.drawString("HIT!", SCR_W - 12, 22);

    char buf[24];
    snprintf(buf, sizeof(buf), "%.0f", ball_kmh);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setTextSize(7);
    tft.drawString(buf, 295, 52);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.setTextSize(2);
    tft.drawString("km/h", 303, 58);
    snprintf(buf, sizeof(buf), "%.0f mph", ball_kmh * 0.621371f);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString(buf, 303, 98);

    tft.drawFastHLine(10, 138, SCR_W - 20, TFT_DARKGREY);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("CLUB SPEED", 12, 146);
    if (club_kmh > 0.0f) {
        snprintf(buf, sizeof(buf), "%.0f km/h", club_kmh);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    } else {
        snprintf(buf, sizeof(buf), "---");
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    }
    tft.setTextSize(3); tft.drawString(buf, 12, 158);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("SMASH", 300, 146);
    if (smash > 0.0f) {
        snprintf(buf, sizeof(buf), "%.2f", smash);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
        snprintf(buf, sizeof(buf), "---");
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    }
    tft.setTextSize(3); tft.drawString(buf, 300, 158);

    tft.drawFastHLine(10, 203, SCR_W - 20, TFT_DARKGREY);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("CARRY", 12, 211);
    snprintf(buf, sizeof(buf), "%.0f m", carry_m);
    tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setTextSize(3);
    tft.drawString(buf, 12, 223);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("TOTAL", 300, 211);
    snprintf(buf, sizeof(buf), "%.0f m", total_m);
    tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.setTextSize(3);
    tft.drawString(buf, 300, 223);

    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("OpenScope v0.3 | github.com/Olsn-Dev/OpenScope", SCR_W / 2, SCR_H - 1);
}

// ─── UI — Calibration mode ────────────────────────────────────────────────────

// Spectrum area constants
#define CAL_SPEC_Y   46    // top of spectrum area
#define CAL_SPEC_H   160   // height of spectrum area
#define CAL_SPEC_X   10    // left margin
#define CAL_SPEC_W   (SCR_W - 20)

static void ui_cal_header()
{
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, SCR_W, 45, 0x7800);  // dark orange header
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_YELLOW, 0x7800); tft.setTextSize(2);
    tft.drawString("CALIBRATION MODE", 12, 22);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_WHITE, 0x7800); tft.setTextSize(1);
    tft.drawString("+short / -long / save=3s", SCR_W - 8, 22);

    // X-axis labels (frequency)
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("2k", CAL_SPEC_X + (int)((2000 - MIN_DETECT_HZ) / (float)(MAX_DETECT_HZ - MIN_DETECT_HZ) * CAL_SPEC_W), CAL_SPEC_Y + CAL_SPEC_H + 2);
    tft.drawString("5k", CAL_SPEC_X + (int)((5000 - MIN_DETECT_HZ) / (float)(MAX_DETECT_HZ - MIN_DETECT_HZ) * CAL_SPEC_W), CAL_SPEC_Y + CAL_SPEC_H + 2);
    tft.drawString("8k", CAL_SPEC_X + (int)((8000 - MIN_DETECT_HZ) / (float)(MAX_DETECT_HZ - MIN_DETECT_HZ) * CAL_SPEC_W), CAL_SPEC_Y + CAL_SPEC_H + 2);
    tft.drawString("12k",CAL_SPEC_X + (int)((12000 - MIN_DETECT_HZ) / (float)(MAX_DETECT_HZ - MIN_DETECT_HZ) * CAL_SPEC_W), CAL_SPEC_Y + CAL_SPEC_H + 2);
    tft.drawString("Hz", CAL_SPEC_X + CAL_SPEC_W, CAL_SPEC_Y + CAL_SPEC_H + 2);
}

// Redraws only the dynamic parts (spectrum + metrics). Called every FFT frame.
static void ui_cal_update(double peak_hz, double peak_mag,
                          float noise_ema, float max_seen)
{
    // ── Spectrum ───────────────────────────────────────────────
    tft.fillRect(CAL_SPEC_X, CAL_SPEC_Y, CAL_SPEC_W, CAL_SPEC_H, TFT_BLACK);

    int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    int bin_hi = (int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    bin_hi = min(bin_hi, FFT_SIZE / 2 - 1);
    int   bin_range = bin_hi - bin_lo;
    float y_base    = (float)(CAL_SPEC_Y + CAL_SPEC_H - 1);

    // Auto-scale: max visible bar = CAL_SPEC_H-5 px
    float scale = (max_seen > 1.0f) ? ((CAL_SPEC_H - 5) / max_seen) : 1.0f;

    for (int x = 0; x < CAL_SPEC_W; x++) {
        int bin = bin_lo + (int)((float)x / CAL_SPEC_W * bin_range);
        if (bin > bin_hi) break;
        float mag = (float)vReal[bin];
        int   h   = min((int)(mag * scale), CAL_SPEC_H - 1);
        if (h <= 0) continue;
        uint16_t col = (mag >= g_threshold) ? TFT_RED : TFT_GREEN;
        tft.drawFastVLine(CAL_SPEC_X + x, (int)y_base - h, h, col);
    }

    // Threshold line (yellow)
    int thresh_y = (int)y_base - (int)(g_threshold * scale);
    if (thresh_y >= CAL_SPEC_Y) {
        tft.drawFastHLine(CAL_SPEC_X, thresh_y, CAL_SPEC_W, TFT_YELLOW);
    }

    // Peak marker (white tick at top of spectrum)
    if (peak_hz > 0.0) {
        int px = CAL_SPEC_X + (int)(((float)peak_hz - MIN_DETECT_HZ) / (MAX_DETECT_HZ - MIN_DETECT_HZ) * CAL_SPEC_W);
        if (px >= CAL_SPEC_X && px < CAL_SPEC_X + CAL_SPEC_W) {
            tft.drawFastVLine(px, CAL_SPEC_Y, 6, TFT_WHITE);
        }
    }

    // ── Metrics area ───────────────────────────────────────────
    int my = CAL_SPEC_Y + CAL_SPEC_H + 14;  // top of metrics area
    tft.fillRect(0, my - 2, SCR_W, SCR_H - my + 2, TFT_BLACK);

    char buf[32];
    tft.setTextDatum(TL_DATUM);

    // Row 1: Noise floor | Peak mag | Max seen
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("NOISE FLOOR", 10, my);
    tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.setTextSize(2);
    snprintf(buf, sizeof(buf), "%.1f", noise_ema);
    tft.drawString(buf, 10, my + 10);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("PEAK MAG", 175, my);
    tft.setTextColor((peak_mag >= g_threshold) ? TFT_RED : TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    snprintf(buf, sizeof(buf), "%.1f", peak_mag);
    tft.drawString(buf, 175, my + 10);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("MAX SEEN", 340, my);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.setTextSize(2);
    snprintf(buf, sizeof(buf), "%.1f", max_seen);
    tft.drawString(buf, 340, my + 10);

    // Row 2: Peak freq + speed
    int my2 = my + 32;
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("PEAK FREQ", 10, my2);
    if (peak_hz > 0.0) {
        snprintf(buf, sizeof(buf), "%.0f Hz = %.1f km/h", peak_hz, (float)peak_hz * HZ_TO_KMH);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
        snprintf(buf, sizeof(buf), "---");
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    }
    tft.setTextSize(2); tft.drawString(buf, 10, my2 + 10);

    // Row 3: Threshold (large) + suggested
    int my3 = my2 + 32;
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("THRESHOLD", 10, my3);
    snprintf(buf, sizeof(buf), "%.0f", g_threshold);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.setTextSize(3);
    tft.drawString(buf, 10, my3 + 10);

    // Suggested threshold = noise_ema × 4, clamped to at least noise + 20
    float suggested = max(noise_ema * 4.0f, noise_ema + 20.0f);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("SUGGESTED", 200, my3);
    snprintf(buf, sizeof(buf), "%.0f  (noise x4)", suggested);
    tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.setTextSize(2);
    tft.drawString(buf, 200, my3 + 10);
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

    double a = (p.bin > 0)            ? vReal[p.bin - 1] : 0.0;
    double b = vReal[p.bin];
    double c = (p.bin < FFT_SIZE / 2) ? vReal[p.bin + 1] : 0.0;
    double denom = a - 2.0 * b + c;
    p.hz = ((double)p.bin + ((denom != 0.0) ? (0.5 * (a - c) / denom) : 0.0))
           * SAMPLE_RATE / FFT_SIZE;
    return p;
}

static void run_fft()
{
    FFT_obj.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT_obj.compute(FFTDirection::Forward);
    FFT_obj.complexToMagnitude();
}

static bool detect_speeds(double& ball_hz, double& club_hz)
{
    run_fft();

    int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    int bin_hi = min((int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE), FFT_SIZE / 2 - 1);
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
            club_hz = lo.hz;
            ball_hz = hi.hz;
            return true;
        }
    }

    ball_hz = p1.hz;
    club_hz = 0.0;
    return true;
}

// ─── Button ───────────────────────────────────────────────────────────────────

// 0 = nothing, 1 = short (<1 s), 2 = medium (1–3 s), 3 = long (>3 s)
static int read_button()
{
    if (digitalRead(BTN_PIN) != LOW) return 0;
    uint32_t t = millis();
    while (digitalRead(BTN_PIN) == LOW) {
        uint32_t held = millis() - t;
        if (held > 3000) { while (digitalRead(BTN_PIN) == LOW) delay(10); return 3; }
        delay(10);
    }
    uint32_t held = millis() - t;
    if (held < 50)   return 0;
    if (held < 1000) return 1;
    return 2;
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
    int bin_hi = min((int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE), FFT_SIZE / 2 - 1);

    while (true) {
        // Button handling
        int btn = read_button();
        if (btn == 1) {
            g_threshold = min(g_threshold + 10.0f, 2000.0f);
            Serial.printf("[CAL] Threshold → %.0f\n", g_threshold);
        } else if (btn == 2) {
            g_threshold = max(g_threshold - 10.0f, 5.0f);
            Serial.printf("[CAL] Threshold → %.0f\n", g_threshold);
        } else if (btn == 3) {
            nvs_save_threshold();
            Serial.println("[CAL] Exiting calibration mode");
            return;
        }

        // Sample + FFT (without hit gating — we want to see everything)
        sample_radar();
        run_fft();

        // Find raw peak in detection window (ignoring threshold)
        double peak_mag = 0.0;
        int    peak_bin = -1;
        for (int i = bin_lo; i <= bin_hi; i++) {
            if (vReal[i] > peak_mag) { peak_mag = vReal[i]; peak_bin = i; }
        }

        double peak_hz = (peak_bin >= 0)
            ? ((double)peak_bin * SAMPLE_RATE / FFT_SIZE)
            : 0.0;

        // Update noise EMA (only when clearly below threshold — no swing)
        if (first) {
            noise_ema = (float)peak_mag;
            first = false;
        } else if (peak_mag < g_threshold) {
            noise_ema = 0.92f * noise_ema + 0.08f * (float)peak_mag;
        }

        if ((float)peak_mag > max_seen) max_seen = (float)peak_mag;

        ui_cal_update(peak_hz, peak_mag, noise_ema, max_seen);

        Serial.printf("[CAL] peak_mag=%.1f  noise_ema=%.1f  max=%.1f  thresh=%.0f\n",
                      peak_mag, noise_ema, max_seen, g_threshold);
    }
}

// ─── Arduino entry points ─────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    Serial.println("\n[OpenScope] v0.3 booting");

    pinMode(BTN_PIN, INPUT_PULLUP);

    // 12-bit ADC, 11 dB attenuation → ~0–3.1 V range.
    // If ADC_11db fails on newer ESP-IDF: replace with ADC_ATTEN_DB_12.
    analogReadResolution(12);
    analogSetPinAttenuation(RADAR_ADC_PIN, ADC_11db);

    nvs_load();

    tft.init();
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(TFT_BLACK);

    ui_splash();

    Serial.printf("[OpenScope] %.1f Hz/bin = %.3f km/h/bin\n",
                  (float)SAMPLE_RATE / FFT_SIZE,
                  (float)SAMPLE_RATE / FFT_SIZE * HZ_TO_KMH);
    Serial.printf("[OpenScope] Gate: %.0f–%.0f km/h  |  Threshold: %.1f\n",
                  MIN_DETECT_HZ * HZ_TO_KMH, MAX_DETECT_HZ * HZ_TO_KMH, g_threshold);
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
        reset_stats(g_club);
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
    float carry_m  = ball_kmh * CLUBS[g_club].carry_factor;
    float total_m  = carry_m  * (1.0f + CLUBS[g_club].roll_factor);

    Serial.printf("[HIT] Ball %.1f km/h | Club %.1f km/h | Smash %.2f | Carry %.0f m | Total %.0f m\n",
                  ball_kmh, club_kmh, smash, carry_m, total_m);

    record_carry(g_club, carry_m);
    ui_result(ball_kmh, club_kmh, smash, carry_m, total_m);

    uint32_t t0 = millis();
    while (millis() - t0 < 6000) {
        int b = read_button();
        if (b == 1) { g_club = (g_club + 1) % NUM_CLUBS; break; }
        if (b == 2) { reset_stats(g_club); break; }
        if (b == 3) { calibration_loop(); break; }
        delay(20);
    }

    ui_splash();
}
