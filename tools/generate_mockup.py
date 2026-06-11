#!/usr/bin/env python3
"""
generate_mockup.py — Render the OpenScope v0.9 UI mockup from firmware constants.

Parses src/config.h for colour values and layout constants so the mockup stays
in sync with the firmware automatically. The UI is fully touch-driven (LM1
style): the only physical control is Power.

Screens rendered (mirrors display.cpp):
    Boot Splash · Main Menu · Mode Select · Advanced layout · Large-Digit
    layout · Club Picker · Shot History · Speed Training · Settings ·
    Calibration

Usage (from repo root):
    python tools/generate_mockup.py
Output:
    docs/openscope-ui.png
"""

import re, os, math, random
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT   = Path(__file__).parent.parent
OUT    = ROOT / "docs" / "openscope-ui.png"
CLUBS  = ["D","3W","5W","3I","4I","5I","6I","7I","8I","9I","PW","GW","SW","LW"]
NAMES  = ["Driver","3-Wood","5-Wood","3-Iron","4-Iron","5-Iron","6-Iron",
          "7-Iron","8-Iron","9-Iron","PW","GW","SW","LW"]

# ─── Parse src/config.h ───────────────────────────────────────────────────────

def parse_defines(path):
    d = {}
    for line in open(path):
        m = re.match(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)(?:\s|$)", line)
        if m:
            v = m.group(2)
            d[m.group(1)] = int(v, 16) if v.startswith("0x") else int(v)
    return d

cfg        = parse_defines(ROOT / "src" / "config.h")
SCR_W      = cfg["SCR_W"]        # 480
SCR_H      = cfg["SCR_H"]        # 320
COL_W      = SCR_W // 3          # 160
ROW_H      = cfg["ROW_H"]        # 130
MINI_ROW_H = cfg["MINI_ROW_H"]   # 60

# Touch-layout constants. The ones defined as expressions in config.h (PILL_X,
# BAR_Y, …) aren't captured by the simple #define parser, so derive them here
# exactly as the firmware macros do.
BAR_H      = cfg.get("BAR_H", 60)
BAR_Y      = SCR_H - BAR_H
PILL_W     = cfg.get("PILL_W", 104)
PILL_H     = cfg.get("PILL_H", 62)
PILL_X     = COL_W * 2 + (COL_W - PILL_W) // 2
PILL_Y     = ROW_H + (ROW_H - PILL_H) // 2
LPILL_X    = SCR_W - PILL_W - 16
LPILL_Y    = cfg.get("LPILL_Y", 100)
BTN_R      = cfg.get("BTN_RADIUS", 8)

SET_HDR_H  = cfg.get("SET_HDR_H", 48)
SET_ROW_H  = cfg.get("SET_ROW_H", 45)
SET_N_ROWS = cfg.get("SET_N_ROWS", 6)

MENU_HDR_H = cfg.get("MENU_HDR_H", 56)
MENU_ROW_H = cfg.get("MENU_ROW_H", 72)
MENU_GAP   = cfg.get("MENU_ROW_GAP", 8)

PICK_HDR_H = cfg.get("PICK_HDR_H", 48)
PICK_ROW_H = cfg.get("PICK_ROW_H", 52)
PICK_ROWS  = (SCR_H - PICK_HDR_H) // PICK_ROW_H

HIST_HDR_H = cfg.get("HIST_HDR_H", 48)
HIST_COL_H = cfg.get("HIST_COL_H", 24)
HIST_ROW_H = cfg.get("HIST_ROW_H", 34)
HIST_ROWS  = (SCR_H - HIST_HDR_H - HIST_COL_H) // HIST_ROW_H

FW_VERSION = (re.search(r'#define\s+FW_VERSION\s+"([^"]+)"',
                        open(ROOT / "src" / "config.h").read()) or [None, "v0.9"])[1]

# ─── Colour helpers ───────────────────────────────────────────────────────────

def rgb565(c):
    r = ((c >> 11) & 0x1F) * 255 // 31
    g = ((c >> 5)  & 0x3F) * 255 // 63
    b = (c         & 0x1F) * 255 // 31
    return (r, g, b)

# TFT_eSPI built-in palette
BG     = (0,   0,   0  )
WHITE  = (240, 240, 240)
CYAN   = (0,   220, 220)
GREEN  = (0,   230, 118)
YELLOW = (255, 214, 0  )
RED    = (220, 30,  30 )
NAVY   = (0,   0,   100)

# Colours from config.h
DIV       = rgb565(cfg["COL_DIV"])
UNIT_C    = rgb565(cfg["COL_UNIT"])
DIM       = rgb565(cfg["COL_DIM"])
CAL_HDR   = rgb565(cfg["COL_CAL_HDR"])
BTN_BG    = rgb565(cfg.get("COL_BTN_BG",  0x18E3))
BTN_BRD   = rgb565(cfg.get("COL_BTN_BRD", 0x4208))
TILE_BG   = rgb565(cfg.get("COL_TILE_BG", 0x0841))

# Theme label colours (Black theme = white labels, Blue theme = cyan labels)
LABEL_BLACK = rgb565(cfg.get("COL_LABEL_BLACK", 0xFFFF))
LABEL_BLUE  = rgb565(cfg.get("COL_LABEL_BLUE",  0x35DE))

# ─── Font loader ──────────────────────────────────────────────────────────────

def load_font(size):
    candidates = [
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Monaco.ttf",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/Library/Fonts/Courier New.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
    ]
    for p in candidates:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                pass
    return ImageFont.load_default()

F_HUGE  = load_font(74)   # Large-Digit / Speed value  (Font 8 in TFT_eSPI)
F_NUM   = load_font(50)   # Advanced tile numbers       (Font 7)
F_PILL  = load_font(26)   # club pill abbr              (Font 4)
F_TITLE = load_font(20)   # screen titles / big labels  (Font 4)
F_LABEL = load_font(14)   # tile labels                 (Font 4)
F_UNIT  = load_font(11)   # tile units / hints          (Font 2)
F_SMALL = load_font(10)   # footnotes
F_MED   = load_font(17)   # calibration / settings rows (Font 4)
F_HDR   = load_font(12)   # header bars                 (Font 2)
F_BTN   = load_font(18)   # touch button labels

# ─── Drawing primitives ───────────────────────────────────────────────────────

def tc(draw, text, cx, y, font, color):
    bb = draw.textbbox((0, 0), text, font=font)
    draw.text((cx - (bb[2]-bb[0]) // 2, y), text, font=font, fill=color)

def mc(draw, text, cx, cy, font, color):
    bb = draw.textbbox((0, 0), text, font=font)
    w, h = bb[2]-bb[0], bb[3]-bb[1]
    draw.text((cx - w // 2, cy - h // 2 - bb[1]), text, font=font, fill=color)

def tl(draw, text, x, y, font, color):
    draw.text((x, y), text, font=font, fill=color)

def ml(draw, text, x, cy, font, color):
    bb = draw.textbbox((0, 0), text, font=font)
    draw.text((x, cy - (bb[3]-bb[1]) // 2 - bb[1]), text, font=font, fill=color)

def mr(draw, text, x, cy, font, color):
    bb = draw.textbbox((0, 0), text, font=font)
    w, h = bb[2]-bb[0], bb[3]-bb[1]
    draw.text((x - w, cy - h // 2 - bb[1]), text, font=font, fill=color)

def rounded_rect(draw, x, y, w, h, r, fill=None, outline=None, width=1):
    try:
        draw.rounded_rectangle([(x, y), (x+w-1, y+h-1)], radius=r,
                                fill=fill, outline=outline, width=width)
    except AttributeError:
        if fill:    draw.rectangle([x, y, x+w-1, y+h-1], fill=fill)
        if outline: draw.rectangle([x, y, x+w-1, y+h-1], outline=outline, width=width)

def grid_lines(draw):
    draw.line([(COL_W, 0), (COL_W, SCR_H)],       fill=DIV)
    draw.line([(COL_W*2, 0), (COL_W*2, SCR_H)],   fill=DIV)
    draw.line([(0, ROW_H), (SCR_W, ROW_H)],        fill=DIV)
    draw.line([(0, ROW_H*2), (SCR_W, ROW_H*2)],    fill=DIV)

def chevron(draw, x, cy, color):
    """Left-pointing back chevron with a hot-spot near (x, cy)."""
    draw.line([(x+10, cy-9), (x, cy), (x+10, cy+9)], fill=color, width=2)

def gear(draw, cx, cy, color):
    draw.ellipse([cx-9, cy-9, cx+9, cy+9], outline=color, width=1)
    draw.ellipse([cx-3, cy-3, cx+3, cy+3], fill=color)
    for a in range(0, 360, 45):
        r = a * 3.14159 / 180
        draw.line([(cx+int(math.cos(r)*9),  cy+int(math.sin(r)*9)),
                   (cx+int(math.cos(r)*13), cy+int(math.sin(r)*13))], fill=color)

def draw_tile(draw, col, row, label, number, unit, num_color, label_col,
              dimmed=False):
    cx = col * COL_W + COL_W // 2
    y0 = row * ROW_H
    lc = DIM if dimmed else label_col
    nc = DIM if dimmed else num_color
    uc = DIM if dimmed else UNIT_C
    tc(draw, label,  cx, y0 + 10, F_LABEL, lc)
    tc(draw, number, cx, y0 + 38, F_NUM,   nc)
    tc(draw, unit,   cx, y0 + 98, F_UNIT,  uc)

def draw_club_pill(draw, x, y, w, h, abbr, label_col, tap_hint=False):
    cx, cy = x + w // 2, y + h // 2
    rounded_rect(draw, x, y, w, h, BTN_R, fill=BTN_BG)
    rounded_rect(draw, x, y, w, h, BTN_R, outline=label_col, width=2)
    mc(draw, abbr, cx, cy - 1, F_PILL, WHITE)
    if tap_hint:
        tc(draw, "TAP TO CHANGE", cx, y + h + 4, F_SMALL, UNIT_C)

def session_nav(draw, hint, label_col):
    by = BAR_Y + BAR_H // 2 + 1
    # Filled "dock" strip, matching draw_session_nav in display.cpp.
    draw.rectangle([0, BAR_Y + 1, SCR_W, SCR_H], fill=TILE_BG)
    draw.line([(0, BAR_Y), (SCR_W, BAR_Y)], fill=DIV)
    chevron(draw, 14, by, label_col)
    ml(draw, "Back", 34, by, F_HDR, label_col)
    mc(draw, hint, SCR_W // 2, by, F_HDR, UNIT_C)
    gear(draw, SCR_W - 24, by, label_col)
    mr(draw, "Menu", SCR_W - 42, by, F_HDR, label_col)

def screen_header(draw, title, label_col, back=True, h=None,
                  right=None, right_col=CYAN):
    """Dark card strip with a themed accent underline (display.cpp style)."""
    if h is None: h = MENU_HDR_H
    draw.rectangle([0, 0, SCR_W, h - 2], fill=TILE_BG)
    draw.line([(0, h - 2), (SCR_W, h - 2)], fill=label_col)
    draw.line([(0, h - 1), (SCR_W, h - 1)], fill=DIV)
    cy = (h - 2) // 2
    if back:
        chevron(draw, 14, cy, label_col)
        ml(draw, "Back", 34, cy, F_HDR, label_col)
    if right:
        mr(draw, right, SCR_W - 16, cy, F_HDR, right_col)
    mc(draw, title, SCR_W // 2, cy, F_TITLE, WHITE)

def menu_row(draw, i, label, accent):
    y  = MENU_HDR_H + i * MENU_ROW_H
    h  = MENU_ROW_H - MENU_GAP
    cy = y + h // 2 + 2
    rounded_rect(draw, 8, y + 4, SCR_W - 16, h - 4, BTN_R, fill=BTN_BG)
    rounded_rect(draw, 8, y + 4, 6, h - 4, BTN_R, fill=accent)
    ml(draw, label, 28, cy, F_TITLE, WHITE)
    # "›" affordance chevron on the right.
    draw.line([(SCR_W-34, cy-8), (SCR_W-26, cy), (SCR_W-34, cy+8)],
              fill=UNIT_C, width=2)

# ─── Screen: Boot Splash ──────────────────────────────────────────────────────

def screen_splash(label_col=LABEL_BLACK):
    img = Image.new("RGB", (SCR_W, SCR_H), BG); d = ImageDraw.Draw(img)
    mc(d, "OpenScope", SCR_W // 2, SCR_H // 2 - 30, load_font(44), WHITE)
    d.line([(SCR_W//2 - 92, SCR_H//2 + 6), (SCR_W//2 + 92, SCR_H//2 + 6)],
           fill=label_col)
    tc(d, "GOLF LAUNCH MONITOR", SCR_W // 2, SCR_H // 2 + 20, F_HDR, UNIT_C)
    tc(d, FW_VERSION, SCR_W // 2, SCR_H // 2 + 42, F_HDR, UNIT_C)
    return img

# ─── Screen: Main Menu ────────────────────────────────────────────────────────

def screen_menu(label_col=LABEL_BLACK):
    img = Image.new("RGB", (SCR_W, SCR_H), BG); d = ImageDraw.Draw(img)
    screen_header(d, "OpenScope", label_col, back=False)
    menu_row(d, 0, "Start Session", GREEN)
    menu_row(d, 1, "Shot History",  YELLOW)
    menu_row(d, 2, "Settings",      CYAN)
    menu_row(d, 3, "Shut Down",     RED)
    tc(d, FW_VERSION, SCR_W // 2, SCR_H - 12, F_SMALL, UNIT_C)
    return img

# ─── Screen: Mode Select ──────────────────────────────────────────────────────

def screen_mode(label_col=LABEL_BLACK):
    img = Image.new("RGB", (SCR_W, SCR_H), BG); d = ImageDraw.Draw(img)
    screen_header(d, "Select Mode", label_col, back=True)
    menu_row(d, 0, "Practice Range", GREEN)
    menu_row(d, 1, "On Course",      CYAN)
    menu_row(d, 2, "Speed Training", YELLOW)
    return img

# ─── Screen: Advanced layout (ready, Black theme) ─────────────────────────────

def screen_advanced(label_col=LABEL_BLACK):
    img = Image.new("RGB", (SCR_W, SCR_H), BG); d = ImageDraw.Draw(img)
    grid_lines(d)
    draw_tile(d, 0, 0, "Club",  "--",  "km/h", WHITE, label_col, dimmed=True)
    draw_tile(d, 1, 0, "Ball",  "--",  "km/h", WHITE, label_col, dimmed=True)
    draw_tile(d, 2, 0, "Smash", "--",  "",     WHITE, label_col, dimmed=True)
    draw_tile(d, 0, 1, "Avg",   "182", "m",    WHITE, label_col)
    draw_tile(d, 1, 1, "Best",  "201", "m",    GREEN, label_col)
    draw_club_pill(d, PILL_X, PILL_Y, PILL_W, PILL_H, "7I", label_col)
    session_nav(d, "SWING WHEN READY    SWIPE L/R: LAYOUT", label_col)
    return img

# ─── Screen: Large-Digit layout (result, Blue theme) ──────────────────────────

def screen_large(label_col=LABEL_BLUE):
    img = Image.new("RGB", (SCR_W, SCR_H), BG); d = ImageDraw.Draw(img)
    area_w = SCR_W - PILL_W - 40
    cx = area_w // 2 + 8
    tc(d, "Total Distance", cx, 24, F_TITLE, label_col)
    mc(d, "209", cx, 130, F_HUGE, WHITE)
    tc(d, "m",   cx, 188, F_TITLE, UNIT_C)
    draw_club_pill(d, LPILL_X, LPILL_Y, PILL_W, PILL_H, "7I", label_col, tap_hint=True)
    session_nav(d, "TAP TO CONTINUE   SWIPE U/D: METRIC", label_col)
    return img

# ─── Screen: Club Picker ──────────────────────────────────────────────────────

def screen_picker(label_col=LABEL_BLACK):
    img = Image.new("RGB", (SCR_W, SCR_H), BG); d = ImageDraw.Draw(img)
    active = 7                                   # 7-Iron is current
    scroll = max(0, min(active - PICK_ROWS // 2, len(CLUBS) - PICK_ROWS))
    screen_header(d, "Select Club", label_col, back=True, h=PICK_HDR_H)
    for r in range(PICK_ROWS):
        idx = scroll + r
        if idx >= len(CLUBS): break
        y = PICK_HDR_H + r * PICK_ROW_H
        is_active = (idx == active)
        if is_active:
            rounded_rect(d, 6, y + 3, SCR_W - 12, PICK_ROW_H - 6, BTN_R, fill=BTN_BG)
        d.line([(0, y), (SCR_W, y)], fill=DIV)
        ml(d, CLUBS[idx], 20, y + PICK_ROW_H // 2, F_MED,
           label_col if is_active else WHITE)
        ml(d, NAMES[idx], 90, y + PICK_ROW_H // 2, F_MED,
           label_col if is_active else WHITE)
        if is_active:
            mr(d, "current", SCR_W - 16, y + PICK_ROW_H // 2, F_HDR, GREEN)
    if scroll > 0:
        tc(d, "swipe up", SCR_W - 60, PICK_HDR_H + 6, F_HDR, UNIT_C)
    if scroll + PICK_ROWS < len(CLUBS):
        tc(d, "swipe down", SCR_W - 60, SCR_H - 16, F_HDR, UNIT_C)
    return img

# ─── Screen: Speed Training ───────────────────────────────────────────────────

def screen_speed(label_col=LABEL_BLACK):
    img = Image.new("RGB", (SCR_W, SCR_H), BG); d = ImageDraw.Draw(img)
    tc(d, "SWING SPEED", SCR_W // 2, 26, F_TITLE, label_col)
    mc(d, "118", SCR_W // 2, 140, F_HUGE, WHITE)
    tc(d, "km/h", SCR_W // 2, 198, F_TITLE, UNIT_C)
    by = BAR_Y + BAR_H // 2 + 1
    d.rectangle([0, BAR_Y + 1, SCR_W, SCR_H], fill=TILE_BG)
    d.line([(0, BAR_Y), (SCR_W, BAR_Y)], fill=DIV)
    chevron(d, 14, by, label_col)
    ml(d, "Back", 34, by, F_HDR, label_col)
    mc(d, "SWING TO MEASURE", SCR_W // 2, by, F_HDR, UNIT_C)
    return img

# ─── Screen: Shot History ─────────────────────────────────────────────────────

def screen_history(label_col=LABEL_BLACK):
    img = Image.new("RGB", (SCR_W, SCR_H), BG); d = ImageDraw.Draw(img)
    screen_header(d, "Shot History", label_col, back=True, h=HIST_HDR_H,
                  right="Clear", right_col=RED)

    # Column labels (positions mirror ui_history_draw in display.cpp).
    C_BALL, C_SMASH, C_CARRY, C_TOTAL = 216, 300, 384, SCR_W - 12
    cy = HIST_HDR_H + HIST_COL_H // 2
    ml(d, "#",    16, cy, F_HDR, UNIT_C)
    ml(d, "CLUB", 60, cy, F_HDR, UNIT_C)
    mr(d, "BALL km/h",  C_BALL,  cy, F_HDR, UNIT_C)
    mr(d, "SMASH",      C_SMASH, cy, F_HDR, UNIT_C)
    mr(d, "CARRY m",    C_CARRY, cy, F_HDR, UNIT_C)
    mr(d, "TOTAL m",    C_TOTAL, cy, F_HDR, UNIT_C)
    d.line([(0, HIST_HDR_H + HIST_COL_H - 1), (SCR_W, HIST_HDR_H + HIST_COL_H - 1)],
           fill=DIV)

    # Sample shots, newest first (zebra striping like the firmware).
    shots = [   # num, club, ball, smash, carry, total
        (12, "7I", "172", "1.34", "139", "147"),
        (11, "7I", "168", "1.31", "136", "144"),
        (10, "7I", "175", "--",   "141", "150"),
        ( 9, "D",  "228", "1.42", "204", "224"),
        ( 8, "D",  "221", "1.39", "198", "218"),
        ( 7, "PW", "138", "1.21", "104", "107"),
        ( 6, "PW", "133", "1.24", "100", "103"),
    ]
    for r, (num, club, ball, smash, carry, total) in enumerate(shots[:HIST_ROWS]):
        y  = HIST_HDR_H + HIST_COL_H + r * HIST_ROW_H
        ry = y + HIST_ROW_H // 2
        if r % 2 == 0:
            d.rectangle([0, y, SCR_W, y + HIST_ROW_H - 1], fill=TILE_BG)
        ml(d, str(num), 16, ry, F_HDR, UNIT_C)
        ml(d, club,     60, ry, F_HDR, label_col)
        mr(d, ball,  C_BALL,  ry, F_HDR, WHITE)
        mr(d, smash, C_SMASH, ry, F_HDR, GREEN if smash != "--" else UNIT_C)
        mr(d, carry, C_CARRY, ry, F_HDR, WHITE)
        mr(d, total, C_TOTAL, ry, F_HDR, WHITE)
    tc(d, "swipe up", 60, SCR_H - 16, F_HDR, UNIT_C)
    return img

# ─── Screen: Settings ─────────────────────────────────────────────────────────

def screen_settings(label_col=LABEL_BLACK):
    img = Image.new("RGB", (SCR_W, SCR_H), BG); d = ImageDraw.Draw(img)
    screen_header(d, "Settings", label_col, back=True, h=SET_HDR_H)

    labels = ["Units", "Color", "Layout", "Reset Stats", "Radar Cal.", "Touch Cal."]
    values = ["Kmh/m", "Black", "Advanced", "7I", "►", "►"]
    for i, (label, value) in enumerate(zip(labels, values)):
        y = SET_HDR_H + i * SET_ROW_H
        rounded_rect(d, 4, y + 3, SCR_W - 8, SET_ROW_H - 6, BTN_R // 2, fill=BTN_BG)
        rounded_rect(d, 4, y + 3, 6, SET_ROW_H - 6, BTN_R // 2, fill=label_col)
        ml(d, label, 22,         y + SET_ROW_H // 2, F_MED, WHITE)
        mr(d, value, SCR_W - 16, y + SET_ROW_H // 2, F_MED, CYAN)
    return img

# ─── Screen: Calibration ──────────────────────────────────────────────────────

def screen_calibration(label_col=LABEL_BLACK):
    img = Image.new("RGB", (SCR_W, SCR_H), BG); d = ImageDraw.Draw(img)
    HDR_H = 30
    d.rectangle([0, 0, SCR_W, HDR_H], fill=CAL_HDR)
    ml(d, "CALIBRATION MODE",      10,      HDR_H // 2, F_HDR, YELLOW)
    mr(d, "tap the buttons below", SCR_W-8, HDR_H // 2, F_HDR, WHITE)

    SPEC_X, SPEC_Y, SPEC_W, SPEC_H = 10, HDR_H+4, SCR_W-20, 120
    d.rectangle([SPEC_X, SPEC_Y, SPEC_X+SPEC_W, SPEC_Y+SPEC_H], fill=(4, 12, 12))
    random.seed(42)
    noise_floor, max_seen, threshold = 14.0, 312.7, 80.0
    scale = (SPEC_H - 4) / max_seen
    for x in range(SPEC_W):
        frac = x / SPEC_W
        mag = noise_floor * (0.7 + 0.6 * random.random())
        if 0.38 < frac < 0.46:
            mag = max_seen * max(0, 1 - abs(frac - 0.42) / 0.04 * 0.6)
        h = min(int(mag * scale), SPEC_H - 1)
        if h <= 0: continue
        color = RED if mag >= threshold else (0, 68, 68)
        d.line([(SPEC_X+x, SPEC_Y+SPEC_H-1), (SPEC_X+x, SPEC_Y+SPEC_H-1-h)], fill=color)
    ty = max(SPEC_Y + SPEC_H - 1 - int(threshold * scale), SPEC_Y)
    d.line([(SPEC_X, ty), (SPEC_X+SPEC_W, ty)], fill=YELLOW)
    px = SPEC_X + int(0.42 * SPEC_W)
    d.line([(px, SPEC_Y), (px, SPEC_Y+6)], fill=WHITE)

    tick_y = SPEC_Y + SPEC_H + 4
    MIN_HZ, MAX_HZ = cfg["MIN_DETECT_HZ"], cfg["MAX_DETECT_HZ"]
    for label, hz in [("2k",2000),("5k",5000),("8k",8000),("12k",12000)]:
        xp = SPEC_X + int((hz - MIN_HZ) / (MAX_HZ - MIN_HZ) * SPEC_W)
        tc(d, label, xp, tick_y, F_SMALL, DIM)

    MY = tick_y + 14
    tl(d, "NOISE FLOOR", 8,   MY,    F_SMALL, UNIT_C); tl(d, "14.2",  8,   MY+12, F_MED, CYAN)
    tl(d, "PEAK MAG",   170,  MY,    F_SMALL, UNIT_C); tl(d, "312.7", 170, MY+12, F_MED, RED)
    tl(d, "MAX SEEN",   332,  MY,    F_SMALL, UNIT_C); tl(d, "312.7", 332, MY+12, F_MED, YELLOW)
    R2Y = MY + 40
    tl(d, "PEAK", 8, R2Y, F_SMALL, UNIT_C)
    tl(d, "4820 Hz  =  107.8 km/h", 56, R2Y, F_SMALL, WHITE)
    R3Y = R2Y + 24
    tl(d, "THRESHOLD", 8,   R3Y,   F_SMALL, UNIT_C); tl(d, "80", 110, R3Y-4, F_MED, YELLOW)
    tl(d, "SUGGESTED", 230, R3Y,   F_SMALL, UNIT_C); tl(d, "58", 340, R3Y-4, F_MED, CYAN)

    def cal_btn(x, label, col):
        rounded_rect(d, x+1, BAR_Y+1, COL_W-2, BAR_H-2, BTN_R, fill=BTN_BG)
        rounded_rect(d, x, BAR_Y, COL_W, BAR_H, BTN_R, outline=BTN_BRD)
        mc(d, label, x + COL_W // 2, BAR_Y + BAR_H // 2, F_BTN, col)
    cal_btn(0,        "-10",  CYAN)
    cal_btn(COL_W,    "SAVE", GREEN)
    cal_btn(COL_W*2,  "+10",  CYAN)
    return img

# ─── Compose final image ──────────────────────────────────────────────────────

GAP = 10

def compose():
    panels = [
        (screen_splash(),       "BOOT SPLASH"),
        (screen_menu(),         "MAIN MENU"),
        (screen_mode(),         "MODE SELECT"),
        (screen_advanced(),     "ADVANCED  (Black theme)"),
        (screen_large(),        "LARGE DIGIT  (Blue theme)"),
        (screen_picker(),       "CLUB PICKER"),
        (screen_history(),      "SHOT HISTORY"),
        (screen_speed(),        "SPEED TRAINING"),
        (screen_settings(),     "SETTINGS"),
        (screen_calibration(),  "CALIBRATION"),
    ]
    cols, rows = 2, (len(panels) + 1) // 2
    cap_h   = 22
    cell_w  = SCR_W
    cell_h  = SCR_H + cap_h
    total_w = cell_w * cols + GAP * (cols + 1)
    total_h = cell_h * rows + GAP * (rows + 1)
    canvas  = Image.new("RGB", (total_w, total_h), (6, 6, 12))
    cdraw   = ImageDraw.Draw(canvas)
    cap_f   = load_font(13)

    for i, (scr, lbl) in enumerate(panels):
        col, row = i % cols, i // cols
        x = GAP + col * (cell_w + GAP)
        y = GAP + row * (cell_h + GAP)
        tl(cdraw, lbl, x + 2, y, cap_f, (150, 150, 165))
        canvas.paste(scr, (x, y + cap_h))

    canvas.save(OUT, "PNG")
    print(f"Saved {OUT}  ({total_w}x{total_h})")

if __name__ == "__main__":
    compose()
