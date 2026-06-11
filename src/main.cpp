// OpenScope — DIY Golf Launch Monitor  v0.8
// ESP32 + 1× CDM324 24 GHz Doppler Radar + ILI9488 3.5" touch TFT
//
// Source layout:
//   config.h   — all compile-time constants and pin definitions
//   clubs.h/cpp— Club/ClubStats types and the CLUBS[] table
//   radar.h/cpp— ADC sampling, FFT, peak detection (single Doppler channel)
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

// ─── Power button ─────────────────────────────────────────────────────────────
// The only physical control left. All navigation is on the touch screen
// (see ui_get_tap / ui_*_hit in display.h).

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
        int tx, ty;
        if (ui_get_tap(&tx, &ty)) {
            switch (ui_cal_hit(tx, ty)) {
                case 1:  // -10
                    g_threshold = max(g_threshold - 10.0f, 5.0f);
                    Serial.printf("[CAL] Threshold → %.0f\n", g_threshold);
                    break;
                case 3:  // +10
                    g_threshold = min(g_threshold + 10.0f, 2000.0f);
                    Serial.printf("[CAL] Threshold → %.0f\n", g_threshold);
                    break;
                case 2:  // SAVE + exit
                    nvs_save_settings(g_threshold, g_use_mph, g_club);
                    Serial.println("[CAL] Threshold saved, exit");
                    return;
            }
        }
        if (power_held(2000)) {
            // Hardware fallback: save only the threshold and exit.
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
            if (vReal[i] > peak_mag) { peak_mag = vReal[i]; peak_bin = i; }
        }
        double peak_hz = (peak_bin >= 0)
                         ? ((double)peak_bin * SAMPLE_RATE / FFT_SIZE)
                         : 0.0;

        if (first) { noise_ema = (float)peak_mag; first = false; }
        else if (peak_mag < g_threshold)
            noise_ema = 0.92f * noise_ema + 0.08f * (float)peak_mag;

        if ((float)peak_mag > max_seen) max_seen = (float)peak_mag;

        // Pass the radar spectrum buffer to the display module
        ui_cal_update(vReal, peak_hz, peak_mag,
                      noise_ema, max_seen, g_threshold, g_use_mph);
    }
}

// ─── Settings loop ────────────────────────────────────────────────────────────

static void settings_loop()
{
    ui_settings_draw(g_club, g_use_mph);

    while (true) {
        int tx, ty;
        if (ui_get_tap(&tx, &ty)) {
            switch (ui_settings_hit(tx, ty)) {
                case 0:  // Units
                    g_use_mph = !g_use_mph;
                    Serial.printf("[SET] Units → %s\n", speed_unit(g_use_mph));
                    ui_settings_draw(g_club, g_use_mph);
                    break;
                case 1:  // Reset stats
                    reset_stats(g_club, g_stats);
                    ui_settings_draw(g_club, g_use_mph, true);
                    delay(800);
                    ui_settings_draw(g_club, g_use_mph, false);
                    break;
                case 2:  // Radar calibration
                    calibration_loop();
                    ui_settings_draw(g_club, g_use_mph);
                    break;
                case 3: {  // Touch calibration
                    uint16_t tcal[5];
                    display_touch_calibrate(tcal);
                    nvs_save_touch_cal(tcal);
                    ui_settings_draw(g_club, g_use_mph);
                    break;
                }
                case 9:  // DONE / exit
                    nvs_save_settings(g_threshold, g_use_mph, g_club);
                    Serial.println("[SET] Exit");
                    return;
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
    Serial.println("\n[OpenScope] v0.7 booting");

    pinMode(BTN_POWER, INPUT_PULLUP);

    // ADC: 12-bit, 11 dB attenuation → ~0–3.1 V input range
    // On newer ESP-IDF: replace ADC_11db with ADC_ATTEN_DB_12 if it fails.
    analogReadResolution(12);
    analogSetPinAttenuation(RADAR_ADC_PIN, ADC_11db);

    nvs_load(g_threshold, g_use_mph, g_club, g_stats, NUM_CLUBS);
    display_init();

    // Touch: apply stored calibration, or run the 4-corner calibration once.
    uint16_t tcal[5];
    if (nvs_load_touch_cal(tcal)) {
        display_set_touch_cal(tcal);
        Serial.println("[TOUCH] Calibration loaded");
    } else {
        Serial.println("[TOUCH] No calibration — running first-time setup");
        display_touch_calibrate(tcal);
        nvs_save_touch_cal(tcal);
    }

    ui_splash(g_club, g_stats, g_use_mph);

    Serial.printf("[OpenScope] Club: %s  Units: %s  Threshold: %.1f\n",
                  CLUBS[g_club].name, speed_unit(g_use_mph), g_threshold);
    Serial.println("[OpenScope] Ready.");
}

void loop()
{
    // ── Touch handling ───────────────────────────────────────────────────────
    int tx, ty;
    if (ui_get_tap(&tx, &ty)) {
        int hit = ui_splash_hit(tx, ty);
        if (hit == 1) {                       // tap club circle → next club
            g_club = (g_club + 1) % NUM_CLUBS;
            nvs_save_settings(g_threshold, g_use_mph, g_club);
            Serial.printf("[TOUCH] Club → %s\n", CLUBS[g_club].name);
            ui_splash(g_club, g_stats, g_use_mph);
            return;
        }
        if (hit == 2) {                       // tap bottom bar → settings
            settings_loop();
            ui_splash(g_club, g_stats, g_use_mph);
            return;
        }
    }
    if (power_held(2000)) {
        go_to_sleep();
        return;
    }

    // ── Radar detection ──────────────────────────────────────────────────────
    sample_radar();
    double ball_hz = 0.0, club_hz = 0.0;
    if (!detect_speeds(ball_hz, club_hz, g_threshold)) return;

    // ── Physics ───────────────────────────────────────────────────────────────
    // MEASURED: ball_hz / club_hz are the Doppler shifts. With one sensor
    // aligned to the shot line, ball_kmh = ball_hz · HZ_TO_KMH is the true
    // line-of-sight ball speed off the face. Smash = ball / club speed.
    //
    // MODELED: carry/total are estimated from ball speed via each club's
    // empirical carry factor (carry_f already bakes in that club's typical
    // launch angle) and rollout factor. A single Doppler cannot measure launch
    // angle, so carry is a model — not a per-shot measurement.
    const float ball_kmh = (float)(ball_hz * HZ_TO_KMH);
    const float club_kmh = (club_hz > 0.0) ? (float)(club_hz * HZ_TO_KMH) : 0.0f;
    const float smash    = (club_kmh > 0.0f) ? (ball_kmh / club_kmh) : 0.0f;
    const float carry_m  = ball_kmh * CLUBS[g_club].carry_f;
    const float total_m  = carry_m * (1.0f + CLUBS[g_club].roll_f);

    Serial.printf("[HIT] Ball %.1f km/h | Club %.1f km/h | Smash %.2f"
                  " | Carry %.0f m | Total %.0f m\n",
                  ball_kmh, club_kmh, smash, carry_m, total_m);

    record_carry(g_club, carry_m, g_stats);
    ui_result(ball_kmh, club_kmh, smash, carry_m, total_m, g_club, g_use_mph);

    // ── Hold result, remain responsive ───────────────────────────────────────
    // Any tap dismisses early; otherwise auto-returns after 6 s.
    uint32_t t0 = millis();
    while (millis() - t0 < 6000) {
        int rx, ry;
        if (ui_get_tap(&rx, &ry)) break;
        if (power_held(2000)) go_to_sleep();
        delay(20);
    }

    ui_splash(g_club, g_stats, g_use_mph);
}
