// OpenScope — DIY Golf Launch Monitor  v0.9
// ESP32 + 1× CDM324 24 GHz Doppler Radar + ILI9488 3.5" touch TFT
//
// Source layout:
//   config.h   — all compile-time constants, pin definitions and UI enums
//   clubs.h/cpp— Club/ClubStats types and the CLUBS[] table
//   radar.h/cpp— ADC sampling, FFT, peak detection (single Doppler channel)
//   display.h/cpp — all TFT drawing (owns TFT_eSPI object), themes, gestures
//   storage.h/cpp — NVS persistence (owns Preferences object)
//   main.cpp   — global state, the touch UI state machine, sleep
//
// UI model (LM1-style, fully touch-driven — only Power is a physical button):
//   Main menu ──► Mode select ──► Session (Advanced / Large Digit)
//             │              └──► Speed Training
//             └──► Shot History (last 50 shots, persisted in NVS)
//   • Swipe ↔ toggles Advanced ⇄ Large Digit; swipe ↕ cycles the focused
//     metric in Large Digit.
//   • Tap the club pill to open the scrollable club picker.
//   • Bottom-left "Back" (or left-edge swipe-right) goes up a level; the
//     bottom-right gear opens Settings. Colour/Layout/Units live in Settings.

#include <Arduino.h>
#include "esp_sleep.h"
#include "config.h"
#include "clubs.h"
#include "radar.h"
#include "display.h"
#include "storage.h"

// ─── Global state ─────────────────────────────────────────────────────────────

static ClubStats  g_stats[NUM_CLUBS];
static ShotRecord g_hist[HISTORY_MAX];
static int        g_hist_count = 0;
static int       g_club       = 0;
static float     g_threshold  = PEAK_THRESHOLD_DEFAULT;
static bool      g_use_mph    = false;
static bool      g_blue_theme = false;                 // false = Black theme
static int       g_layout     = LAYOUT_ADVANCED;       // session display layout
static UiMetric  g_metric     = MET_TOTAL;             // Large-Digit focus

// Top-level screen the loop() dispatcher is showing.
enum AppState { ST_MENU, ST_MODE };
static AppState g_state = ST_MENU;

// Persist every user-visible setting in one shot.
static void save_settings()
{
    nvs_save_settings(g_threshold, g_use_mph, g_club, g_blue_theme, g_layout);
}

// ─── Power button ─────────────────────────────────────────────────────────────
// The only physical control left. All navigation is on the touch screen.

// Returns true if the power button is held for hold_ms (ignores short taps).
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

static void go_to_sleep()
{
    save_settings();
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
                    save_settings();
                    Serial.println("[CAL] Threshold saved, exit");
                    return;
            }
        }
        if (power_held(2000)) {
            save_settings();
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

        ui_cal_update(vReal, peak_hz, peak_mag,
                      noise_ema, max_seen, g_threshold, g_use_mph);
    }
}

// ─── Settings loop ────────────────────────────────────────────────────────────
// Six tappable rows: Units, Color, Layout, Reset Stats, Radar Cal., Touch Cal.
// Exit via the header back chevron, a left-edge swipe-right, or a power hold.

static void settings_loop()
{
    ui_set_theme(g_blue_theme);
    ui_settings_draw(g_club, g_use_mph, g_blue_theme, (UiLayout)g_layout);

    while (true) {
        int gx, gy;
        UiGesture g = ui_get_gesture(&gx, &gy);

        if (g == GES_SWIPE_R && gx < EDGE_BACK_X) { save_settings(); return; }

        if (g == GES_TAP) {
            switch (ui_settings_hit(gx, gy)) {
                case 0:  // Units (Mph/Yds ⇄ Kmh/m)
                    g_use_mph = !g_use_mph;
                    Serial.printf("[SET] Units → %s\n", speed_unit(g_use_mph));
                    break;
                case 1:  // Color (Black ⇄ Blue)
                    g_blue_theme = !g_blue_theme;
                    ui_set_theme(g_blue_theme);
                    Serial.printf("[SET] Theme → %s\n", g_blue_theme ? "Blue" : "Black");
                    break;
                case 2:  // Layout (Advanced ⇄ Large Digit)
                    g_layout = (g_layout == LAYOUT_ADVANCED)
                               ? LAYOUT_LARGE_DIGIT : LAYOUT_ADVANCED;
                    Serial.printf("[SET] Layout → %s\n",
                                  g_layout == LAYOUT_ADVANCED ? "Advanced" : "Large");
                    break;
                case 3:  // Reset stats (current club)
                    reset_stats(g_club, g_stats);
                    ui_settings_draw(g_club, g_use_mph, g_blue_theme,
                                     (UiLayout)g_layout, true);
                    delay(800);
                    break;
                case 4:  // Radar calibration
                    calibration_loop();
                    break;
                case 5: {  // Touch calibration
                    uint16_t tcal[5];
                    display_touch_calibrate(tcal);
                    nvs_save_touch_cal(tcal);
                    break;
                }
                case 9:  // Back / exit
                    save_settings();
                    Serial.println("[SET] Exit");
                    return;
                default:
                    continue;   // tapped dead space — no redraw
            }
            ui_settings_draw(g_club, g_use_mph, g_blue_theme, (UiLayout)g_layout);
        }

        if (power_held(2000)) { save_settings(); return; }
        delay(5);
    }
}

// ─── Club picker ──────────────────────────────────────────────────────────────
// Scrollable list; tap a club to select, Back to keep the current one.
// (TFT_eSPI has no inertial scrolling, so the list pages by swipe ↕.)

static void club_picker()
{
    const int max_scroll = max(0, NUM_CLUBS - PICK_ROWS);
    int scroll = constrain(g_club - PICK_ROWS / 2, 0, max_scroll);
    ui_picker_draw(g_club, scroll);

    while (true) {
        int gx, gy;
        UiGesture g = ui_get_gesture(&gx, &gy);

        if (g == GES_SWIPE_R && gx < EDGE_BACK_X) return;   // edge-swipe back
        if (g == GES_SWIPE_U) {                              // reveal later clubs
            scroll = min(scroll + (PICK_ROWS - 1), max_scroll);
            ui_picker_draw(g_club, scroll);
        } else if (g == GES_SWIPE_D) {                       // reveal earlier clubs
            scroll = max(scroll - (PICK_ROWS - 1), 0);
            ui_picker_draw(g_club, scroll);
        } else if (g == GES_TAP) {
            int h = ui_picker_hit(gx, gy, scroll);
            if (h == 99) return;                             // Back — no change
            if (h >= 0) {
                g_club = h;
                save_settings();
                Serial.printf("[PICK] Club → %s\n", CLUBS[g_club].name);
                return;
            }
        }
        if (power_held(2000)) { go_to_sleep(); return; }
        delay(8);
    }
}

// ─── Shot history loop ────────────────────────────────────────────────────────
// Newest-first table of the persisted shot log. Pages by swipe ↕ like the club
// picker; Back via header chevron / left-edge swipe; Clear erases the log.

static void history_loop()
{
    int scroll = 0;
    ui_history_draw(g_hist, g_hist_count, scroll, g_use_mph);

    while (true) {
        const int max_scroll = max(0, g_hist_count - HIST_ROWS);
        int gx, gy;
        UiGesture g = ui_get_gesture(&gx, &gy);

        if (g == GES_SWIPE_R && gx < EDGE_BACK_X) return;   // edge-swipe back
        if (g == GES_SWIPE_U) {                              // reveal older shots
            scroll = min(scroll + (HIST_ROWS - 1), max_scroll);
            ui_history_draw(g_hist, g_hist_count, scroll, g_use_mph);
        } else if (g == GES_SWIPE_D) {                       // reveal newer shots
            scroll = max(scroll - (HIST_ROWS - 1), 0);
            ui_history_draw(g_hist, g_hist_count, scroll, g_use_mph);
        } else if (g == GES_TAP) {
            switch (ui_history_hit(gx, gy)) {
                case 99: return;                             // Back
                case 98:                                     // Clear
                    if (g_hist_count > 0) {
                        clear_history(g_hist, g_hist_count);
                        scroll = 0;
                        ui_history_draw(g_hist, g_hist_count, scroll, g_use_mph);
                    }
                    break;
            }
        }
        if (power_held(2000)) { go_to_sleep(); return; }
        delay(8);
    }
}

// ─── Session rendering helpers ────────────────────────────────────────────────

static void draw_ready()
{
    ui_set_theme(g_blue_theme);
    if (g_layout == LAYOUT_ADVANCED) ui_splash(g_club, g_stats, g_use_mph);
    else                             ui_large_ready(g_metric, g_club, g_use_mph);
}

static void draw_result(float ball_kmh, float club_kmh, float smash,
                        float carry_m, float total_m)
{
    ui_set_theme(g_blue_theme);
    if (g_layout == LAYOUT_ADVANCED)
        ui_result(ball_kmh, club_kmh, smash, carry_m, total_m, g_club, g_use_mph);
    else
        ui_large_result(g_metric, ball_kmh, club_kmh, smash,
                        carry_m, total_m, g_club, g_use_mph);
}

// ─── Session loop (Practice / On Course) ──────────────────────────────────────
// Measures shots while staying responsive to touch. Returns when the user
// navigates Back to the mode-select screen.

static void run_session(SessionMode /*mode*/)
{
    draw_ready();

    while (true) {
        // ── Touch / gestures ─────────────────────────────────────────────────
        int gx, gy;
        UiGesture g = ui_get_gesture(&gx, &gy);

        if (g == GES_SWIPE_R && gx < EDGE_BACK_X) return;        // edge-swipe back

        if (g == GES_SWIPE_L || g == GES_SWIPE_R) {              // toggle layout
            g_layout = (g_layout == LAYOUT_ADVANCED)
                       ? LAYOUT_LARGE_DIGIT : LAYOUT_ADVANCED;
            save_settings();
            draw_ready();
            continue;
        }
        if (g_layout == LAYOUT_LARGE_DIGIT &&
            (g == GES_SWIPE_U || g == GES_SWIPE_D)) {            // cycle metric
            g_metric = (g == GES_SWIPE_U)
                       ? (UiMetric)((g_metric + 1) % MET_COUNT)
                       : (UiMetric)((g_metric + MET_COUNT - 1) % MET_COUNT);
            draw_ready();
            continue;
        }
        if (g == GES_TAP) {
            int hit = (g_layout == LAYOUT_ADVANCED) ? ui_splash_hit(gx, gy)
                                                    : ui_large_hit(gx, gy);
            switch (hit) {
                case 3: return;                                  // Back
                case 2: settings_loop(); draw_ready(); continue; // Menu/Settings
                case 1: club_picker();   draw_ready(); continue; // club pill
                case 4:                                          // metric cell
                    if (g_layout == LAYOUT_ADVANCED) {
                        int m = ui_advanced_metric_at(gx, gy);
                        if (m >= 0) {
                            g_metric = (UiMetric)m;
                            g_layout = LAYOUT_LARGE_DIGIT;
                            save_settings();
                            draw_ready();
                        }
                    }
                    continue;
            }
        }

        if (power_held(2000)) { go_to_sleep(); return; }

        // ── Radar detection ──────────────────────────────────────────────────
        sample_radar();
        double ball_hz = 0.0, club_hz = 0.0;
        if (!detect_speeds(ball_hz, club_hz, g_threshold)) continue;

        // ── Physics ───────────────────────────────────────────────────────────
        // MEASURED: ball/club speed off the Doppler shift. MODELED: carry/total
        // from each club's empirical carry+roll factors (a single Doppler can't
        // measure launch angle, so distances are a model, not a per-shot reading).
        const float ball_kmh = (float)(ball_hz * HZ_TO_KMH);
        const float club_kmh = (club_hz > 0.0) ? (float)(club_hz * HZ_TO_KMH) : 0.0f;
        const float smash    = (club_kmh > 0.0f) ? (ball_kmh / club_kmh) : 0.0f;
        const float carry_m  = ball_kmh * CLUBS[g_club].carry_f;
        const float total_m  = carry_m * (1.0f + CLUBS[g_club].roll_f);

        Serial.printf("[HIT] Ball %.1f km/h | Club %.1f km/h | Smash %.2f"
                      " | Carry %.0f m | Total %.0f m\n",
                      ball_kmh, club_kmh, smash, carry_m, total_m);

        record_carry(g_club, carry_m, g_stats);
        record_shot({ (uint8_t)g_club, ball_kmh, club_kmh, carry_m, total_m },
                    g_hist, g_hist_count);
        draw_result(ball_kmh, club_kmh, smash, carry_m, total_m);

        // Hold the result; any tap/swipe dismisses early, else auto-return at 6 s.
        uint32_t t0 = millis();
        while (millis() - t0 < 6000) {
            int rx, ry;
            if (ui_get_gesture(&rx, &ry) != GES_NONE) break;
            if (power_held(2000)) go_to_sleep();
            delay(20);
        }
        draw_ready();
    }
}

// ─── Speed Training loop ──────────────────────────────────────────────────────
// One big swing-speed number (the dominant Doppler peak). No club/layout here.

static void speed_loop()
{
    ui_set_theme(g_blue_theme);
    ui_speed(0.0f, g_use_mph, false);

    while (true) {
        int gx, gy;
        UiGesture g = ui_get_gesture(&gx, &gy);
        if (g == GES_SWIPE_R && gx < EDGE_BACK_X) return;
        if (g == GES_TAP && gy >= BAR_Y && gx < 150) return;     // Back

        if (power_held(2000)) { go_to_sleep(); return; }

        sample_radar();
        double ball_hz = 0.0, club_hz = 0.0;
        if (!detect_speeds(ball_hz, club_hz, g_threshold)) continue;

        const float kmh = (float)(ball_hz * HZ_TO_KMH);          // dominant peak
        Serial.printf("[SPEED] %.1f km/h\n", kmh);
        ui_speed(kmh, g_use_mph, true);

        uint32_t t0 = millis();
        while (millis() - t0 < 4000) {
            int rx, ry;
            if (ui_get_gesture(&rx, &ry) != GES_NONE) break;
            if (power_held(2000)) go_to_sleep();
            delay(20);
        }
        ui_speed(0.0f, g_use_mph, false);
    }
}

// ─── Blocking menu helpers ────────────────────────────────────────────────────

// Wait for a main-menu row tap. Returns 0..3; a power hold sleeps directly.
static int menu_wait()
{
    while (true) {
        int gx, gy;
        if (ui_get_gesture(&gx, &gy) == GES_TAP) {
            int h = ui_menu_hit(gx, gy);
            if (h >= 0) return h;
        }
        if (power_held(2000)) go_to_sleep();
        delay(8);
    }
}

// Wait for a mode-select tap. Returns 0/1/2 (mode) or -1 for Back.
static int mode_wait()
{
    while (true) {
        int gx, gy;
        UiGesture g = ui_get_gesture(&gx, &gy);
        if (g == GES_SWIPE_R && gx < EDGE_BACK_X) return -1;
        if (g == GES_TAP) {
            int h = ui_mode_hit(gx, gy);
            if (h == 3) return -1;                 // Back
            if (h >= 0) return h;
        }
        if (power_held(2000)) go_to_sleep();
        delay(8);
    }
}

// ─── Arduino entry points ─────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    Serial.println("\n[OpenScope] v0.9 booting");

    pinMode(BTN_POWER, INPUT_PULLUP);

    // ADC: 12-bit, 11 dB attenuation → ~0–3.1 V input range
    analogReadResolution(12);
    analogSetPinAttenuation(RADAR_ADC_PIN, ADC_11db);

    nvs_load(g_threshold, g_use_mph, g_club, g_blue_theme, g_layout,
             g_stats, NUM_CLUBS);
    nvs_load_history(g_hist, g_hist_count);
    Serial.printf("[NVS] Shot history: %d shot(s)\n", g_hist_count);
    display_init();
    ui_set_theme(g_blue_theme);

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

    g_state = ST_MENU;
    Serial.printf("[OpenScope] Club: %s  Units: %s  Theme: %s  Layout: %s\n",
                  CLUBS[g_club].name, speed_unit(g_use_mph),
                  g_blue_theme ? "Blue" : "Black",
                  g_layout == LAYOUT_ADVANCED ? "Advanced" : "Large");
    Serial.println("[OpenScope] Ready.");
}

void loop()
{
    switch (g_state) {
        case ST_MENU: {
            ui_set_theme(g_blue_theme);
            ui_menu_draw();
            int sel = menu_wait();
            if      (sel == 0) g_state = ST_MODE;       // Start Session
            else if (sel == 1) history_loop();          // Shot History → back to menu
            else if (sel == 2) settings_loop();         // Settings → back to menu
            else if (sel == 3) go_to_sleep();           // Shut Down
            break;
        }
        case ST_MODE: {
            ui_set_theme(g_blue_theme);
            ui_mode_draw();
            int m = mode_wait();
            if      (m < 0)           g_state = ST_MENU;          // Back
            else if (m == MODE_SPEED) speed_loop();              // → back to mode
            else                      run_session((SessionMode)m); // → back to mode
            break;
        }
    }
}
