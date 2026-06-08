# Bill of Materials — OpenScope v0.6

All components from AliExpress unless noted. Prices approximate (SEK).

---

## Main Components

| # | Component | Search / Part number | Qty | ~Price (SEK) |
|---|-----------|----------------------|-----|--------------|
| 1 | ESP32 dev board with 18650 battery holder | `ESP32 18650 battery holder board` | 1 | 90 |
| 2 | 18650 Li-Ion battery, 3000 mAh, protected | `18650 3000mAh protected battery` | 1 | 60 |
| 3 | CDM324 24 GHz Doppler radar module | AliExpress: `4000332661554` | 2 | 540 |
| 4 | LM358 op-amp IC or module | `LM358 op amp DIP8` or `LM358 module` | 1 | 10 |
| 5 | 3.5" TFT display, SPI, ST7796, 480×320 | `3.5 inch TFT LCD SPI ST7796 480x320` | 1 | 120 |
| 6 | Tactile push button, 6×6 mm, PCB mount | `6x6mm tactile push button switch` | 4 | 8 |

---

## LM358 Preamplifier Passives

The LM358 DIP-8 contains **two op-amps** — one preamp per radar, both in the
same IC. Each channel amplifies the CDM324 IF signal ×100 and bandpass-filters
it to 300 Hz – 16 kHz (covers ~7–360 km/h).

| Component | Value | Qty | Purpose |
|-----------|-------|-----|---------|
| Resistor | 1 kΩ | 2 | Gain lower resistor R1 (one per channel) |
| Resistor | 100 kΩ | 8 | R2 (feedback) + R3, R4 (bias divider) × 2 channels |
| Capacitor | 1 µF, film or electrolytic | 2 | AC coupling (high-pass ~160 Hz) |
| Capacitor | 100 pF, ceramic | 2 | Feedback cap (low-pass ~16 kHz) |
| Capacitor | 10 µF, electrolytic | 2 | Bias midpoint bypass (one per channel) |
| Capacitor | 100 nF, ceramic | 2 | VCC decoupling |

> All resistors 0.25 W, 5% tolerance or better. A mixed resistor/capacitor
> assortment kit from AliExpress (≈25 SEK) covers all values above.

---

## Wiring & Assembly

| Component | Search | Qty | ~Price (SEK) |
|-----------|--------|-----|--------------|
| Dupont jumper wires, female–female, 20 cm | `dupont jumper wire female female` | 1 pack (40 pcs) | 15 |
| Dupont jumper wires, male–female, 20 cm | `dupont jumper wire male female` | 1 pack (40 pcs) | 15 |
| Perfboard / strip board, 5×7 cm | `perfboard prototype PCB 5x7` | 1 | 10 |
| USB-C or Micro-USB cable (matches your ESP32 board) | — | 1 | 20 |

---

## Cost Summary

| Category | ~Price (SEK) |
|----------|-------------|
| Main components | 825 |
| Preamp passives | 25 |
| Wiring & assembly | 60 |
| **Total** | **~910 SEK** |

---

## Notes

### Buttons
The firmware (v0.5) uses **three separate buttons**:

| Button | GPIO | Function |
|--------|------|---------|
| Scroll | GPIO25 | Cycle clubs / navigate menus / threshold +10 in calibration |
| Select | GPIO26 | Open settings / confirm selection / threshold -10 in calibration |
| Power  | GPIO27 | Hold 2 s → deep sleep; press to wake (RTC GPIO) |

Connect each button between the GPIO pin and GND. No external resistors needed —
the ESP32 uses internal pull-ups. For an enclosed device, mount all three buttons
in drilled holes on the side of the housing. Order 4 buttons (3 used + 1 spare).

### CDM324 radar (×2)
Two CDM324 modules are used for launch angle measurement:
- **Radar A (GPIO34):** mounted horizontally — measures ball speed.
- **Radar B (GPIO35):** mounted at 20° above horizontal — together with A,
  allows the firmware to compute launch angle via the Doppler frequency ratio.

See `docs/wiring.md` for the full mounting diagram and angle instructions.

- The **standard CDM324** (not the -UK or -F variant) is approved for 24 GHz
  ISM use in Sweden and the EU. No licence required.
- Supply both CDM324 modules from the ESP32 board's 5V rail.
- Do **not** connect any CDM324 IF pin directly to the ESP32 ADC — always via
  the LM358 preamp circuit.

### LM358 preamp bandwidth
The preamp must pass **300 Hz – 18 kHz** to cover the full speed range
(~7–400 km/h). A 5 kHz upper cutoff (common in older application notes) would
limit detection to ~112 km/h. Use the component values above.

### ESP32 + 18650 board
The all-in-one board includes onboard TP4056 charging, a boost converter, and
battery protection. No separate charging or protection circuit is needed.
Charge via the board's USB port.

### Display backlight
The ST7796 display's backlight (BL pin) can be connected directly to 3.3V for
always-on backlight. Do not connect it to 5V.
