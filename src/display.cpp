#include <Arduino.h>
#include <TFT_eSPI.h>
#include "display.h"
#include "config.h"
#include "clubs.h"

// The TFT object lives here — all drawing goes through this module.
static TFT_eSPI tft;

// ─── Theme ──────────────────────────────────────────────────────────────────
// Only the metric-label colour changes between the two LM1 themes; everything
// else (black bg, white numbers, grey units) is shared. Default: black theme.
static uint16_t s_label_col = COL_LABEL_BLACK;

void ui_set_theme(bool blue_theme)
{
    s_label_col = blue_theme ? COL_LABEL_BLUE : COL_LABEL_BLACK;
}

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

void display_splash()
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4); tft.setTextSize(2);              // ~52 px wordmark
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("OpenScope", SCR_W / 2, SCR_H / 2 - 30);
    tft.setTextSize(1);                                  // others assume size 1
    tft.drawFastHLine(SCR_W / 2 - 92, SCR_H / 2 + 6, 184, s_label_col);
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString("GOLF LAUNCH MONITOR", SCR_W / 2, SCR_H / 2 + 26);
    tft.drawString(FW_VERSION, SCR_W / 2, SCR_H / 2 + 48);
    delay(1000);
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

// ─── Selection outline ────────────────────────────────────────────────────────
// The highlighted list row gets a 2-px white rounded outline. Erasing is done
// by the row renderers themselves (they repaint the full row card), so the
// ui_*_select helpers just redraw the two affected rows.

static void sel_outline(int x, int y, int w, int h, int r)
{
    tft.drawRoundRect(x,     y,     w,     h,     r, TFT_WHITE);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r, TFT_WHITE);
}

// ─── Button-bar primitive ─────────────────────────────────────────────────────

static void draw_button(int x, int y, int w, int h, const char* label,
                        uint16_t border, uint16_t text_col)
{
    tft.fillRoundRect(x + 1, y + 1, w - 2, h - 2, BTN_RADIUS, COL_BTN_BG);
    tft.drawRoundRect(x, y, w, h, BTN_RADIUS, border);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4); tft.setTextColor(text_col, COL_BTN_BG);
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
    uint16_t lc = dimmed ? COL_DIM : s_label_col;   // themed label colour
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

// LM1-style club selector: a rounded-rectangle *outline* pill holding the
// current club abbreviation. `hint` is a small caption drawn under the pill
// (e.g. which button changes the club); nullptr = no caption.
static void draw_club_pill(int x, int y, int w, int h, int club_idx,
                           const char* hint = nullptr)
{
    const int cx = x + w / 2, cy = y + h / 2;
    tft.fillRoundRect(x, y, w, h, BTN_RADIUS, COL_BTN_BG);
    tft.drawRoundRect(x, y, w, h, BTN_RADIUS, s_label_col);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, BTN_RADIUS, s_label_col);
    tft.setTextDatum(MC_DATUM);
    // Font 4 (full ASCII) — fonts 6/7/8 are numeric-only and can't draw the
    // alphabetic club abbreviations ("PW", "3W", "LW", …).
    tft.setTextFont(4);
    tft.setTextColor(TFT_WHITE, COL_BTN_BG);
    tft.drawString(CLUBS[club_idx].abbr, cx, cy - 2);
    if (hint) {
        tft.setTextFont(1); tft.setTextColor(COL_UNIT, TFT_BLACK);
        tft.drawString(hint, cx, y + h + 8);
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
//   (mini row below: button hints)

// Bottom hint strip shared by the session screens: three text zones telling
// the user what the buttons do right now.
static void draw_session_nav(const char* left, const char* centre,
                             const char* right)
{
    const int by = BAR_Y + BAR_H / 2 + 1;

    tft.fillRect(0, BAR_Y + 1, SCR_W, BAR_H - 1, COL_TILE_BG);
    tft.drawFastHLine(0, BAR_Y, SCR_W, COL_DIV);

    tft.setTextFont(2);
    tft.setTextDatum(ML_DATUM); tft.setTextColor(COL_UNIT, COL_TILE_BG);
    tft.drawString(left, 12, by);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(s_label_col, COL_TILE_BG);
    tft.drawString(centre, SCR_W / 2, by);
    tft.setTextDatum(MR_DATUM); tft.setTextColor(COL_UNIT, COL_TILE_BG);
    tft.drawString(right, SCR_W - 12, by);
}

// ── Advanced layout ────────────────────────────────────────────────────────

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
    draw_club_pill(PILL_X, PILL_Y, PILL_W, PILL_H, club_idx, "UP/DN TO CHANGE");

    draw_session_nav("UP/DN: CLUB", "SWING WHEN READY", "OK: MENU");
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

    draw_club_pill(PILL_X, PILL_Y, PILL_W, PILL_H, club_idx);

    // Mini row — dismiss hint, same dock styling as the session nav.
    tft.fillRect(0, BAR_Y + 1, SCR_W, BAR_H - 1, COL_TILE_BG);
    tft.drawFastHLine(0, BAR_Y, SCR_W, COL_DIV);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2); tft.setTextColor(s_label_col, COL_TILE_BG);
    tft.drawString("ANY BUTTON TO CONTINUE", SCR_W / 2, ROW_H * 2 + MINI_ROW_H / 2);
}

// ── Large Digit layout ─────────────────────────────────────────────────────
// One metric, huge. Label on top, big white value centred in the left area,
// unit beneath, club pill on the right.

static void draw_large(const char* label, const char* value, const char* unit,
                       int club_idx, const char* centre_hint, uint16_t value_col)
{
    tft.fillScreen(TFT_BLACK);

    const int area_w = SCR_W - PILL_W - 40;        // space left of the pill
    const int cx     = area_w / 2 + 8;

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(4); tft.setTextColor(s_label_col, TFT_BLACK);
    tft.drawString(label, cx, 28);

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(8); tft.setTextColor(value_col, TFT_BLACK);   // ~75 px digits
    tft.drawString(value, cx, 130);

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(4); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString(unit, cx, 188);

    // Club pill on the right, vertically centred in the metric area.
    draw_club_pill(LPILL_X, LPILL_Y, PILL_W, PILL_H, club_idx, "MENU TO CHANGE");

    draw_session_nav("UP/DN: METRIC", centre_hint, "OK: MENU");
}

// Format one metric's value+unit into caller buffers, returning its label and
// colour. Mirrors the physics in main.cpp. club/smash may be "unset" (≤0).
static const char* large_metric_strings(UiMetric m,
        float ball_kmh, float club_kmh, float smash,
        float carry_m, float total_m, bool use_mph,
        char* val, size_t vn, const char** unit, uint16_t* col)
{
    *col = TFT_WHITE;
    switch (m) {
        case MET_CLUB:
            *unit = speed_unit(use_mph);
            if (club_kmh > 0.0f) snprintf(val, vn, "%.0f", disp_speed(club_kmh, use_mph));
            else                 snprintf(val, vn, "--");
            return "Club Speed";
        case MET_BALL:
            *unit = speed_unit(use_mph);
            snprintf(val, vn, "%.0f", disp_speed(ball_kmh, use_mph));
            return "Ball Speed";
        case MET_SMASH:
            *unit = "Factor"; *col = TFT_GREEN;
            if (smash > 0.0f) snprintf(val, vn, "%.2f", smash);
            else            { snprintf(val, vn, "--"); *col = TFT_WHITE; }
            return "Smash";
        case MET_CARRY:
            *unit = dist_unit(use_mph);
            snprintf(val, vn, "%.0f", disp_dist(carry_m, use_mph));
            return "Carry";
        default: /* MET_TOTAL */
            *unit = dist_unit(use_mph);
            snprintf(val, vn, "%.0f", disp_dist(total_m, use_mph));
            return "Total Distance";
    }
}

// Large-Digit "ready" — shows the focused metric with placeholder dashes.
void ui_large_ready(UiMetric metric, int club_idx, bool use_mph)
{
    char val[12]; const char* unit; uint16_t col;
    const char* label = large_metric_strings(metric, 0, 0, 0, 0, 0, use_mph,
                                             val, sizeof(val), &unit, &col);
    snprintf(val, sizeof(val), "--");
    draw_large(label, val, unit, club_idx, "SWING WHEN READY", TFT_WHITE);
}

// Large-Digit result — shows the focused metric's measured/modeled value.
void ui_large_result(UiMetric metric,
                     float ball_kmh, float club_kmh, float smash,
                     float carry_m, float total_m,
                     int club_idx, bool use_mph)
{
    char val[12]; const char* unit; uint16_t col;
    const char* label = large_metric_strings(metric, ball_kmh, club_kmh, smash,
                                             carry_m, total_m, use_mph,
                                             val, sizeof(val), &unit, &col);
    draw_large(label, val, unit, club_idx, "ANY BUTTON TO CONTINUE", col);
}

// ── Speed Training ─────────────────────────────────────────────────────────
// A single huge swing-speed number, full width (no club pill).

void ui_speed(float swing_kmh, bool use_mph, bool have)
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(4); tft.setTextColor(s_label_col, TFT_BLACK);
    tft.drawString("SWING SPEED", SCR_W / 2, 30);

    char val[12];
    if (have) snprintf(val, sizeof(val), "%.0f", disp_speed(swing_kmh, use_mph));
    else      snprintf(val, sizeof(val), "--");
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(8); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(val, SCR_W / 2, 140);

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(4); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString(speed_unit(use_mph), SCR_W / 2, 198);

    draw_session_nav("HOLD OK: OFF", "SWING TO MEASURE", "OK: BACK");
}

// ── List-row renderers ─────────────────────────────────────────────────────

// Shared full-width list-row renderer for the menu-style screens. `selected`
// adds the white highlight outline. `h` is the row pitch (card is h - GAP).
static void draw_menu_row(int i, const char* label, uint16_t accent,
                          int hdr_h, int row_h, bool selected)
{
    const int y  = hdr_h + i * row_h;
    const int h  = row_h - MENU_ROW_GAP;
    const int cy = y + h / 2 + 2;
    tft.fillRoundRect(8, y + 4, SCR_W - 16, h - 4, BTN_RADIUS, COL_BTN_BG);
    tft.fillRoundRect(8, y + 4, 6, h - 4, BTN_RADIUS, accent);   // accent stripe
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE, COL_BTN_BG);
    tft.drawString(label, 28, cy);
    // "›" affordance chevron on the right — these rows navigate somewhere.
    tft.drawLine(SCR_W - 34, cy - 8, SCR_W - 26, cy, COL_UNIT);
    tft.drawLine(SCR_W - 26, cy, SCR_W - 34, cy + 8, COL_UNIT);
    tft.drawLine(SCR_W - 35, cy - 8, SCR_W - 27, cy, COL_UNIT);
    tft.drawLine(SCR_W - 27, cy, SCR_W - 35, cy + 8, COL_UNIT);
    if (selected) sel_outline(8, y + 4, SCR_W - 16, h - 4, BTN_RADIUS);
}

// Shared sub-screen header: dark card strip with a themed accent underline
// and an optional right-aligned caption (used for button hints / version).
static void draw_screen_header(const char* title, int h,
                               const char* right = nullptr)
{
    tft.fillRect(0, 0, SCR_W, h - 2, COL_TILE_BG);
    tft.drawFastHLine(0, h - 2, SCR_W, s_label_col);     // accent underline
    tft.drawFastHLine(0, h - 1, SCR_W, COL_DIV);
    const int y = (h - 2) / 2;
    if (right) {
        tft.setTextDatum(MR_DATUM);
        tft.setTextFont(2); tft.setTextColor(COL_UNIT, COL_TILE_BG);
        tft.drawString(right, SCR_W - 16, y);
    }
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE, COL_TILE_BG);
    tft.drawString(title, SCR_W / 2, y);
}

// Small centred button-hint line at the very bottom of list screens.
static void draw_footer_hint(const char* hint)
{
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(1); tft.setTextColor(COL_UNIT, TFT_BLACK);
    tft.drawString(hint, SCR_W / 2, SCR_H - 6);
}

// ── Main menu & mode select ────────────────────────────────────────────────

static const struct { const char* label; uint16_t accent; } MENU_ROWS[4] = {
    { "Start Session", TFT_GREEN  },
    { "Shot History",  TFT_YELLOW },
    { "Settings",      TFT_CYAN   },
    { "Shut Down",     TFT_RED    },
};
static const struct { const char* label; uint16_t accent; } MODE_ROWS[4] = {
    { "Practice Range", TFT_GREEN  },
    { "On Course",      TFT_CYAN   },
    { "Speed Training", TFT_YELLOW },
    { "Back",           TFT_WHITE  },
};

void ui_menu_draw(int sel)
{
    tft.fillScreen(TFT_BLACK);
    draw_screen_header("OpenScope", MENU_HDR_H, FW_VERSION);
    for (int i = 0; i < 4; i++)
        draw_menu_row(i, MENU_ROWS[i].label, MENU_ROWS[i].accent,
                      MENU_HDR_H, MENU_ROW_H, i == sel);
    draw_footer_hint("UP/DOWN: SELECT   OK: OPEN   HOLD OK: POWER OFF");
}

void ui_menu_select(int old_sel, int new_sel)
{
    draw_menu_row(old_sel, MENU_ROWS[old_sel].label, MENU_ROWS[old_sel].accent,
                  MENU_HDR_H, MENU_ROW_H, false);
    draw_menu_row(new_sel, MENU_ROWS[new_sel].label, MENU_ROWS[new_sel].accent,
                  MENU_HDR_H, MENU_ROW_H, true);
}

void ui_mode_draw(int sel)
{
    tft.fillScreen(TFT_BLACK);
    draw_screen_header("Select Mode", MENU_HDR_H);
    for (int i = 0; i < 4; i++)
        draw_menu_row(i, MODE_ROWS[i].label, MODE_ROWS[i].accent,
                      MENU_HDR_H, MENU_ROW_H, i == sel);
}

void ui_mode_select(int old_sel, int new_sel)
{
    draw_menu_row(old_sel, MODE_ROWS[old_sel].label, MODE_ROWS[old_sel].accent,
                  MENU_HDR_H, MENU_ROW_H, false);
    draw_menu_row(new_sel, MODE_ROWS[new_sel].label, MODE_ROWS[new_sel].accent,
                  MENU_HDR_H, MENU_ROW_H, true);
}

// ── Session menu ───────────────────────────────────────────────────────────
// Opened with OK during a session. The Layout row's label depends on the
// current layout, so the labels are stashed for ui_smenu_select's redraws.

static char s_smenu_layout[24];
static const char* smenu_label(int i)
{
    static const char* fixed[SMENU_N_ROWS] =
        { "Resume", "Change Club", nullptr, "Settings", "End Session" };
    return (i == 2) ? s_smenu_layout : fixed[i];
}
static const uint16_t SMENU_ACCENT[SMENU_N_ROWS] =
    { TFT_GREEN, TFT_CYAN, TFT_YELLOW, TFT_CYAN, TFT_RED };

void ui_smenu_draw(int sel, UiLayout layout)
{
    snprintf(s_smenu_layout, sizeof(s_smenu_layout), "Layout: %s",
             layout == LAYOUT_LARGE_DIGIT ? "Large Digit" : "Advanced");
    tft.fillScreen(TFT_BLACK);
    draw_screen_header("Session Menu", MENU_HDR_H);
    for (int i = 0; i < SMENU_N_ROWS; i++)
        draw_menu_row(i, smenu_label(i), SMENU_ACCENT[i],
                      MENU_HDR_H, SMENU_ROW_H, i == sel);
    draw_footer_hint("UP/DOWN: SELECT   OK: CONFIRM");
}

void ui_smenu_select(int old_sel, int new_sel)
{
    draw_menu_row(old_sel, smenu_label(old_sel), SMENU_ACCENT[old_sel],
                  MENU_HDR_H, SMENU_ROW_H, false);
    draw_menu_row(new_sel, smenu_label(new_sel), SMENU_ACCENT[new_sel],
                  MENU_HDR_H, SMENU_ROW_H, true);
}

// ── Club picker ────────────────────────────────────────────────────────────
// A vertical list; UP/DOWN move the highlight (the view follows), OK selects.
// `scroll` is the first visible club index; the *current* club is marked.

static void draw_picker_row(int idx, int scroll, int club_idx, bool selected)
{
    const int r = idx - scroll;
    if (r < 0 || r >= PICK_ROWS) return;
    const int  y      = PICK_HDR_H + r * PICK_ROW_H;
    const bool active = (idx == club_idx);

    tft.fillRect(0, y, SCR_W, PICK_ROW_H, TFT_BLACK);
    if (active)
        tft.fillRoundRect(6, y + 3, SCR_W - 12, PICK_ROW_H - 6, BTN_RADIUS, COL_BTN_BG);
    tft.drawFastHLine(0, y, SCR_W, COL_DIV);

    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(active ? s_label_col : TFT_WHITE,
                     active ? COL_BTN_BG : TFT_BLACK);
    tft.drawString(CLUBS[idx].abbr, 20, y + PICK_ROW_H / 2);
    tft.drawString(CLUBS[idx].name, 90, y + PICK_ROW_H / 2);
    if (active) {
        tft.setTextDatum(MR_DATUM);
        tft.setTextFont(2); tft.setTextColor(TFT_GREEN, COL_BTN_BG);
        tft.drawString("current", SCR_W - 16, y + PICK_ROW_H / 2);
    }
    if (selected) sel_outline(6, y + 3, SCR_W - 12, PICK_ROW_H - 6, BTN_RADIUS);
}

void ui_picker_draw(int club_idx, int scroll, int sel)
{
    tft.fillScreen(TFT_BLACK);
    draw_screen_header("Select Club", PICK_HDR_H, "OK: select");

    for (int r = 0; r < PICK_ROWS; r++) {
        int idx = scroll + r;
        if (idx >= NUM_CLUBS) break;
        draw_picker_row(idx, scroll, club_idx, idx == sel);
    }

    // Scroll hint arrows when there is more above/below.
    tft.setTextDatum(MC_DATUM); tft.setTextFont(2);
    tft.setTextColor(COL_UNIT, TFT_BLACK);
    if (scroll > 0)                     tft.drawString("UP: more", SCR_W - 60, PICK_HDR_H + 10);
    if (scroll + PICK_ROWS < NUM_CLUBS) tft.drawString("DOWN: more", SCR_W - 60, SCR_H - 12);
}

void ui_picker_select(int old_sel, int new_sel, int scroll, int club_idx)
{
    draw_picker_row(old_sel, scroll, club_idx, false);
    draw_picker_row(new_sel, scroll, club_idx, true);
}

// ─── Shot history ─────────────────────────────────────────────────────────────
// Newest-first table of the persisted shot log; UP/DOWN page the list.
// `scroll` is the newest-first index of the top visible row.

// Right edges of the four value columns (font 2, MR datum).
#define HCOL_BALL   216
#define HCOL_SMASH  300
#define HCOL_CARRY  384
#define HCOL_TOTAL  (SCR_W - 12)

void ui_history_draw(const ShotRecord* shots, int count, int scroll,
                     bool use_mph)
{
    tft.fillScreen(TFT_BLACK);

    draw_screen_header("Shot History", HIST_HDR_H, "OK: back");

    if (count == 0) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(4); tft.setTextColor(COL_UNIT, TFT_BLACK);
        tft.drawString("No shots yet", SCR_W / 2, SCR_H / 2 - 10);
        tft.setTextFont(2);
        tft.drawString("Hit some balls and they'll show up here",
                       SCR_W / 2, SCR_H / 2 + 18);
        return;
    }

    // Column labels.
    char hdr[16];
    tft.setTextFont(2); tft.setTextColor(COL_UNIT, TFT_BLACK);
    const int cy = HIST_HDR_H + HIST_COL_H / 2;
    tft.setTextDatum(ML_DATUM);
    tft.drawString("#",    16, cy);
    tft.drawString("CLUB", 60, cy);
    tft.setTextDatum(MR_DATUM);
    snprintf(hdr, sizeof(hdr), "BALL %s", speed_unit(use_mph));
    tft.drawString(hdr, HCOL_BALL, cy);
    tft.drawString("SMASH", HCOL_SMASH, cy);
    snprintf(hdr, sizeof(hdr), "CARRY %s", dist_unit(use_mph));
    tft.drawString(hdr, HCOL_CARRY, cy);
    snprintf(hdr, sizeof(hdr), "TOTAL %s", dist_unit(use_mph));
    tft.drawString(hdr, HCOL_TOTAL, cy);
    tft.drawFastHLine(0, HIST_HDR_H + HIST_COL_H - 1, SCR_W, COL_DIV);

    // Rows, newest first: visible row r shows shots[count - 1 - (scroll + r)].
    char buf[12];
    for (int r = 0; r < HIST_ROWS; r++) {
        const int n = scroll + r;            // newest-first index
        if (n >= count) break;
        const ShotRecord& s = shots[count - 1 - n];
        const int y  = HIST_HDR_H + HIST_COL_H + r * HIST_ROW_H;
        const int ry = y + HIST_ROW_H / 2;
        // Zebra striping — much easier to track a row across six columns.
        const uint16_t bg = (r & 1) ? TFT_BLACK : COL_TILE_BG;
        if (bg != TFT_BLACK) tft.fillRect(0, y, SCR_W, HIST_ROW_H, bg);

        tft.setTextFont(2);
        tft.setTextDatum(ML_DATUM);
        snprintf(buf, sizeof(buf), "%d", count - n);     // shot number, 1 = oldest
        tft.setTextColor(COL_UNIT, bg);
        tft.drawString(buf, 16, ry);
        tft.setTextColor(s_label_col, bg);
        tft.drawString(CLUBS[s.club].abbr, 60, ry);

        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(TFT_WHITE, bg);
        snprintf(buf, sizeof(buf), "%.0f", disp_speed(s.ball_kmh, use_mph));
        tft.drawString(buf, HCOL_BALL, ry);

        if (s.club_kmh > 0.0f) {
            snprintf(buf, sizeof(buf), "%.2f", s.ball_kmh / s.club_kmh);
            tft.setTextColor(TFT_GREEN, bg);
        } else {
            snprintf(buf, sizeof(buf), "--");
            tft.setTextColor(COL_UNIT, bg);
        }
        tft.drawString(buf, HCOL_SMASH, ry);

        tft.setTextColor(TFT_WHITE, bg);
        snprintf(buf, sizeof(buf), "%.0f", disp_dist(s.carry_m, use_mph));
        tft.drawString(buf, HCOL_CARRY, ry);
        snprintf(buf, sizeof(buf), "%.0f", disp_dist(s.total_m, use_mph));
        tft.drawString(buf, HCOL_TOTAL, ry);
    }

    // Scroll hints when there is more above/below.
    tft.setTextDatum(MC_DATUM); tft.setTextFont(2);
    tft.setTextColor(COL_UNIT, TFT_BLACK);
    if (scroll > 0)
        tft.drawString("UP: newer", 60, HIST_HDR_H + HIST_COL_H + 10);
    if (scroll + HIST_ROWS < count)
        tft.drawString("DOWN: older", 60, SCR_H - 10);
}

// ─── Settings screen ──────────────────────────────────────────────────────────
// Row labels/values are stashed at draw time so ui_settings_select can redraw
// individual rows without the caller's state.

static const char* SET_LABELS[SET_N_ROWS] = {
    "Units", "Color", "Layout", "Reset Stats", "Clear History",
    "Radar Cal.", "Back"
};
static char s_set_values[SET_N_ROWS][16];
static int  s_set_done_row = -1;

static void draw_settings_row(int i, bool selected)
{
    const int y = SET_HDR_H + i * SET_ROW_H;
    tft.fillRoundRect(4, y + 3, SCR_W - 8, SET_ROW_H - 6, BTN_RADIUS / 2, COL_BTN_BG);
    tft.fillRoundRect(4, y + 3, 6, SET_ROW_H - 6, BTN_RADIUS / 2, s_label_col);

    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE, COL_BTN_BG);
    tft.drawString(SET_LABELS[i], 22, y + SET_ROW_H / 2);

    uint16_t vc = (i == s_set_done_row) ? TFT_GREEN : TFT_CYAN;
    tft.setTextColor(vc, COL_BTN_BG);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(s_set_values[i], SCR_W - 16, y + SET_ROW_H / 2);

    if (selected) sel_outline(4, y + 3, SCR_W - 8, SET_ROW_H - 6, BTN_RADIUS / 2);
}

void ui_settings_draw(int club_idx, bool use_mph, bool blue_theme,
                      UiLayout layout, int sel, int done_row)
{
    tft.fillScreen(TFT_BLACK);
    draw_screen_header("Settings", SET_HDR_H, "OK: toggle / open");

    // Values mirror the toggles handled in main.cpp's settings_loop().
    s_set_done_row = done_row;
    snprintf(s_set_values[0], 16, "%s", use_mph ? "Mph/Yds" : "Kmh/m");
    snprintf(s_set_values[1], 16, "%s", blue_theme ? "Blue" : "Black");
    snprintf(s_set_values[2], 16, "%s",
             layout == LAYOUT_LARGE_DIGIT ? "Large Digit" : "Advanced");
    snprintf(s_set_values[3], 16, "%s",
             done_row == 3 ? "Done!" : CLUBS[club_idx].name);
    snprintf(s_set_values[4], 16, "%s", done_row == 4 ? "Done!" : "\x10");
    snprintf(s_set_values[5], 16, "\x10");     // ▶  enter radar calibration
    snprintf(s_set_values[6], 16, "\x11");     // ◀  back to previous screen

    for (int i = 0; i < SET_N_ROWS; i++)
        draw_settings_row(i, i == sel);
}

void ui_settings_select(int old_sel, int new_sel)
{
    draw_settings_row(old_sel, false);
    draw_settings_row(new_sel, true);
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
    tft.drawString("UP/DOWN: adjust   OK: save", SCR_W - 8, 15);

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

    // Bottom bar — the three button bindings on this screen.
    draw_button(0,         BAR_Y, COL_W, BAR_H, "DOWN: -10", COL_BTN_BRD, TFT_CYAN);
    draw_button(COL_W,     BAR_Y, COL_W, BAR_H, "OK: SAVE",  COL_BTN_BRD, TFT_GREEN);
    draw_button(COL_W * 2, BAR_Y, COL_W, BAR_H, "UP: +10",   COL_BTN_BRD, TFT_CYAN);
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
