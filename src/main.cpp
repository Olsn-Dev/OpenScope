// OpenScope — DIY Golf Launch Monitor  v0.9
// ESP32 + 1× CDM324 24 GHz Doppler Radar + ILI9488 3.5" SPI TFT
//
// Source layout:
//   config.h   — all compile-time constants, pin definitions and UI enums
//   clubs.h/cpp— Club/ClubStats types and the CLUBS[] table
//   radar.h/cpp— ADC sampling, FFT, peak detection (single Doppler channel)
//   buttons.h/cpp — debounced 3-button input (UP / DOWN / OK)
//   display.h/cpp — all TFT drawing (owns TFT_eSPI object), themes
//   storage.h/cpp — NVS persistence (owns Preferences object)
//   main.cpp   — global state, the button-driven UI state machine, sleep
//
// UI model (LM1-style, driven by three buttons — UP, DOWN, OK):
//   Main menu ──► Mode select ──► Session (Advanced / Large Digit)
//             │              └──► Speed Training
//             └──► Shot History (last 50 shots, persisted in NVS)
//   • UP/DOWN move the list highlight; OK activates the highlighted row.
//   • In a session: UP/DOWN change club (Advanced) or cycle the focused
//     metric (Large Digit); OK opens the session menu (Resume / Change Club /
//     Layout / Settings / End Session).
//   • Holding OK ~1.5 s powers off from anywhere; pressing OK wakes the unit.

#include <Arduino.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "config.h"
#include "clubs.h"
#include "radar.h"
#include "buttons.h"
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

// ─── Power off ────────────────────────────────────────────────────────────────
// Holding OK for BTN_LONG_MS anywhere powers the unit off; pressing OK wakes
// it (GPIO2 is RTC-capable, required for ext0 deep-sleep wake).

static void go_to_sleep()
{
    save_settings();
    display_goodbye();
    // The 3.3V rail stays powered in deep sleep, so the backlight must be
    // switched off explicitly — and held LOW through the sleep, since normal
    // GPIO output state is lost when the digital domain powers down.
    digitalWrite(PIN_TFT_BL, LOW);
    gpio_hold_en((gpio_num_t)PIN_TFT_BL);
    gpio_deep_sleep_hold_en();
    // The long-press event fires while OK is still held — wait for the release,
    // otherwise the low level would wake the chip right back up.
    while (digitalRead(PIN_BTN_OK) == LOW) delay(10);
    delay(50);
    // The normal GPIO pull-ups power down in deep sleep, which would leave the
    // wake pin floating (= random instant wake-ups). Enable the RTC-domain
    // pull-up, which stays active while ext0 wake is armed.
    rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_OK);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_OK);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_OK, 0);
    Serial.println("[PWR] Entering deep sleep");
    esp_deep_sleep_start();
}

// Poll the buttons, handling the global power-off hold. Never returns
// BTN_OK_LONG — that path ends in deep sleep.
static BtnEvent poll_ui()
{
    BtnEvent e = buttons_poll();
    if (e == BTN_OK_LONG) go_to_sleep();   // never returns
    return e;
}

// Move a list selection with UP/DOWN (no wrap). Returns true if it changed.
static bool move_sel(int& sel, BtnEvent e, int n_rows)
{
    const int prev = sel;
    if (e == BTN_UP)   sel = max(sel - 1, 0);
    if (e == BTN_DOWN) sel = min(sel + 1, n_rows - 1);
    return sel != prev;
}

// ─── Calibration loop ─────────────────────────────────────────────────────────
// UP/DOWN nudge the detection threshold (auto-repeat for fast sweeps);
// OK saves and exits.

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
        switch (poll_ui()) {
            case BTN_DOWN:
                g_threshold = max(g_threshold - 10.0f, 5.0f);
                Serial.printf("[CAL] Threshold → %.0f\n", g_threshold);
                break;
            case BTN_UP:
                g_threshold = min(g_threshold + 10.0f, 2000.0f);
                Serial.printf("[CAL] Threshold → %.0f\n", g_threshold);
                break;
            case BTN_OK:
                save_settings();
                Serial.println("[CAL] Threshold saved, exit");
                return;
            default: break;
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
// Seven rows: Units, Color, Layout, Reset Stats, Clear History, Radar Cal.,
// Back. UP/DOWN move the highlight, OK activates the highlighted row.

static void settings_loop()
{
    int sel = 0;
    ui_set_theme(g_blue_theme);
    ui_settings_draw(g_club, g_use_mph, g_blue_theme, (UiLayout)g_layout, sel);

    while (true) {
        const BtnEvent e = poll_ui();
        const int prev = sel;

        if (move_sel(sel, e, SET_N_ROWS)) {
            ui_settings_select(prev, sel);
            continue;
        }
        if (e != BTN_OK) { delay(5); continue; }

        switch (sel) {
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
                                 (UiLayout)g_layout, sel, 3);
                delay(800);
                break;
            case 4:  // Clear shot history
                if (g_hist_count > 0) clear_history(g_hist, g_hist_count);
                ui_settings_draw(g_club, g_use_mph, g_blue_theme,
                                 (UiLayout)g_layout, sel, 4);
                delay(800);
                break;
            case 5:  // Radar calibration
                calibration_loop();
                break;
            case 6:  // Back / exit
                save_settings();
                Serial.println("[SET] Exit");
                return;
        }
        ui_settings_draw(g_club, g_use_mph, g_blue_theme, (UiLayout)g_layout, sel);
    }
}

// ─── Club picker ──────────────────────────────────────────────────────────────
// UP/DOWN move the highlight (the view scrolls to follow), OK selects the
// highlighted club. Selecting the current club leaves it unchanged.

static void club_picker()
{
    const int max_scroll = max(0, NUM_CLUBS - PICK_ROWS);
    int sel    = g_club;
    int scroll = constrain(sel - PICK_ROWS / 2, 0, max_scroll);
    ui_picker_draw(g_club, scroll, sel);

    while (true) {
        const BtnEvent e = poll_ui();
        const int prev = sel;

        if (move_sel(sel, e, NUM_CLUBS)) {
            // Keep the highlight on screen; page redraw only when it scrolls.
            int new_scroll = constrain(scroll, sel - PICK_ROWS + 1, sel);
            new_scroll = constrain(new_scroll, 0, max_scroll);
            if (new_scroll != scroll) {
                scroll = new_scroll;
                ui_picker_draw(g_club, scroll, sel);
            } else {
                ui_picker_select(prev, sel, scroll, g_club);
            }
            continue;
        }
        if (e == BTN_OK) {
            g_club = sel;
            save_settings();
            Serial.printf("[PICK] Club → %s\n", CLUBS[g_club].name);
            return;
        }
        delay(5);
    }
}

// ─── Shot history loop ────────────────────────────────────────────────────────
// Newest-first table of the persisted shot log. UP/DOWN page the list
// (UP = newer, DOWN = older); OK goes back. Clearing lives in Settings.

static void history_loop()
{
    int scroll = 0;
    ui_history_draw(g_hist, g_hist_count, scroll, g_use_mph);

    while (true) {
        const int max_scroll = max(0, g_hist_count - HIST_ROWS);
        switch (poll_ui()) {
            case BTN_DOWN:                               // reveal older shots
                if (scroll < max_scroll) {
                    scroll = min(scroll + (HIST_ROWS - 1), max_scroll);
                    ui_history_draw(g_hist, g_hist_count, scroll, g_use_mph);
                }
                break;
            case BTN_UP:                                 // reveal newer shots
                if (scroll > 0) {
                    scroll = max(scroll - (HIST_ROWS - 1), 0);
                    ui_history_draw(g_hist, g_hist_count, scroll, g_use_mph);
                }
                break;
            case BTN_OK:
                return;
            default:
                delay(5);
        }
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

// ─── Session menu ─────────────────────────────────────────────────────────────
// Opened with OK during a session. Returns the chosen row:
//   0 Resume, 1 Change Club, 2 Layout toggle, 3 Settings, 4 End Session

static int session_menu()
{
    int sel = 0;
    ui_smenu_draw(sel, (UiLayout)g_layout);

    while (true) {
        const BtnEvent e = poll_ui();
        const int prev = sel;
        if (move_sel(sel, e, SMENU_N_ROWS)) { ui_smenu_select(prev, sel); continue; }
        if (e == BTN_OK) return sel;
        delay(5);
    }
}

// ─── Session loop (Practice / On Course) ──────────────────────────────────────
// Measures shots while staying responsive to the buttons. Returns when the
// user picks End Session in the session menu.

static void run_session(SessionMode /*mode*/)
{
    draw_ready();

    while (true) {
        // ── Buttons ──────────────────────────────────────────────────────────
        const BtnEvent e = poll_ui();

        if (e == BTN_UP || e == BTN_DOWN) {
            if (g_layout == LAYOUT_ADVANCED) {           // change club
                g_club = (g_club + (e == BTN_UP ? NUM_CLUBS - 1 : 1)) % NUM_CLUBS;
                save_settings();
            } else {                                     // cycle focused metric
                g_metric = (e == BTN_DOWN)
                           ? (UiMetric)((g_metric + 1) % MET_COUNT)
                           : (UiMetric)((g_metric + MET_COUNT - 1) % MET_COUNT);
            }
            draw_ready();
            continue;
        }
        if (e == BTN_OK) {
            switch (session_menu()) {
                case 1: club_picker();  break;
                case 2:                                   // toggle layout
                    g_layout = (g_layout == LAYOUT_ADVANCED)
                               ? LAYOUT_LARGE_DIGIT : LAYOUT_ADVANCED;
                    save_settings();
                    break;
                case 3: settings_loop(); break;
                case 4: return;                           // End Session
                default: break;                           // 0 = Resume
            }
            draw_ready();
            continue;
        }

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

        // Hold the result; any button dismisses early, else auto-return at 6 s.
        uint32_t t0 = millis();
        while (millis() - t0 < 6000) {
            if (poll_ui() != BTN_NONE) break;
            delay(20);
        }
        draw_ready();
    }
}

// ─── Speed Training loop ──────────────────────────────────────────────────────
// One big swing-speed number (the dominant Doppler peak). OK goes back.

static void speed_loop()
{
    ui_set_theme(g_blue_theme);
    ui_speed(0.0f, g_use_mph, false);

    while (true) {
        if (poll_ui() == BTN_OK) return;

        sample_radar();
        double ball_hz = 0.0, club_hz = 0.0;
        if (!detect_speeds(ball_hz, club_hz, g_threshold)) continue;

        const float kmh = (float)(ball_hz * HZ_TO_KMH);          // dominant peak
        Serial.printf("[SPEED] %.1f km/h\n", kmh);
        ui_speed(kmh, g_use_mph, true);

        uint32_t t0 = millis();
        while (millis() - t0 < 4000) {
            if (poll_ui() != BTN_NONE) break;
            delay(20);
        }
        ui_speed(0.0f, g_use_mph, false);
    }
}

// ─── Blocking menu helpers ────────────────────────────────────────────────────

// Run the main-menu selection. Returns the activated row 0..3.
static int menu_wait()
{
    int sel = 0;
    ui_menu_draw(sel);
    while (true) {
        const BtnEvent e = poll_ui();
        const int prev = sel;
        if (move_sel(sel, e, 4)) { ui_menu_select(prev, sel); continue; }
        if (e == BTN_OK) return sel;
        delay(5);
    }
}

// Run the mode-select screen. Returns 0/1/2 (mode) or -1 for Back.
static int mode_wait()
{
    int sel = 0;
    ui_mode_draw(sel);
    while (true) {
        const BtnEvent e = poll_ui();
        const int prev = sel;
        if (move_sel(sel, e, 4)) { ui_mode_select(prev, sel); continue; }
        if (e == BTN_OK) return (sel == 3) ? -1 : sel;
        delay(5);
    }
}

// ─── Arduino entry points ─────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    Serial.println("\n[OpenScope] " FW_VERSION " booting");

    // After a deep-sleep wake the OK pad is still claimed by the RTC mux
    // (go_to_sleep enables its RTC pull-up); hand it back to the digital GPIO
    // matrix before buttons_init() configures it. The backlight hold from
    // go_to_sleep must be released too, or display_init can't switch it on.
    rtc_gpio_deinit((gpio_num_t)PIN_BTN_OK);
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)PIN_TFT_BL);
    buttons_init();

    // ADC: 12-bit, 11 dB attenuation → ~0–3.1 V input range
    analogReadResolution(12);
    analogSetPinAttenuation(RADAR_ADC_PIN, ADC_11db);

    nvs_load(g_threshold, g_use_mph, g_club, g_blue_theme, g_layout,
             g_stats, NUM_CLUBS);
    nvs_load_history(g_hist, g_hist_count);
    Serial.printf("[NVS] Shot history: %d shot(s)\n", g_hist_count);
    display_init();
    ui_set_theme(g_blue_theme);
    display_splash();

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
        case ST_MENU:
            switch (menu_wait()) {
                case 0: g_state = ST_MODE; break;        // Start Session
                case 1: history_loop();    break;        // Shot History
                case 2: settings_loop();   break;        // Settings
                case 3: go_to_sleep();     break;        // Shut Down
            }
            break;

        case ST_MODE:
            switch (mode_wait()) {
                case -1: g_state = ST_MENU;            break;   // Back
                case MODE_SPEED: speed_loop();          break;
                case MODE_PRACTICE:
                case MODE_ONCOURSE:
                    run_session((SessionMode)MODE_PRACTICE);
                    break;
            }
            break;
    }
}
