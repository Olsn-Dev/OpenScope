# DIY Golf Launch Monitor

A radar-based golf launch monitor inspired by the Shot Scope LM1. Measures ball speed, club speed, smash factor, carry distance, and total distance using a 24 GHz Doppler radar module and an ESP32 microcontroller. Battery-powered for use on the course.

## Features

- Ball speed (km/h)
- Club head speed (km/h)
- Smash factor
- Carry distance (calculated)
- Total distance (calculated)
- 3.5" color TFT display — no phone required
- Battery-powered (18650 Li-Ion)
- Compact 3D-printed housing

## Technology

Uses a **CDM324 24 GHz K-band Doppler radar** with an LM358 op-amp preamplifier circuit, read by an ESP32 microcontroller. Speed is derived via FFT analysis of the Doppler IF signal.

## Project Phases

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Hardware & wiring | 🔲 Not started |
| 2 | Software & calibration | 🔲 Not started |
| 3 | Housing & battery optimization | 🔲 Not started |
| 4 | Testing & validation | 🔲 Not started |

## Repository Structure

```
golf-launch-monitor/
├── src/
│   └── main.cpp          # ESP32 firmware (PlatformIO)
├── docs/
│   ├── bom.md            # Bill of materials with part numbers
│   └── wiring.md         # Wiring diagram and connection guide
├── cad/
│   └── housing.f3d       # Fusion 360 housing design
├── platformio.ini        # PlatformIO build configuration
├── .gitignore
└── README.md
```

## Quick Start

1. Order components — see [docs/bom.md](docs/bom.md)
2. Wire up the circuit — see [docs/wiring.md](docs/wiring.md)
3. Install [PlatformIO](https://platformio.org/)
4. Clone this repo and open in VS Code
5. Build & upload: `pio run --target upload`

## License

MIT
