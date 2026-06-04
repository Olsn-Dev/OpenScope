# Wiring Guide

---

## System Overview

```
CDM324 (IF out) ──► LM358 preamp ──► GPIO34 (ADC)
                                         │
18650 battery ──► ESP32 board            ├──► GPIO23/18/5/2/4 (SPI)
                  (onboard regulator)    │
                                         └──► ST7796 TFT display

Tactile button ──► GPIO0 ──► GND   (internal pull-up, no resistor needed)
```

---

## CDM324 Radar Pinout

| Pin | Connect to |
|-----|------------|
| VCC | ESP32 board 5V rail |
| GND | GND |
| IF  | LM358 preamp input (via 1 µF coupling cap) |

---

## LM358 Preamplifier Circuit

Single-stage non-inverting amplifier with bandpass filter.
- **Gain:** ×100
- **Bandpass:** ~300 Hz – 16 kHz (covers 7–360 km/h Doppler range)
- **Output:** 0–3.3 V swing centered at 1.65 V (VCC/2), suitable for ESP32 ADC

```
           C1 (1µF)      R1 (1kΩ)
CDM324 IF ──┤├────────────┤├──────┬──── (+) LM358 OUT ──► GPIO34
                                  │     │
                              R3 (100k) │  R2 (100kΩ)   C2 (100pF)
                              to 3.3V   └──┤├──────────────┤├──┐
                                           │                    │
                              R4 (100k)   (-) IN               │
                              to GND       └────────────────────┘
                                  │
                              C3 (10µF)
                              to GND

                 C4 (100nF) across VCC–GND near LM358
```

**How it works:**
- R3 + R4 form a voltage divider that biases the non-inverting (+) input to
  1.65 V (VCC/2), so the output sits at mid-rail with no input signal.
  The firmware subtracts 2048 from each ADC reading to centre it on zero.
- C1 blocks DC from the CDM324 and sets the high-pass corner with R1:
  fc = 1 / (2π × 1kΩ × 1µF) ≈ 160 Hz
- C2 across R2 sets the low-pass corner:
  fc = 1 / (2π × 100kΩ × 100pF) ≈ 16 kHz
- Gain = 1 + R2/R1 = 1 + 100k/1k = 101 ≈ ×100

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

## Control Button

| Button leg | Connect to |
|------------|------------|
| Leg 1 | GPIO0 |
| Leg 2 | GND |

No external resistor needed — GPIO0 uses the ESP32's internal pull-up.

For the **onboard boot button** (GPIO0 on the ESP32 dev board): this works
as-is during prototyping. For a finished enclosure, solder a 6×6 mm tactile
button to a short wire lead and route it to an accessible hole in the housing.

**Button functions (firmware v0.4):**

| Press duration | Action |
|----------------|--------|
| Short < 1 s | Cycle club selection |
| Medium 1–3 s | Open settings menu |
| Long > 3 s | Enter calibration mode |

---

## Power

The ESP32+18650 all-in-one board handles charging and regulation internally.
Charge via the board's onboard USB port. No external power circuit needed.

**Voltage summary:**

| Rail | Source | Used by |
|------|--------|---------|
| 3.3V | ESP32 onboard LDO | TFT VCC, TFT BL, LM358 VCC |
| 5V | ESP32 boost converter | CDM324 VCC |
| GND | Common | All components |
