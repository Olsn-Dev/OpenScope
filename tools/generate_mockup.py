#!/usr/bin/env python3
"""
generate_mockup.py — Render the OpenScope UI mockup from firmware constants.

Parses src/config.h for color values and layout constants so the mockup
stays in sync with the firmware automatically. The UI is touch-driven:
navigation is done by tapping on-screen buttons/tiles, with the power
button as the only physical control.

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

# Touch-layout constants — fall back to firmware defaults if config.h
# (e.g. an older button-based revision) does not define them yet.
BAR_H      = cfg.get("BAR_H", 60)
BAR_Y      = cfg.get("BAR_Y", SCR_H - BAR_H)
SET_HDR_H  = cfg.get("SET_HDR_H", 48)
SET_ROW_H  = cfg.get("SET_ROW_H", 50)
SET_N_ROWS = cfg.get("SET_N_ROWS", 4)
SET_DONE_Y = cfg.get("SET_DONE_Y", 256)

# ─── Color helpers ────────────────────────────────────────────────────────────

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

# Colors from config.h
DIV     = rgb565(cfg["COL_DIV"])
UNIT_C  = rgb565(cfg["COL_UNIT"])
DIM     = rgb565(cfg["COL_DIM"])
CAL_HDR = rgb565(cfg["COL_CAL_HDR"])
BTN_BG  = rgb565(cfg.get("COL_BTN_BG",  0x18E3))
BTN_BRD = rgb565(cfg.get("COL_BTN_BRD", 0x4208))

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
    # Fallback — scale up default
    return ImageFont.load_default()

F_NUM   = load_font(50)   # large tile numbers  (Font 7 in TFT_eSPI)
F_LABEL = load_font(14)   # tile labels          (Font 4)
F_UNIT  = load_font(11)   # tile units           (Font 2)
F_SMALL = load_font(10)   # footnotes
F_MED   = load_font(17)   # calibration values
F_BIG   = load_font(32)   # calibration threshold
F_HDR   = load_font(12)   # header bars
F_BTN   = load_font(18)   # touch button labels

# ─── Drawing primitives ───────────────────────────────────────────────────────

def tc(draw, text, cx, y, font, color):
    """Draw text top-center at (cx, y)."""
    bb = draw.textbbox((0, 0), text, font=font)
    w  = bb[2] - bb[0]
    draw.text((cx - w // 2, y), text, font=font, fill=color)

def mc(draw, text, cx, cy, font, color):
    """Draw text middle-center at (cx, cy)."""
    bb = draw.textbbox((0, 0), text, font=font)
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    draw.text((cx - w // 2, cy - h // 2 - bb[1]), text, font=font, fill=color)

def tl(draw, text, x, y, font, color):
    draw.text((x, y), text, font=font, fill=color)

def tr(draw, text, x, y, font, color):
    bb = draw.textbbox((0, 0), text, font=font)
    w  = bb[2] - bb[0]
    draw.text((x - w, y), text, font=font, fill=color)

def ml(draw, text, x, cy, font, color):
    """Draw text middle-left anchored vertically at cy."""
    bb = draw.textbbox((0, 0), text, font=font)
    h  = bb[3] - bb[1]
    draw.text((x, cy - h // 2 - bb[1]), text, font=font, fill=color)

def mr(draw, text, x, cy, font, color):
    """Draw text middle-right anchored at (x, cy)."""
    bb = draw.textbbox((0, 0), text, font=font)
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    draw.text((x - w, cy - h // 2 - bb[1]), text, font=font, fill=color)

def grid_lines(draw):
    draw.line([(COL_W, 0), (COL_W, SCR_H)],       fill=DIV, width=1)
    draw.line([(COL_W*2, 0), (COL_W*2, SCR_H)],   fill=DIV, width=1)
    draw.line([(0, ROW_H), (SCR_W, ROW_H)],        fill=DIV, width=1)
    draw.line([(0, ROW_H*2), (SCR_W, ROW_H*2)],    fill=DIV, width=1)

def draw_button(draw, x, y, w, h, label, text_col):
    """Bordered touch button: dark slate fill + grey border + centered label."""
    draw.rectangle([x+1, y+1, x+w-2, y+h-2], fill=BTN_BG)
    draw.rectangle([x, y, x+w-1, y+h-1], outline=BTN_BRD, width=1)
    mc(draw, label, x + w // 2, y + h // 2, F_BTN, text_col)

def draw_tile(draw, col, row, label, number, unit, num_color, dimmed=False):
    cx = col * COL_W + COL_W // 2
    y0 = row * ROW_H
    lc = DIM if dimmed else CYAN
    nc = DIM if dimmed else num_color
    uc = DIM if dimmed else UNIT_C
    tc(draw, label,  cx, y0 + 10, F_LABEL, lc)
    tc(draw, number, cx, y0 + 38, F_NUM,   nc)
    tc(draw, unit,   cx, y0 + 98, F_UNIT,  uc)

def draw_mini_tile(draw, col, label, value, val_color, dimmed=False):
    cx = col * COL_W + COL_W // 2
    y0 = ROW_H * 2
    lc = DIM if dimmed else UNIT_C
    vc = DIM if dimmed else val_color
    tc(draw, label, cx, y0 + 8,  F_SMALL, lc)
    tc(draw, value, cx, y0 + 20, F_LABEL, vc)

def draw_club_tile(draw, col, row, abbr, tap_hint=False):
    cx = col * COL_W + COL_W // 2
    cy = row * ROW_H + ROW_H // 2
    r  = 42
    draw.ellipse([cx-r, cy-r, cx+r, cy+r],   outline=CYAN, width=1)
    draw.ellipse([cx-r-1, cy-r-1, cx+r+1, cy+r+1], outline=CYAN, width=1)
    mc(draw, abbr, cx, cy, F_LABEL, WHITE)
    if tap_hint:
        tc(draw, "TAP TO CHANGE", cx, cy + 44, F_SMALL, UNIT_C)

# ─── Screen 1 — Ready ─────────────────────────────────────────────────────────

def screen_ready():
    img  = Image.new("RGB", (SCR_W, SCR_H), BG)
    draw = ImageDraw.Draw(img)
    grid_lines(draw)

    # Top row — dimmed (no shot yet)
    draw_tile(draw, 0, 0, "Club",   "--", "km/h", WHITE, dimmed=True)
    draw_tile(draw, 1, 0, "Ball",   "--", "km/h", WHITE, dimmed=True)
    draw_tile(draw, 2, 0, "Launch", "--", "°",     WHITE, dimmed=True)

    # Bottom row — club statistics + tappable club selector
    draw_tile(draw, 0, 1, "Avg",  "182", "m",   WHITE)
    draw_tile(draw, 1, 1, "Best", "201", "m",   GREEN)
    draw_club_tile(draw, 2, 1, "7I", tap_hint=True)

    # Center prompt
    tc(draw, "SWING WHEN READY", SCR_W // 2, ROW_H + 4, F_UNIT, DIM)

    # Bottom action bar — full-width SETTINGS touch button
    draw_button(draw, 0, BAR_Y, SCR_W, BAR_H, "SETTINGS", CYAN)
    return img

# ─── Screen 2 — Result ────────────────────────────────────────────────────────

def screen_result(side_deg=2.3, side_dir="R"):
    img  = Image.new("RGB", (SCR_W, SCR_H), BG)
    draw = ImageDraw.Draw(img)
    grid_lines(draw)

    draw_tile(draw, 0, 0, "Club",   "98",   "km/h", WHITE)
    draw_tile(draw, 1, 0, "Ball",   "152",  "km/h", WHITE)
    draw_tile(draw, 2, 0, "Launch", "19.4", "°",    GREEN)
    draw_tile(draw, 0, 1, "Carry",  "187",  "m",    WHITE)
    draw_tile(draw, 1, 1, "Total",  "209",  "m",    WHITE)
    draw_club_tile(draw, 2, 1, "7I")

    # Mini row — side angle (col 0) + smash (col 1) + tap hint (col 2)
    side_str = "STRAIGHT" if abs(side_deg) < 0.5 else f"{side_dir} {abs(side_deg):.1f}°"
    draw_mini_tile(draw, 0, "SIDE",  side_str, WHITE)
    draw_mini_tile(draw, 1, "SMASH", "1.55",   UNIT_C)

    # Col 2 of the mini row — tap-to-continue hint (any tap dismisses)
    cx = 2 * COL_W + COL_W // 2
    tc(draw, "TAP TO",   cx, ROW_H * 2 + 14, F_UNIT, UNIT_C)
    tc(draw, "CONTINUE", cx, ROW_H * 2 + 32, F_UNIT, UNIT_C)
    return img

# ─── Screen 3 — Settings ─────────────────────────────────────────────────────

def screen_settings():
    img  = Image.new("RGB", (SCR_W, SCR_H), BG)
    draw = ImageDraw.Draw(img)

    # Header bar (navy)
    draw.rectangle([0, 0, SCR_W, SET_HDR_H], fill=NAVY)
    ml(draw, "Settings", 16, SET_HDR_H // 2, F_MED, CYAN)
    mr(draw, "tap an item", SCR_W - 12, SET_HDR_H // 2, F_HDR, UNIT_C)

    labels = ["Units", "Reset Stats", "Radar Cal.", "Touch Cal."]
    values = ["km/h",  "7I",          "►",          "►"]

    # Tappable item rows — each a bordered button-style strip
    for i, (label, value) in enumerate(zip(labels, values)):
        y = SET_HDR_H + i * SET_ROW_H
        draw.rectangle([0, y + 2, SCR_W, y + SET_ROW_H - 2], fill=BTN_BG)
        draw.line([(0, y), (SCR_W, y)], fill=DIV, width=1)
        draw.rectangle([0, y + 2, 4, y + SET_ROW_H - 2], fill=CYAN)  # accent stripe
        ml(draw, label, 20,         y + SET_ROW_H // 2, F_MED, WHITE)
        mr(draw, value, SCR_W - 20, y + SET_ROW_H // 2, F_MED, CYAN)

    # DONE bar — exit settings
    draw_button(draw, 0, SET_DONE_Y, SCR_W, SCR_H - SET_DONE_Y, "DONE", GREEN)
    return img

# ─── Screen 4 — Calibration ───────────────────────────────────────────────────

def screen_calibration():
    img  = Image.new("RGB", (SCR_W, SCR_H), BG)
    draw = ImageDraw.Draw(img)

    # Header bar
    HDR_H = 30
    draw.rectangle([0, 0, SCR_W, HDR_H], fill=CAL_HDR)
    ml(draw, "CALIBRATION MODE",       10,      HDR_H // 2, F_HDR, YELLOW)
    mr(draw, "tap the buttons below", SCR_W-8,  HDR_H // 2, F_HDR, WHITE)

    # Spectrum area
    SPEC_X, SPEC_Y, SPEC_W, SPEC_H = 10, HDR_H+4, SCR_W-20, 120
    draw.rectangle([SPEC_X, SPEC_Y, SPEC_X+SPEC_W, SPEC_Y+SPEC_H], fill=(4, 12, 12))

    # Fake spectrum bars — noise floor with one spike
    random.seed(42)
    noise_floor = 14.0
    max_seen    = 312.7
    threshold   = 80.0
    scale = (SPEC_H - 4) / max_seen

    for x in range(SPEC_W):
        frac = x / SPEC_W
        # Noise base
        mag = noise_floor * (0.7 + 0.6 * random.random())
        # Spike around 42% of spectrum (represents a shot peak)
        if 0.38 < frac < 0.46:
            mag = max_seen * max(0, 1 - abs(frac - 0.42) / 0.04 * 0.6)
        h = min(int(mag * scale), SPEC_H - 1)
        if h <= 0:
            continue
        color = RED if mag >= threshold else (0, 68, 68)
        draw.line([(SPEC_X+x, SPEC_Y+SPEC_H-1),
                   (SPEC_X+x, SPEC_Y+SPEC_H-1-h)], fill=color)

    # Threshold line
    ty = SPEC_Y + SPEC_H - 1 - int(threshold * scale)
    ty = max(ty, SPEC_Y)
    draw.line([(SPEC_X, ty), (SPEC_X+SPEC_W, ty)], fill=YELLOW, width=1)

    # Peak tick
    px = SPEC_X + int(0.42 * SPEC_W)
    draw.line([(px, SPEC_Y), (px, SPEC_Y+6)], fill=WHITE, width=1)

    # Frequency axis ticks
    tick_y = SPEC_Y + SPEC_H + 4
    MIN_HZ = cfg["MIN_DETECT_HZ"]
    MAX_HZ = cfg["MAX_DETECT_HZ"]
    for label, hz in [("2k",2000),("5k",5000),("8k",8000),("12k",12000)]:
        xp = SPEC_X + int((hz - MIN_HZ) / (MAX_HZ - MIN_HZ) * SPEC_W)
        tc(draw, label, xp, tick_y, F_SMALL, DIM)

    # Metrics row 1
    MY = tick_y + 14
    tl(draw, "NOISE FLOOR", 8,   MY,    F_SMALL, UNIT_C)
    tl(draw, "14.2",        8,   MY+12, F_MED,   CYAN)
    tl(draw, "PEAK MAG",   170,  MY,    F_SMALL, UNIT_C)
    tl(draw, "312.7",      170,  MY+12, F_MED,   RED)
    tl(draw, "MAX SEEN",   332,  MY,    F_SMALL, UNIT_C)
    tl(draw, "312.7",      332,  MY+12, F_MED,   YELLOW)

    # Metrics row 2
    R2Y = MY + 40
    tl(draw, "PEAK",                             8,  R2Y,   F_SMALL, UNIT_C)
    tl(draw, "4820 Hz  =  107.8 km/h",          56, R2Y,   F_SMALL, WHITE)

    # Threshold + suggested
    R3Y = R2Y + 24
    tl(draw, "THRESHOLD",   8,   R3Y,    F_SMALL, UNIT_C)
    tl(draw, "80",          110, R3Y-4,  F_MED,   YELLOW)
    tl(draw, "SUGGESTED",  230,  R3Y,    F_SMALL, UNIT_C)
    tl(draw, "58",         340,  R3Y-4,  F_MED,   CYAN)

    # Bottom action bar — three touch buttons: [-10] [SAVE] [+10]
    draw_button(draw, 0,         BAR_Y, COL_W, BAR_H, "-10",  CYAN)
    draw_button(draw, COL_W,     BAR_Y, COL_W, BAR_H, "SAVE", GREEN)
    draw_button(draw, COL_W*2,   BAR_Y, COL_W, BAR_H, "+10",  CYAN)
    return img

# ─── Compose final image ──────────────────────────────────────────────────────

GAP = 10

def compose():
    screens = [screen_ready(), screen_result(), screen_settings(), screen_calibration()]
    labels  = ["READY", "RESULT", "SETTINGS", "CALIBRATION"]

    # Two rows of 2 screens each
    cols, rows = 2, 2
    total_w = SCR_W * cols + GAP * (cols + 1)
    total_h = SCR_H * rows + GAP * (rows + 1)
    canvas  = Image.new("RGB", (total_w, total_h), (6, 6, 12))

    for i, (scr, lbl) in enumerate(zip(screens, labels)):
        col = i % cols
        row = i // cols
        x   = GAP + col * (SCR_W + GAP)
        y   = GAP + row * (SCR_H + GAP)
        canvas.paste(scr, (x, y))

    canvas.save(OUT, "PNG")
    print(f"Saved {OUT}  ({total_w}×{total_h})")

if __name__ == "__main__":
    compose()
