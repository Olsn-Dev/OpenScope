// OpenScope — DIY Golf Launch Monitor  v0.2
// ESP32 + CDM324 24 GHz Doppler Radar + ST7796 3.5" TFT
//
// Signal path:
//   CDM324 IF → LM358 preamp (×100, 300 Hz – 18 kHz bandpass) → GPIO34 (ADC)
//
// Controls:
//   GPIO0 short press  (<1 s) → cycle club
//   GPIO0 long press   (>1 s) → reset stats for current club
//
// Physics:
//   f_d = 2 × v × f_c / c          (Doppler shift)
//   v [km/h] = f_d [Hz] × 0.022384  (CDM324, f_c = 24.125 GHz)
//   carry [m] = ball_speed [km/h] × per-club carry factor
//   total [m] = carry × (1 + per-club roll factor)
//   smash     = ball_speed / club_speed

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <arduinoFFT.h>
#include <Preferences.h>

// ─── Pins ─────────────────────────────────────────────────────────────────────

#define RADAR_ADC_PIN  34   // GPIO34: input-only, no pull, from LM358 output
#define BTN_PIN         0   // GPIO0:  boot button, active-LOW, has onboard pull-up

// ─── Sampling & FFT ───────────────────────────────────────────────────────────

#define SAMPLE_RATE  40000  // Hz — Nyquist covers 20 kHz (~447 km/h)
#define FFT_SIZE     1024   // bins → 39 Hz/bin ≈ 0.87 km/h resolution

// ─── Doppler physics (CDM324, f_c = 24.125 GHz) ───────────────────────────────

#define HZ_TO_KMH  0.022384f
#define HZ_TO_MPH  0.013912f

// ─── Detection gate ───────────────────────────────────────────────────────────
// Lower bound rejects static clutter & slow follow-through noise.
// Upper bound is above any realistic ball speed (~350 km/h).

#define MIN_DETECT_HZ   1800   // ~40 km/h
#define MAX_DETECT_HZ  16000   // ~358 km/h

// Raise if spurious triggers; lower if real shots are missed.
// Print peak_mag over Serial to calibrate.
#define PEAK_THRESHOLD  80.0

// Minimum Hz gap required between two peaks to treat them as club + ball.
// Below this, the two candidates are likely the same event (harmonics / noise).
#define MIN_PEAK_SEP_HZ  800

// Ball freq must be ≥ this ratio above club freq to form a valid smash pair.
#define SMASH_MIN_RATIO  1.15f

// ─── Display ──────────────────────────────────────────────────────────────────

#define TFT_ROTATION  1   // 1 = landscape, USB connector on right
#define SCR_W  480
#define SCR_H  320

// ─── Club definitions ─────────────────────────────────────────────────────────
// carry_factor: carry[m] = ball_speed[km/h] × carry_factor  (empirical)
// roll_factor:  roll [m] = carry[m]  × roll_factor
// Reference: driver at 150 km/h → ~200 m carry is well-established.

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
    float    sum;     // sum of carry distances (for avg)
    float    max_c;
    float    min_c;
};
static ClubStats g_stats[NUM_CLUBS];
static int       g_club = 0;   // currently selected club index

// ─── FFT buffers ──────────────────────────────────────────────────────────────

static double vReal[FFT_SIZE];
static double vImag[FFT_SIZE];

// ─── Objects ──────────────────────────────────────────────────────────────────

static ArduinoFFT<double> FFT_obj(vReal, vImag, FFT_SIZE, (double)SAMPLE_RATE);
static TFT_eSPI           tft;
static Preferences        prefs;

// ─── NVS helpers ──────────────────────────────────────────────────────────────

static void nvs_load()
{
    prefs.begin("openscope", false);
    for (int i = 0; i < NUM_CLUBS; i++) {
        char k[10];
        snprintf(k, sizeof(k), "c%d_n",  i); g_stats[i].count = prefs.getUInt(k,   0);
        snprintf(k, sizeof(k), "c%d_s",  i); g_stats[i].sum   = prefs.getFloat(k,  0.0f);
        snprintf(k, sizeof(k), "c%d_mx", i); g_stats[i].max_c = prefs.getFloat(k,  0.0f);
        snprintf(k, sizeof(k), "c%d_mn", i); g_stats[i].min_c = prefs.getFloat(k,  9999.0f);
    }
    prefs.end();
}

static void nvs_save(int i)
{
    prefs.begin("openscope", false);
    char k[10];
    snprintf(k, sizeof(k), "c%d_n",  i); prefs.putUInt(k,  g_stats[i].count);
    snprintf(k, sizeof(k), "c%d_s",  i); prefs.putFloat(k, g_stats[i].sum);
    snprintf(k, sizeof(k), "c%d_mx", i); prefs.putFloat(k, g_stats[i].max_c);
    snprintf(k, sizeof(k), "c%d_mn", i); prefs.putFloat(k, g_stats[i].min_c);
    prefs.end();
}

static void record_carry(int idx, float carry)
{
    ClubStats& s = g_stats[idx];
    s.count++;
    s.sum += carry;
    if (carry > s.max_c) s.max_c = carry;
    if (carry < s.min_c) s.min_c = carry;
    nvs_save(idx);
}

static void reset_stats(int idx)
{
    g_stats[idx] = { 0, 0.0f, 0.0f, 9999.0f };
    nvs_save(idx);
    Serial.printf("[NVS] Stats reset for %s\n", CLUBS[idx].name);
}

// ─── UI ───────────────────────────────────────────────────────────────────────

static void ui_splash()
{
    tft.fillScreen(TFT_BLACK);

    // Header bar
    tft.fillRect(0, 0, SCR_W, 45, TFT_NAVY);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_NAVY);
    tft.setTextSize(2);
    tft.drawString("OpenScope", 12, 22);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_GREEN, TFT_NAVY);
    tft.drawString(CLUBS[g_club].name, SCR_W - 12, 22);

    // Radar icon
    const int cx = SCR_W / 2, cy = 155;
    tft.drawCircle(cx, cy, 55, TFT_DARKGREY);
    tft.drawCircle(cx, cy, 36, TFT_DARKGREY);
    tft.drawCircle(cx, cy, 16, TFT_GREEN);
    tft.drawFastHLine(cx - 60, cy, 120, TFT_DARKGREY);
    tft.drawFastVLine(cx, cy - 60, 120, TFT_DARKGREY);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Swing when ready...", cx, 228);

    // Stats bar
    tft.drawFastHLine(10, 267, SCR_W - 20, TFT_DARKGREY);

    const ClubStats& s = g_stats[g_club];
    char buf[20];

    // AVG
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("AVG", 25, 273);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setTextSize(2);
    if (s.count > 0) snprintf(buf, sizeof(buf), "%.0f m", s.sum / s.count);
    else             snprintf(buf, sizeof(buf), "--- m");
    tft.drawString(buf, 25, 284);

    // MAX
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("MAX", 185, 273);
    tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setTextSize(2);
    if (s.count > 0) snprintf(buf, sizeof(buf), "%.0f m", s.max_c);
    else             snprintf(buf, sizeof(buf), "--- m");
    tft.drawString(buf, 185, 284);

    // MIN
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString("MIN", 340, 273);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.setTextSize(2);
    if (s.count > 0) snprintf(buf, sizeof(buf), "%.0f m", s.min_c);
    else             snprintf(buf, sizeof(buf), "--- m");
    tft.drawString(buf, 340, 284);

    // Shot count
    snprintf(buf, sizeof(buf), "%lu shots", (unsigned long)s.count);
    tft.setTextDatum(BR_DATUM);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
    tft.drawString(buf, SCR_W - 4, SCR_H - 1);
}

static void ui_result(float ball_kmh, float club_kmh, float smash,
                      float carry_m,  float total_m)
{
    tft.fillScreen(TFT_BLACK);

    // Header bar
    tft.fillRect(0, 0, SCR_W, 45, TFT_NAVY);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_NAVY); tft.setTextSize(2);
    tft.drawString(CLUBS[g_club].name, 12, 22);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_GREEN, TFT_NAVY);
    tft.drawString("HIT!", SCR_W - 12, 22);

    // ── Ball speed — large primary ───────────────────────────────
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

    // ── Club speed + Smash ───────────────────────────────────────
    tft.setTextDatum(TL_DATUM);
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

    // ── Carry + Total ────────────────────────────────────────────
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
    tft.drawString("OpenScope v0.2 | github.com/Olsn-Dev/OpenScope", SCR_W / 2, SCR_H - 1);
}

// ─── Radar sampling ───────────────────────────────────────────────────────────

static void sample_radar()
{
    // Busy-wait gives accurate sample spacing without DMA.
    // At 40 kHz each period = 25 µs; analogRead takes ~5-10 µs → ~15 µs headroom.
    const uint32_t period_us = 1000000UL / SAMPLE_RATE;
    for (int i = 0; i < FFT_SIZE; i++) {
        uint32_t t0 = micros();
        int raw   = analogRead(RADAR_ADC_PIN);
        vReal[i]  = (double)(raw - 2048);
        vImag[i]  = 0.0;
        while ((micros() - t0) < period_us) {}
    }
}

// ─── Peak detection ───────────────────────────────────────────────────────────

struct Peak { int bin; double mag; double hz; };

static Peak find_best_peak(int bin_lo, int bin_hi, int exclude_bin, int exclude_r)
{
    Peak p = { -1, 0.0, 0.0 };
    for (int i = bin_lo; i <= bin_hi; i++) {
        if (exclude_bin >= 0 && abs(i - exclude_bin) < exclude_r) continue;
        if (vReal[i] > p.mag) { p.mag = vReal[i]; p.bin = i; }
    }
    if (p.bin < 0 || p.mag < PEAK_THRESHOLD) return { -1, 0.0, 0.0 };

    // Parabolic interpolation for sub-bin accuracy
    double a = (p.bin > 0)            ? vReal[p.bin - 1] : 0.0;
    double b = vReal[p.bin];
    double c = (p.bin < FFT_SIZE / 2) ? vReal[p.bin + 1] : 0.0;
    double denom = a - 2.0 * b + c;
    double offset = (denom != 0.0) ? (0.5 * (a - c) / denom) : 0.0;
    p.hz = ((double)p.bin + offset) * SAMPLE_RATE / FFT_SIZE;
    return p;
}

// Runs FFT on vReal/vImag, then extracts ball_hz and (optionally) club_hz.
// Returns true if a valid ball hit was detected.
static bool detect_speeds(double& ball_hz, double& club_hz)
{
    FFT_obj.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT_obj.compute(FFTDirection::Forward);
    FFT_obj.complexToMagnitude();  // results in vReal[]

    int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    int bin_hi = (int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    bin_hi = min(bin_hi, FFT_SIZE / 2 - 1);

    int sep = max(5, (int)((double)MIN_PEAK_SEP_HZ / SAMPLE_RATE * FFT_SIZE));

    Peak p1 = find_best_peak(bin_lo, bin_hi, -1, 0);
    if (p1.bin < 0) return false;

    Peak p2 = find_best_peak(bin_lo, bin_hi, p1.bin, sep);

    Serial.printf("[FFT] p1 bin=%d mag=%.1f hz=%.0f", p1.bin, p1.mag, p1.hz);
    if (p2.bin >= 0) Serial.printf(" | p2 bin=%d mag=%.1f hz=%.0f", p2.bin, p2.mag, p2.hz);
    Serial.println();

    if (p2.bin >= 0) {
        // Sort ascending by frequency
        Peak lo = (p1.hz < p2.hz) ? p1 : p2;
        Peak hi = (p1.hz < p2.hz) ? p2 : p1;

        if (hi.hz >= lo.hz * SMASH_MIN_RATIO) {
            // Valid club (lo) + ball (hi) pair
            club_hz = lo.hz;
            ball_hz = hi.hz;
            return true;
        }
    }

    // Only one significant peak → treat as ball speed, club not detected
    ball_hz = p1.hz;
    club_hz = 0.0;
    return true;
}

// ─── Button ───────────────────────────────────────────────────────────────────

// Returns 0=nothing, 1=short press, 2=long press (>1 s)
static int read_button()
{
    if (digitalRead(BTN_PIN) != LOW) return 0;

    uint32_t t = millis();
    while (digitalRead(BTN_PIN) == LOW) {
        if (millis() - t > 1000) {
            while (digitalRead(BTN_PIN) == LOW) delay(10);  // wait for release
            return 2;
        }
        delay(10);
    }
    return (millis() - t > 50) ? 1 : 0;  // ignore glitches < 50 ms
}

static void handle_button(int evt)
{
    if (evt == 1) {
        g_club = (g_club + 1) % NUM_CLUBS;
        Serial.printf("[BTN] Club → %s\n", CLUBS[g_club].name);
        ui_splash();
    } else if (evt == 2) {
        reset_stats(g_club);
        ui_splash();
    }
}

// ─── Arduino entry points ─────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    Serial.println("\n[OpenScope] v0.2 booting");

    pinMode(BTN_PIN, INPUT_PULLUP);

    // ADC: 12-bit, 11 dB attenuation → ~0–3.1 V range
    // NOTE: on ESP-IDF 5.x / newer Arduino ESP32 core, ADC_11db may warn;
    //       replace with ADC_ATTEN_DB_12 if it fails to compile.
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
    Serial.printf("[OpenScope] Gate: %.0f – %.0f km/h\n",
                  MIN_DETECT_HZ * HZ_TO_KMH,
                  MAX_DETECT_HZ * HZ_TO_KMH);
    Serial.println("[OpenScope] Ready.");
}

void loop()
{
    // Service button (club change / stats reset) before sampling
    int btn = read_button();
    if (btn) { handle_button(btn); return; }

    // ~25.6 ms sample window
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

    // Hold result on screen; remain responsive to button presses
    uint32_t t0 = millis();
    while (millis() - t0 < 6000) {
        int b = read_button();
        if (b) { handle_button(b); return; }
        delay(20);
    }

    ui_splash();
}
