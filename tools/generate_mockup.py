#!/usr/bin/env python3
"""
generate_mockup.py — Render the OpenScope UI mockup from firmware constants.

Parses src/config.h for color values and layout constants so the mockup
stays in sync with the firmware automatically.

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

cfg   = parse_defines(ROOT / "src" / "config.h")
SCR_W = cfg["SCR_W"]       # 480
SCR_H = cfg["SCR_H"]       # 320
COL_W = SCR_W // 3         # 160
ROW_H = SCR_H // 2         # 160

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
SEL_BG  = rgb565(cfg["COL_SEL_BG"])

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

# ─── Drawing primitives ───────────────────────────────────────────────────────

def tc(draw, text, cx, y, font, color):
    """Draw text top-center at (cx, y)."""
    bb = draw.textbbox((0, 0), text, font=font)
    w  = bb[2] - bb[0]
    draw.text((cx - w // 2, y), text, font=font, fill=color)

def tl(draw, text, x, y, font, color):
    draw.text((x, y), text, font=font, fill=color)

def tr(draw, text, x, y, font, color):
    bb = draw.textbbox((0, 0), text, font=font)
    w  = bb[2] - bb[0]
    draw.text((x - w, y), text, font=font, fill=color)

def br_text(draw, text, x, y, font, color):
    """Draw text bottom-right anchored at (x, y)."""
    bb = draw.textbbox((0, 0), text, font=font)
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    draw.text((x - w, y - h), text, font=font, fill=color)

def grid_lines(draw):
    draw.line([(COL_W, 0), (COL_W, SCR_H)],     fill=DIV, width=1)
    draw.line([(COL_W*2, 0), (COL_W*2, SCR_H)], fill=DIV, width=1)
    draw.line([(0, ROW_H), (SCR_W, ROW_H)],      fill=DIV, width=1)

def draw_tile(draw, col, row, label, number, unit, num_color, dimmed=False):
    cx = col * COL_W + COL_W // 2
    y0 = row * ROW_H
    lc = DIM if dimmed else CYAN
    nc = DIM if dimmed else num_color
    uc = DIM if dimmed else UNIT_C
    tc(draw, label,  cx, y0 + 10, F_LABEL, lc)
    tc(draw, number, cx, y0 + 38, F_NUM,   nc)
    tc(draw, unit,   cx, y0 + 98, F_UNIT,  uc)

def draw_club_tile(draw, col, row, abbr):
    cx = col * COL_W + COL_W // 2
    cy = row * ROW_H + ROW_H // 2
    r  = 42
    draw.ellipse([cx-r, cy-r, cx+r, cy+r],   outline=CYAN, width=1)
    draw.ellipse([cx-r-1, cy-r-1, cx+r+1, cy+r+1], outline=CYAN, width=1)
    tc(draw, abbr, cx, cy - 10, F_LABEL, WHITE)

# ─── Screen 1 — Ready ─────────────────────────────────────────────────────────

def screen_ready():
    img  = Image.new("RGB", (SCR_W, SCR_H), BG)
    draw = ImageDraw.Draw(img)
    grid_lines(draw)

    # Top row — dimmed (no shot yet)
    draw_tile(draw, 0, 0, "CLUB",   "--", "km/h", WHITE, dimmed=True)
    draw_tile(draw, 1, 0, "BALL",   "--", "km/h", WHITE, dimmed=True)
    draw_tile(draw, 2, 0, "LAUNCH", "--", "°",     WHITE, dimmed=True)

    # Bottom row — club statistics
    draw_tile(draw, 0, 1, "AVG",  "182", "m",   WHITE)
    draw_tile(draw, 1, 1, "BEST", "201", "m",   GREEN)
    draw_club_tile(draw, 2, 1, "7I")

    # Center prompt
    tc(draw, "SWING WHEN READY", SCR_W // 2, ROW_H + 4, F_UNIT, DIM)
    return img

# ─── Screen 2 — Result ────────────────────────────────────────────────────────

def screen_result():
    img  = Image.new("RGB", (SCR_W, SCR_H), BG)
    draw = ImageDraw.Draw(img)
    grid_lines(draw)

    draw_tile(draw, 0, 0, "CLUB",   "98",   "km/h",     WHITE)
    draw_tile(draw, 1, 0, "BALL",   "152",  "km/h",     WHITE)
    draw_tile(draw, 2, 0, "LAUNCH", "19.4", "°",   GREEN)   # ← launch angle tile
    draw_tile(draw, 0, 1, "CARRY",  "187",  "m",        WHITE)
    draw_tile(draw, 1, 1, "TOTAL",  "209",  "m",        WHITE)
    draw_club_tile(draw, 2, 1, "7I")

    # Smash factor footnote (bottom-right)
    br_text(draw, "smash 1.55", SCR_W - 4, SCR_H - 3, F_SMALL, UNIT_C)
    return img

# ─── Screen 3 — Calibration ───────────────────────────────────────────────────

def screen_calibration():
    img  = Image.new("RGB", (SCR_W, SCR_H), BG)
    draw = ImageDraw.Draw(img)

    # Header bar
    HDR_H = 30
    draw.rectangle([0, 0, SCR_W, HDR_H], fill=CAL_HDR)
    tl(draw, "CALIBRATION MODE",                           10,       8,  F_HDR, YELLOW)
    tr(draw, "scroll=+10  select=-10  power=save+exit", SCR_W-8,  8,  F_HDR, WHITE)

    # Spectrum area
    SPEC_X, SPEC_Y, SPEC_W, SPEC_H = 10, HDR_H+4, SCR_W-20, 148
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
        # Spike around 40% of spectrum (represents a shot peak)
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
    R2Y = MY + 42
    tl(draw, "PEAK FREQ",                        8, R2Y,    F_SMALL, UNIT_C)
    tl(draw, "4820 Hz  =  107.8 km/h",           8, R2Y+12, F_SMALL, WHITE)

    # Threshold + suggested
    R3Y = R2Y + 32
    tl(draw, "THRESHOLD",   8,   R3Y,    F_SMALL, UNIT_C)
    tl(draw, "80",          8,   R3Y+10, F_BIG,   YELLOW)
    tl(draw, "SUGGESTED",  200,  R3Y,    F_SMALL, UNIT_C)
    tl(draw, "58  (noise x4)", 200, R3Y+12, F_SMALL, CYAN)

    return img

# ─── Screen 4 — Settings ─────────────────────────────────────────────────────

def screen_settings(sel=0):
    img  = Image.new("RGB", (SCR_W, SCR_H), BG)
    draw = ImageDraw.Draw(img)

    # Header bar (navy)
    HDR_H = 48
    draw.rectangle([0, 0, SCR_W, HDR_H], fill=NAVY)
    tl(draw, "Settings", 16, 16, F_MED, CYAN)
    tr(draw, "scroll=next  select=choose  power=exit", SCR_W - 12, 20, F_HDR, UNIT_C)

    labels = ["Units",        "Reset Stats",  "Calibration"]
    values = ["km/h",         "7I",            "►"]  # ▶ arrow for calibration

    for i, (label, value) in enumerate(zip(labels, values)):
        y = 68 + i * 74
        active = (i == sel)
        row_bg = (10, 26, 42) if active else BG
        draw.rectangle([0, y - 2, SCR_W, y + 52], fill=row_bg)
        if active:
            draw.rectangle([0, y - 2, 4, y + 52], fill=CYAN)

        lc = WHITE if active else UNIT_C
        vc = CYAN  if active else DIM
        tl(draw, label, 20,          y + 14, F_MED, lc)
        tr(draw, value, SCR_W - 20,  y + 14, F_MED, vc)

    # Divider + version footer
    draw.line([(0, SCR_H - 22), (SCR_W, SCR_H - 22)], fill=DIV, width=1)
    tc(draw, "OpenScope v0.6", SCR_W // 2, SCR_H - 18, F_SMALL, DIM)

    return img

# ─── Compose final image ──────────────────────────────────────────────────────

GAP = 10

def compose():
    screens = [screen_ready(), screen_result(), screen_settings(sel=0), screen_calibration()]
    labels  = ["READY", "RESULT", "SETTINGS", "CALIBRATION"]

    # Two rows of 2 screens each
    cols, rows = 2, 2
    total_w = SCR_W * cols + GAP * (cols + 1)
    total_h = SCR_H * rows + GAP * (rows + 1)
    canvas  = Image.new("RGB", (total_w, total_h), (6, 6, 12))

    F_CAP = load_font(13)
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
