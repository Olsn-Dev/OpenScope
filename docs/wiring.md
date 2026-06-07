# Wiring Guide

---

## System Overview

```
CDM324-A (IF out) ──► LM358 preamp A ──► GPIO34 (ADC — Radar A, primary)
CDM324-B (IF out) ──► LM358 preamp B ──► GPIO35 (ADC — Radar B, 30 mm offset)
                                              │
18650 battery ──► ESP32 board                ├──► GPIO23/18/5/2/4 (SPI)
                  (onboard regulator)        │
                                             └──► ST7796 TFT display

Tactile button ──► GPIO0 ──► GND   (internal pull-up, no resistor needed)
```

Both CDM324 modules are mounted side by side with **30 mm centre-to-centre** spacing.
Each module has its own identical LM358 preamplifier circuit.
The firmware samples Radar A and Radar B sequentially (each 25.6 ms / 1024 samples at
40 kHz) and cross-validates the detected ball speed. If both radars agree within 10 %,
the result is averaged and displayed as **DUAL OK** (green). If only one radar triggers
it is shown as **SINGLE** (dimmed). If both trigger but disagree by more than 10 %,
the frame is discarded as a false trigger.

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

## Control Buttons (v0.5 — three buttons)

All buttons: connect one leg to the GPIO, other leg to GND.
No external resistors — the ESP32 uses internal pull-ups.

| Button | GPIO | Leg 1 | Leg 2 |
|--------|------|-------|-------|
| Scroll | GPIO25 | GPIO25 | GND |
| Select | GPIO26 | GPIO26 | GND |
| Power  | GPIO27 | GPIO27 | GND |

> GPIO27 is an RTC GPIO (RTC_GPIO17 internally) which means it can wake the
> ESP32 from deep sleep. Do not change the Power button to a non-RTC pin.

**Button functions:**

| Screen | Scroll | Select | Power (hold 2 s) |
|--------|--------|--------|------------------|
| Splash | Next club | Open settings | Deep sleep |
| Result | Next club | Dismiss result | Deep sleep |
| Settings | Next item | Activate item | Save + exit |
| Calibration | Threshold +10 | Threshold -10 | Save + exit |

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
