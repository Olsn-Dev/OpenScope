# Wiring Guide

---

## System Overview

```
CDM324 L (ground, left V arm)  ──► LM358 op-amp 1 ──► GPIO34 (ADC1_CH6)
CDM324 R (ground, right V arm) ──► LM358 op-amp 2 ──► GPIO35 (ADC1_CH7)
CDM324 T (top, 20° up)         ──► LM358 op-amp 3 ──► GPIO32 (ADC1_CH4)
                                                           │
18650 battery ──► ESP32 board                              ├──► GPIO23/18/19/5/2/4 (SPI)
                  (onboard regulator)                      │
                                                           ├──► ILI9488 TFT display
                                                           └──► XPT2046 touch (TOUCH_CS GPIO21)

BTN_POWER  ──► GPIO27 ──► GND   (only physical control)
```

> All navigation is on the touch screen. The only physical button is Power.

> Three radar channels require three preamp channels. Use **three LM358 ICs**
> (one per radar) — each DIP-8 has two op-amps; use one per IC.

---

## Radar Placement & Mounting Angles

Place the entire unit **behind the golfer**, pointing toward the target —
the same as commercial systems (Trackman, FlightScope, etc.). The ball
flies *away* from the radar; Doppler measures receding speed identically
to approaching speed.

```
Top view:

 [Unit]  ←  ~0.5–1 m  →  [Golfer]  →  ●  →  →  →  target
              behind                  ball
```

### Ground radars — V-formation (Radar L & R)

```
Top view (looking down from above):

              ↑  target / ball flight direction

              /\   ← 90° vertex angle
             /  \
            / 45°\ 45°
     [L]──►/      \◄──[R]
  GPIO34  /        \  GPIO35

  Both radars on the ground, ~0.5–1 m behind the golfer.
  V-tip (vertex) points toward the target.
```

**Key rules:**
- Each radar arm is **45° from the shot direction** (90° total V-angle).
- Mount both flat on the ground — use a spirit level.
- Aim the V-tip toward the ball impact point.
- Place the unit **0.5–1 m behind the golfer**, slightly to the side
  so it is not in the swing path.
- Keep both radars **static** — vibration adds noise.

**Why 45°?** This angle maximises `sin(V)·cos(V)`, giving the optimal
trade-off between CDM324 signal strength and side-angle sensitivity.
Estimated accuracy: < 0.1° at 150 km/h.

**Changing the arm angle:**
```cpp
// src/config.h
#define RADAR_V_HALF_DEG  45.0f   // degrees per arm from shot direction
```

---

### Top radar — launch angle (Radar T)

```
Side view:

  [Golfer]          ●  →  →  →  ↗  Ball trajectory (~8–40° typical)
                   tee         /
  Radar T ───────────────────►/   20° above horizontal
  (GPIO32)                   /
  ~0.5–1 m behind golfer
──────────────────────────────────────────────────  Ground
```

**Key rules:**
- Mount tilted **20° upward** — use a printed wedge or protractor.
- Point the boresight toward the expected ball impact position.
- Keep static during use.

**Why 20°?** Golf launch angles range from ~8° (driver) to ~40° (lob
wedge). A 20° top-radar angle gives good sensitivity across the full range.

**Changing the mount angle:**
```cpp
// src/config.h
#define RADAR_T_ANGLE_DEG  20.0f   // degrees above horizontal
```

---

## CDM324 Radar Pinout

| Pin | Connect to |
|-----|------------|
| VCC | ESP32 board 5V rail |
| GND | GND |
| IF  | LM358 preamp input (via 1 µF coupling cap) |

Both CDM324 modules use the same pinout. Each IF output goes to its own
op-amp channel inside the same LM358 DIP-8 IC.

---

## LM358 Preamplifiers (×3)

Three **LM358 DIP-8** ICs — one per radar. Each IC contains two op-amps;
use one, leave the second unused. All three ICs use identical component values.

- **Gain:** ×100 per channel
- **Bandpass:** ~300 Hz – 16 kHz (covers 7–360 km/h Doppler range)
- **Output:** 0–3.3 V centred at 1.65 V (VCC/2)

```
LM358 DIP-8 pin-out (top view, build ×3):
                ┌───────┐
  OUT A  (1) ───┤1     8├─── VCC (3.3 V)
   IN− A (2) ───┤2     7├─── OUT B  (unused)
   IN+ A (3) ───┤3     6├─── IN− B  (unused)
    GND  (4) ───┤4     5├─── IN+ B  (unused)
                └───────┘

LM358 #1 channel A → GPIO34   (Radar L, left V arm)
LM358 #2 channel A → GPIO35   (Radar R, right V arm)
LM358 #3 channel A → GPIO32   (Radar T, top, angled upward)
```

**Single-channel schematic (build twice — once per op-amp):**

```
           C1 (1µF)      R1 (1kΩ)
CDM324 IF ──┤├────────────┤├──────┬──── (+) IN
                                  │
                              R3 (100k)
                              to 3.3V         LM358 OUT ──► GPIOxx

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

**Passives required (both channels combined):**

| Component | Value | Qty |
|-----------|-------|-----|
| Resistor | 1 kΩ | 2 |
| Resistor | 100 kΩ | 8 |
| Capacitor | 1 µF | 2 |
| Capacitor | 100 pF | 2 |
| Capacitor | 10 µF | 2 |
| Capacitor | 100 nF | 2 (VCC decoupling) |

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
| 3.3V | ESP32 onboard LDO | TFT VCC/BL, LM358 #1–3 VCC |
| 5V | ESP32 boost converter | CDM324 L + R + T VCC |
| GND | Common | All components |
