# Wiring Guide

---

## System Overview

```
CDM324 A (horizontal)    ──► LM358 op-amp 1 ──► GPIO34 (ADC)
CDM324 B (angled 20° up) ──► LM358 op-amp 2 ──► GPIO35 (ADC)
                                                     │
18650 battery ──► ESP32 board                        ├──► GPIO23/18/5/2/4 (SPI)
                  (onboard regulator)                │
                                                     └──► ST7796 TFT display

BTN_SCROLL ──► GPIO25 ──► GND
BTN_SELECT ──► GPIO26 ──► GND
BTN_POWER  ──► GPIO27 ──► GND
```

---

## Radar Placement & Mounting Angles

Both CDM324 modules must be mounted in fixed positions relative to the
ball impact point. Use a printed wedge, a protractor, or a 3D-printed
bracket to hold Radar B at the correct elevation angle.

```
Side view:

                             ↗  Ball trajectory (~12–35° typical)
                            /
              Radar B ────►/   20° above horizontal
             (GPIO35)     /
                         /
──────────────────────────────────────────────────  Ground
              Radar A ──────────────────────────►   0° (horizontal)
             (GPIO34)
                 │
              ~10 cm behind tee, centred on ball path
```

**Key rules:**
- Radar A must be **exactly horizontal** — use a spirit level.
- Radar B must be tilted **upward** by `RADAR_B_ANGLE_DEG` (default 20°).
  If you change this angle, update the `#define` in `src/main.cpp`.
- Both radars should point directly at the expected ball impact position.
- Mount them **side by side**, no more than 5 cm apart horizontally.
- Keep both radars **static** during use — any vibration adds noise.

**Why 20°?**
Golf launch angles range from ~8° (driver) to ~40° (lob wedge). A 20°
Radar B angle gives the best sensitivity across the full club range:
at 20° launch the radars read equal frequency, and the ratio deviates
measurably in both directions for higher and lower angles.

**Changing the mount angle:**
If you build a bracket at a different angle, change this line in
`src/main.cpp` to match:
```cpp
#define RADAR_B_ANGLE_DEG  20.0f   // degrees above horizontal
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

## LM358 Dual-Channel Preamplifier

The LM358 DIP-8 contains **two independent op-amps** — one per radar.
Both channels share VCC/GND and use identical component values.

- **Gain:** ×100 per channel
- **Bandpass:** ~300 Hz – 16 kHz (covers 7–360 km/h Doppler range)
- **Output:** 0–3.3 V centred at 1.65 V (VCC/2)

```
LM358 DIP-8 pin-out (top view):
                ┌───────┐
  OUT A  (1) ───┤1     8├─── VCC (3.3 V)
   IN− A (2) ───┤2     7├─── OUT B  → GPIO35
   IN+ A (3) ───┤3     6├─── IN− B
    GND  (4) ───┤4     5├─── IN+ B
                └───────┘

Channel A → GPIO34   (Radar A, horizontal)
Channel B → GPIO35   (Radar B, angled upward)
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
| 3.3V | ESP32 onboard LDO | TFT VCC/BL, LM358 VCC |
| 5V | ESP32 boost converter | CDM324 A + B VCC |
| GND | Common | All components |
