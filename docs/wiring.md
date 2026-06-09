# Wiring Guide

---

## System Overview

```
CDM324 L (ground, left V arm)  ──► LM358 op-amp 1 ──► GPIO34 (ADC1_CH6)
CDM324 R (ground, right V arm) ──► LM358 op-amp 2 ──► GPIO35 (ADC1_CH7)
CDM324 T (top, 20° up)         ──► LM358 op-amp 3 ──► GPIO32 (ADC1_CH4)
                                                           │
18650 battery ──► ESP32 board                              ├──► GPIO23/18/5/2/4 (SPI)
                  (onboard regulator)                      │
                                                           └──► ST7796 TFT display

BTN_SCROLL ──► GPIO25 ──► GND
BTN_SELECT ──► GPIO26 ──► GND
BTN_POWER  ──► GPIO27 ──► GND
```

> Three radar channels require three preamp channels. Use **three LM358 ICs**
> (one per radar) — each DIP-8 has two op-amps; use one per IC.

---

## Radar Placement & Mounting Angles

All three CDM324 modules must be mounted in fixed positions relative to
the ball impact point.

### Ground radars — V-formation (Radar L & R)

```
Top view (looking down from above):

              ↑  target / ball flight direction

              /\   ← 90° vertex angle
             /  \
            / 45°\ 45°
     [L]──►/      \◄──[R]
  GPIO34  /        \  GPIO35

  Both radars on the ground, ~10 cm behind the tee.
  V-tip (vertex) points toward the target.
```

**Key rules:**
- Each radar arm is **45° from the shot direction** (90° total V-angle).
- Mount both flat on the ground — use a spirit level.
- Aim each radar's boresight toward the ball impact point.
- Keep the vertex of the V within ~10 cm of the tee.
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

                             ↗  Ball trajectory (~8–40° typical)
                            /
              Radar T ────►/   20° above horizontal
             (GPIO32)     /
                         /
──────────────────────────────────────────────────  Ground
                 │
              ~10 cm behind tee, centred on ball path
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

## TFT Display (ST7796) → ESP32 SPI

| Display pin | ESP32 GPIO |
|-------------|------------|
| MOSI | 23 |
| SCLK | 18 |
| CS | 5 |
| DC | 2 |
| RST | 4 |
| BL (backlight) | 3.3V (always on) |
| VCC | 3.3V |
| GND | GND |

MISO is not used — leave unconnected.

---

## Control Buttons

One leg to the GPIO, other leg to GND. No external resistors needed —
the ESP32 uses internal pull-ups.

| Button | GPIO | Function |
|--------|------|---------|
| Scroll | GPIO25 | Cycle clubs / navigate / threshold +10 in calibration |
| Select | GPIO26 | Open settings / confirm / threshold -10 in calibration |
| Power  | GPIO27 | Hold 2 s → deep sleep; press to wake |

> GPIO27 is RTC GPIO (RTC_GPIO17 internally) — required for deep sleep wake.
> Do not move the Power button to a non-RTC pin.

**Button functions by screen:**

| Screen | Scroll | Select | Power (hold 2 s) |
|--------|--------|--------|------------------|
| Splash | Next club | Open settings | Deep sleep |
| Result | Next club | Dismiss result | Deep sleep |
| Settings | Next item | Activate item | Save + exit |
| Calibration | Threshold +10 | Threshold -10 | Save + exit |

---

## Power

| Rail | Source | Used by |
|------|--------|---------|
| 3.3V | ESP32 onboard LDO | TFT VCC/BL, LM358 #1–3 VCC |
| 5V | ESP32 boost converter | CDM324 L + R + T VCC |
| GND | Common | All components |
