# Wiring Guide

> **Status:** Work in progress — will be completed in Phase 1.

## Overview

```
CDM324 (IF out) → LM358 op-amp → ESP32 ADC (GPIO34)
                                → ESP32 → TFT display (SPI)
18650 battery → ESP32 board (onboard regulator)
```

## CDM324 Pinout

| Pin | Description |
|-----|-------------|
| VCC | 5V supply |
| GND | Ground |
| IF  | Doppler IF signal output (to LM358 input) |

## LM358 Preamplifier

Circuit based on the CDM324/IPM-165 application note.
- Gain: ~100×
- Bandpass: ~100 Hz – 5 kHz (covers golf ball speeds 50–300 km/h)
- Output: 0–3.3V swing suitable for ESP32 ADC

Schematic to be added.

## TFT Display (ST7796) → ESP32 SPI

| Display Pin | ESP32 GPIO |
|-------------|------------|
| MOSI | 23 |
| SCLK | 18 |
| CS | 5 |
| DC | 2 |
| RST | 4 |
| VCC | 3.3V |
| GND | GND |

## Power

The ESP32+18650 all-in-one board handles charging and regulation internally.
Charge via the onboard USB-C/Micro-USB port.
