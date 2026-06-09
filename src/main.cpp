// OpenScope — DIY Golf Launch Monitor  v0.6
// ESP32 + 2× CDM324 24 GHz Doppler Radar + ST7796 3.5" TFT
//
// Source layout:
//   config.h   — all compile-time constants and pin definitions
//   clubs.h/cpp— Club/ClubStats types and the CLUBS[] table
//   radar.h/cpp— ADC sampling, FFT, peak detection, launch angle solver
//   display.h/cpp — all TFT drawing (owns TFT_eSPI object)
//   storage.h/cpp — NVS persistence (owns Preferences object)
//   main.cpp   — global state, buttons, sleep, main loops

#include <Arduino.h>
#include "esp_sleep.h"
#include "config.h"
#include "clubs.h"
#include "radar.h"
#include "display.h"
#include "storage.h"

// ─── Global state ─────────────────────────────────────────────────────────────

static ClubStats g_stats[NUM_CLUBS];
static int       g_club      = 0;
static float     g_threshold = PEAK_THRESHOLD_DEFAULT;
static bool      g_use_mph   = false;

// ─── Buttons ──────────────────────────────────────────────────────────────────

static bool s_prev_scroll = false;
static bool s_prev_select = false;

// Returns true on a clean falling-edge press (25 ms debounce).
static bool scroll_pressed()
{
    bool cur = (digitalRead(BTN_SCROLL) == LOW);
    if (cur && !s_prev_scroll) { delay(25); cur = (digitalRead(BTN_SCROLL) == LOW); }
    bool edge = cur && !s_prev_scroll;
    s_prev_scroll = cur;
    return edge;
}

static bool select_pressed()
{
    bool cur = (digitalRead(BTN_SELECT) == LOW);
    if (cur && !s_prev_select) { delay(25); cur = (digitalRead(BTN_SELECT) == LOW); }
    bool edge = cur && !s_prev_select;
    s_prev_select = cur;
    return edge;
}

// Returns true if the power button is held for hold_ms.
// Blocks until released or timeout; ignores short taps.
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
    return false;
}

// ─── Power management ─────────────────────────────────────────────────────────

static void go_to_sleep()
{
    nvs_save_settings(g_threshold, g_use_mph, g_club);
    display_goodbye();
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_27, 0);  // BTN_POWER = GPIO27 (RTC)
    Serial.println("[PWR] Entering deep sleep");
    esp_deep_sleep_start();
}

// ─── Calibration loop ─────────────────────────────────────────────────────────

static void calibration_loop()
{
    Serial.println("[CAL] Enter");
    ui_cal_header();

    float noise_ema = 0.0f, max_seen = 0.0f;
    bool  first  = true;
    const int bin_lo = (int)((double)MIN_DETECT_HZ / SAMPLE_RATE * FFT_SIZE);
    const int bin_hi = min((int)((double)MAX_DETECT_HZ / SAMPLE_RATE * FFT_SIZE),
                           FFT_SIZE/2 - 1);

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
            // Save only the threshold (other settings saved on settings exit)
            nvs_save_settings(g_threshold, g_use_mph, g_club);
            Serial.println("[CAL] Threshold saved, exit");
            return;
        }

        sample_radar();
        run_fft();

        // Find the highest peak in the detection window for the noise/peak metrics
        double peak_mag = 0.0;
        int    peak_bin = -1;
        for (int i = bin_lo; i <= bin_hi; i++) {
            if (vRealL[i] > peak_mag) { peak_mag = vRealL[i]; peak_bin = i; }
        }
        double peak_hz = (peak_bin >= 0)
                         ? ((double)peak_bin * SAMPLE_RATE / FFT_SIZE)
                         : 0.0;

        if (first) { noise_ema = (float)peak_mag; first = false; }
        else if (peak_mag < g_threshold)
            noise_ema = 0.92f * noise_ema + 0.08f * (float)peak_mag;

        if ((float)peak_mag > max_seen) max_seen = (float)peak_mag;

        // Pass vRealL spectrum buffer (Radar L) to display module
        ui_cal_update(vRealL, peak_hz, peak_mag,
                      noise_ema, max_seen, g_threshold, g_use_mph);
    }
}

// ─── Settings loop ────────────────────────────────────────────────────────────

static void settings_loop()
{
    int item = 0;
    ui_settings_draw(item, g_club, g_use_mph);

    while (true) {
        if (scroll_pressed()) {
            item = (item + 1) % 3;
            ui_settings_draw(item, g_club, g_use_mph);
        }
        if (select_pressed()) {
            if (item == 0) {
                g_use_mph = !g_use_mph;
                Serial.printf("[SET] Units → %s\n", speed_unit(g_use_mph));
                ui_settings_draw(item, g_club, g_use_mph);
            } else if (item == 1) {
                reset_stats(g_club, g_stats);
                ui_settings_draw(item, g_club, g_use_mph, true);
                delay(800);
                ui_settings_draw(item, g_club, g_use_mph, false);
            } else {
                calibration_loop();
                ui_settings_draw(item, g_club, g_use_mph);
            }
        }
        if (power_held(2000)) {
            nvs_save_settings(g_threshold, g_use_mph, g_club);
            Serial.println("[SET] Exit");
            return;
        }
    }
}

// ─── Arduino entry points ─────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    Serial.println("\n[OpenScope] v0.6 booting");

    pinMode(BTN_SCROLL, INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP);
    pinMode(BTN_POWER,  INPUT_PULLUP);

    // ADC: 12-bit, 11 dB attenuation → ~0–3.1 V input range
    // On newer ESP-IDF: replace ADC_11db with ADC_ATTEN_DB_12 if it fails.
    analogReadResolution(12);
    analogSetPinAttenuation(RADAR_ADC_PIN_L, ADC_11db);
    analogSetPinAttenuation(RADAR_ADC_PIN_R, ADC_11db);
    analogSetPinAttenuation(RADAR_ADC_PIN_T, ADC_11db);

    nvs_load(g_threshold, g_use_mph, g_club, g_stats, NUM_CLUBS);
    display_init();
    ui_splash(g_club, g_stats, g_use_mph);

    Serial.printf("[OpenScope] Club: %s  Units: %s  Threshold: %.1f  V-half: %.0f°  Top: %.0f°\n",
                  CLUBS[g_club].name, speed_unit(g_use_mph),
                  g_threshold, RADAR_V_HALF_DEG, RADAR_T_ANGLE_DEG);
    Serial.println("[OpenScope] Ready.");
}

void loop()
{
    // ── Button handling ──────────────────────────────────────────────────────
    if (scroll_pressed()) {
        g_club = (g_club + 1) % NUM_CLUBS;
        nvs_save_settings(g_threshold, g_use_mph, g_club);
        Serial.printf("[BTN] Club → %s\n", CLUBS[g_club].name);
        ui_splash(g_club, g_stats, g_use_mph);
        return;
    }
    if (select_pressed()) {
        settings_loop();
        ui_splash(g_club, g_stats, g_use_mph);
        return;
    }
    if (power_held(2000)) {
        go_to_sleep();
        return;
    }

    // ── Radar detection ──────────────────────────────────────────────────────
    sample_radar();
    double ball_hz = 0.0, club_hz = 0.0;
    float  launch_deg = -1.0f, side_deg = 0.0f;
    if (!detect_speeds(ball_hz, club_hz, launch_deg, side_deg, g_threshold)) return;

    // ── Physics ───────────────────────────────────────────────────────────────
    // detect_speeds() returns ball_hz = k (true speed proxy, already corrected
    // for both launch angle α and side angle β by the 3-radar solver).
    // ball_kmh = k · HZ_TO_KMH is the true ball speed off the face.
    float ball_kmh = (float)(ball_hz * HZ_TO_KMH);
    float carry_m;
    if (launch_deg > 0.0f) {
        const float alpha_rad = launch_deg * DEG_TO_RAD;
        const float typ_rad   = CLUBS[g_club].typ_launch * DEG_TO_RAD;
        const float ang_corr  = sinf(2.0f * alpha_rad) / sinf(2.0f * typ_rad);
        carry_m = ball_kmh * CLUBS[g_club].carry_f * ang_corr;
    } else {
        carry_m = ball_kmh * CLUBS[g_club].carry_f;
    }

    const float club_kmh = (club_hz > 0.0) ? (float)(club_hz * HZ_TO_KMH) : 0.0f;
    const float smash    = (club_kmh > 0.0f) ? (ball_kmh / club_kmh) : 0.0f;
    const float total_m  = carry_m * (1.0f + CLUBS[g_club].roll_f);

    const char* side_dir = (side_deg >= 0.0f) ? "R" : "L";
    Serial.printf("[HIT] Ball %.1f km/h | Club %.1f km/h | Smash %.2f"
                  " | Launch %.1f° | Side %s%.1f° | Carry %.0f m | Total %.0f m\n",
                  ball_kmh, club_kmh, smash,
                  (launch_deg > 0.0f) ? launch_deg : 0.0f,
                  side_dir, fabsf(side_deg),
                  carry_m, total_m);

    record_carry(g_club, carry_m, g_stats);
    ui_result(ball_kmh, club_kmh, carry_m, total_m, launch_deg, side_deg,
              g_club, g_use_mph);

    // ── Hold result, remain responsive ───────────────────────────────────────
    uint32_t t0 = millis();
    while (millis() - t0 < 6000) {
        if (scroll_pressed()) {
            g_club = (g_club + 1) % NUM_CLUBS;
            nvs_save_settings(g_threshold, g_use_mph, g_club);
            break;
        }
        if (select_pressed()) break;
        if (power_held(2000)) go_to_sleep();
        delay(20);
    }

    ui_splash(g_club, g_stats, g_use_mph);
}
