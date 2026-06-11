# Wiring Guide

---

## System Overview

```
CDM324 (ground, 1.4 m behind ball) ──► LM358 op-amp ──► GPIO34 (ADC1_CH6)
                                                           │
18650 battery ──► ESP32 board                              ├──► GPIO23/18/19/5/2/4 (SPI)
                  (onboard regulator)                      │
                                                           ├──► ILI9488 TFT display
                                                           └──► XPT2046 touch (TOUCH_CS GPIO21)

BTN_POWER  ──► GPIO27 ──► GND   (only physical control)
```

> All navigation is on the touch screen. The only physical button is Power.

> A single radar needs a single preamp channel — one **LM358 DIP-8** IC
> (it has two op-amps; use one, leave the other unused).

---

## Radar Placement

Place the unit on the ground **~1.4 m (4–5 ft) behind the ball**, in line
with the hitting direction, sensor/screen facing the target — the same
geometry as the Shot Scope LM1. The ball flies *away* from the radar;
Doppler measures receding speed identically to approaching speed.

```
Top view:

 [Unit] ──── ~1.4 m ────► ●  →  →  →  target
  (on ground,            ball
   facing target)
```

**Key rules:**
- One CDM324, flat on the ground, boresight pointing at the target.
- Aim it down the intended shot line through the ball impact point.
- Keep it **static** — vibration adds noise.
- For **speed-training mode** (swing speed only), place it ~2.1 m behind
  the golfer instead.

> **Why a single radar?** A lone Doppler sensor measures speed along its
> boresight only. Aligned with the shot it reads true ball and club speed
> directly, but it cannot recover launch angle, spin, or side/dispersion —
> those need extra sensors and are intentionally not faked. See the metric
> table in the [README](../README.md).

---

## CDM324 Radar Pinout

| Pin | Connect to |
|-----|------------|
| VCC | ESP32 board 5V rail |
| GND | GND |
| IF  | LM358 preamp input (via 1 µF coupling cap) |

---

## LM358 Preamplifier (×1)

One **LM358 DIP-8** IC. The chip contains two op-amps; use one channel and
leave the second unused.

- **Gain:** ×100
- **Bandpass:** ~300 Hz – 16 kHz (covers 7–360 km/h Doppler range)
- **Output:** 0–3.3 V centred at 1.65 V (VCC/2)

```
LM358 DIP-8 pin-out (top view):
                ┌───────┐
  OUT A  (1) ───┤1     8├─── VCC (3.3 V)
   IN− A (2) ───┤2     7├─── OUT B  (unused)
   IN+ A (3) ───┤3     6├─── IN− B  (unused)
    GND  (4) ───┤4     5├─── IN+ B  (unused)
                └───────┘

LM358 channel A → GPIO34   (CDM324 IF)
```

**Single-channel schematic:**

```
           C1 (1µF)      R1 (1kΩ)
CDM324 IF ──┤├────────────┤├──────┬──── (+) IN
                                  │
                              R3 (100k)
                              to 3.3V         LM358 OUT ──► GPIO34

                              R4 (100k)   R2 (100kΩ)   C2 (100pF)
                              to GND    (−) IN ──┤├──────────┤├──┐
                                             │                    │
                                             └────────────────────┘
                                  │
                              C3 (10µF)
                              to GND

                 C4 (100nF) across VCC–GND near LM358
```

**How it works:**
- R3 + R4 bias the (+) input to 1.65 V (VCC/2). The firmware subtracts
  2048 from each ADC reading to centre it on zero.
- C1 + R1 set the high-pass corner: fc ≈ 1/(2π·1kΩ·1µF) ≈ 160 Hz
- C2 + R2 set the low-pass corner: fc ≈ 1/(2π·100kΩ·100pF) ≈ 16 kHz
- Gain = 1 + R2/R1 = 101 ≈ ×100

**Passives required (single channel):**

| Component | Value | Qty |
|-----------|-------|-----|
| Resistor | 1 kΩ | 1 |
| Resistor | 100 kΩ | 4 |
| Capacitor | 1 µF | 1 |
| Capacitor | 100 pF | 1 |
| Capacitor | 10 µF | 1 |
| Capacitor | 100 nF | 1 (VCC decoupling) |

---

## TFT Display (ILI9488) + Touch (XPT2046) → ESP32 SPI

The display and the resistive touch controller share one SPI bus. They have
separate chip-select lines but common MOSI / SCLK / MISO.

| Module pin | ESP32 GPIO | Notes |
|------------|------------|-------|
| MOSI / SDI / T_DIN | 23 | shared |
| SCLK / T_CLK | 18 | shared |
| MISO / SDO / T_DO | 19 | shared — **required for touch** |
| LCD_CS | 5 | display select |
| T_CS | 21 | touch select (`TOUCH_CS`) |
| DC / RS | 2 | |
| RST | 4 | |
| BL (backlight) | 3.3V (always on) | |
| T_IRQ | — | leave unconnected (TFT_eSPI polls, no IRQ used) |
| VCC | 3.3V | |
| GND | GND | |

> Unlike the old display-only wiring, **MISO must now be connected** — the
> XPT2046 returns touch coordinates over it. On most of these 3.5" modules the
> display SDO and the touch T_DO are the same physical net; wire it to GPIO19.

---

## Control — Power button + touch

Only one physical button remains. Everything else is on the touch screen.

| Button | GPIO | Function |
|--------|------|---------|
| Power  | GPIO27 | Hold 2 s → deep sleep; press to wake |

> GPIO27 is RTC GPIO (RTC_GPIO17 internally) — required for deep sleep wake.
> Do not move the Power button to a non-RTC pin.
> One leg to GPIO27, the other to GND — internal pull-up, no resistor needed.

**Touch actions by screen:**

| Screen | Touch targets | Power (hold 2 s) |
|--------|---------------|------------------|
| Splash | Tap club circle = next club · bottom bar = Settings | Deep sleep |
| Result | Tap anywhere = dismiss | Deep sleep |
| Settings | Tap a row (Units / Reset / Radar Cal. / Touch Cal.) · DONE bar = exit | Save + exit |
| Calibration | `[-10]` `[SAVE]` `[+10]` buttons | Save + exit (fallback) |

> **First boot / Touch Cal.:** if no touch calibration is stored, the unit runs
> a 4-corner calibration once. Re-run it any time from Settings → Touch Cal.

---

## Power

| Rail | Source | Used by |
|------|--------|---------|
| 3.3V | ESP32 onboard LDO | TFT VCC/BL, LM358 VCC |
| 5V | ESP32 boost converter | CDM324 VCC |
| GND | Common | All components |
