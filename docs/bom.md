# Bill of Materials

All components ordered from AliExpress unless noted. Prices approximate (SEK).

| # | Component | Search / Part Number | Qty | ~Price (SEK) |
|---|-----------|----------------------|-----|--------------|
| 1 | ESP32 dev board with 18650 battery holder | Search: `ESP32 18650 battery holder board` | 1 | 90 |
| 2 | 18650 Li-Ion battery (3000 mAh, protected) | Search: `18650 3000mAh protected battery` | 1 | 60 |
| 3 | CDM324 24 GHz Doppler radar module | AliExpress item: `4000332661554` | 1 | 270 |
| 4 | LM358 op-amp module (preamplifier for CDM324) | Search: `LM358 op amp amplifier module` | 1 | 15 |
| 5 | 3.5" TFT display, SPI, ST7796, 480×320 | Search: `3.5 inch TFT LCD SPI ST7796 480x320` | 1 | 120 |
| 6 | Resistors & capacitors (op-amp filter circuit) | Local (Kjell & Co) or AliExpress component kit | — | 20 |
| | **Total** | | | **~575 SEK** |

## Notes

- The CDM324 **requires** an LM358 preamplifier circuit to produce a stable, readable signal. Do not connect the CDM324 IF output directly to the ESP32 ADC.
- The ESP32+18650 all-in-one board includes onboard TP4056 charging, boost converter, and battery protection — no separate charging circuit needed.
- The standard CDM324 (non-UK, non-F version) is approved for use in Sweden/Europe.
