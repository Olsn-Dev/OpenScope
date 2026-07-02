# Wiring Guide

---

## System Overview

Target board: **LILYGO T-Energy-S3** (ESP32-S3-WROOM-1-N16R8, 18650 holder,
USB-C).

```
CDM324 (ground, 1.4 m behind ball) ──► LM358 preamp ──► GPIO1 (ADC1_CH0)
                                                           │
18650 battery ──► T-Energy-S3                              ├──► GPIO11/12/13/10/9/14 (SPI)
                  (onboard regulator)                      │
                                                           ├──► ILI9488 TFT display
                                                           └──► XPT2046 touch (TOUCH_CS GPIO21)

BTN_POWER  ──► GPIO2 ──► GND   (only physical control)
```

> All navigation is on the touch screen. The only physical button is Power.

> **ESP32-S3 pins to avoid** (used by the T-Energy-S3 board itself):
> IO0 (boot button), IO3 (battery-voltage divider), IO15/IO16 (32 kHz
> crystal), IO19/IO20 (USB-C data), IO35–IO37 (octal PSRAM), IO43/IO44
> (UART0), IO45/IO46 (boot straps).

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

### Option A — pre-built module (HW-164, what we use)

The blue **HW-164 "LM358 ×100 amplifier" module** (marked `2578-LM358` on the
back, four pins `VCC IN OUT GND`, blue gain trimmer) replaces the discrete
preamp below:

| Module pin | Connect to |
|------------|------------|
| VCC | 3.3V |
| IN  | CDM324 IF |
| OUT | GPIO1 (ADC1_CH0) |
| GND | GND |

- Start with the trimmer at max gain (~×100) and tune down if the ADC clips.
- **Check the idle level:** the firmware expects the output to idle near
  mid-rail (~2048 ADC counts). Use Settings → Radar Cal. to inspect it. If
  the output idles near 0 V instead, bias the module's IN pin to VCC/2 with
  two 100 kΩ resistors (one to 3.3V, one to GND) and feed the CDM324 IF in
  through a 1 µF series cap — same trick as R3/R4/C1 in the schematic below.

### Option B — discrete build (reference)

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

LM358 channel A → GPIO1   (CDM324 IF)
```

**Single-channel schematic:**

```
           C1 (1µF)      R1 (1kΩ)
CDM324 IF ──┤├────────────┤├──────┬──── (+) IN
                                  │
                              R3 (100k)
                              to 3.3V         LM358 OUT ──► GPIO1

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

On the module the touch pins are labelled `TCK / TCS / TDI / TDO / PEN`
(= T_CLK, T_CS, T_DIN, T_DO, T_IRQ).

| Module pin | ESP32-S3 GPIO | Notes |
|------------|---------------|-------|
| SDI / TDI (MOSI) | 11 | shared |
| SCK / TCK (SCLK) | 12 | shared |
| SDO / TDO (MISO) | 13 | shared — **required for touch** |
| CS | 10 | display select |
| TCS | 21 | touch select (`TOUCH_CS`) |
| D/C | 9 | |
| RST | 14 | |
| BL (backlight) | 3.3V (always on) | |
| PEN (T_IRQ) | — | leave unconnected (TFT_eSPI polls, no IRQ used) |
| VDD | 3.3V | |
| GND | GND | |

> IO9–14 + IO21 all sit on the same T-Energy-S3 header (with a GND pin), so
> the whole display ribbon lands on one connector. IO10–13 are the S3's
> native FSPI pins — full 40 MHz SPI works.
>
> Unlike the old display-only wiring, **MISO must now be connected** — the
> XPT2046 returns touch coordinates over it. On these 3.5" modules the display
> SDO and the touch TDO are the same physical net; wire it to GPIO13.

---

## Control — Power button + touch

Only one physical button remains. Everything else is on the touch screen.

| Button | GPIO | Function |
|--------|------|---------|
| Power  | GPIO2 | Hold 2 s → deep sleep; press to wake |

> GPIO2 is RTC-capable (on the ESP32-S3 every GPIO 0–21 is) — required for
> deep sleep wake. Do not move the Power button to a non-RTC pin (IO22+).
> One leg to GPIO2, the other to GND — internal pull-up, no resistor needed.

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
| 3.3V | T-Energy-S3 buck (SY8089) | TFT VDD/BL, LM358 VCC |
| VCC5V | USB: ~5 V · battery: **3.7–4.2 V** (no boost!) | CDM324 VCC |
| GND | Common | All components |

> **⚠ CDM324 supply on battery:** the T-Energy-S3 has **no 5 V boost
> converter** — per the schematic its `VCC5V` header pin is USB VBUS when
> plugged in, but raw battery voltage (3.7–4.2 V) on battery power. The
> CDM324 is specced at 5 V ±0.25 V. Test it: many units still oscillate at
> ~4 V with reduced sensitivity, but if range is poor, add a small **MT3608
> boost module** (VBAT/VCC5V → 5 V, ~10 SEK) to feed the radar.
