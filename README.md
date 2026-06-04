<img width="1418" height="672" alt="5980D860-2881-47B6-B055-FEBFA808417B_1_201_a" src="https://github.com/user-attachments/assets/8935de40-4522-4e0e-a2bf-af1001c883e9" />

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
| 2 | Software & calibration | 🟡 In progress (v0.3) |
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
6. Calibrate — see below

## Controls

The single button is **GPIO0** (the boot button on most ESP32 dev boards).

| Press duration | Normal mode | Calibration mode |
|----------------|-------------|------------------|
| Short < 1 s | Cycle club | Threshold +10 |
| Medium 1–3 s | Reset stats for current club | Threshold −10 |
| Long > 3 s | Enter calibration mode | Save threshold + exit |

## Calibration

The detection threshold tells the firmware what signal level counts as a real shot vs. background noise. It is saved to flash so you only need to calibrate once (or after changing hardware).

### Step 1 — Enter calibration mode

Hold the button for **3 seconds** from the main screen. The display switches to a live FFT spectrum showing signal strength across the detection window (40–360 km/h).

```
┌──────────────────────────────────────────────────────┐
│  CALIBRATION MODE          +short / -long / save=3s  │
├──────────────────────────────────────────────────────┤
│                                                       │
│   [live spectrum bars — green below threshold,        │
│    red above — yellow line = current threshold]       │
│                                                       │
├───────────────┬───────────────┬──────────────────────┤
│ NOISE FLOOR   │ PEAK MAG      │ MAX SEEN             │
│ 11.4          │ 13.2          │ 13.2                 │
├───────────────┴───────────────┴──────────────────────┤
│ PEAK FREQ:  ---                                       │
│ THRESHOLD: 80        SUGGESTED: 46  (noise x4)       │
└──────────────────────────────────────────────────────┘
```

### Step 2 — Measure the noise floor

Leave the device **completely still** for ~30 seconds without swinging. Watch **NOISE FLOOR** settle to a steady value (typically 8–20 depending on your environment and circuit).

### Step 3 — Measure a real shot

Take a **full swing** (or swing a club without a ball). Watch **MAX SEEN** jump — this is the signal level from a real shot (typically 200–600). The bars will turn red and a white tick marks the peak frequency.

### Step 4 — Set the threshold

The yellow horizontal line on the spectrum is the threshold. A good value sits **clearly above the noise floor** but **well below MAX SEEN** — roughly `noise floor × 4` (the **SUGGESTED** value is a good starting point).

- **Short press** → threshold +10
- **Medium press (1 s)** → threshold −10

Adjust until the yellow line is comfortably above background noise but would be crossed by any real swing.

### Step 5 — Save

Hold the button **3 seconds**. The threshold is saved to flash and the device returns to normal mode.

### Example values

| Measurement | Typical value | Meaning |
|-------------|--------------|---------|
| Noise floor | 10–20 | Background with no movement |
| Shot peak | 200–600 | Signal from a real swing |
| Suggested threshold | 40–80 | `noise × 4` |
| Good threshold | 60–100 | Above noise, well below shots |

> **Tip:** If you get false triggers (random hits without swinging), raise the threshold. If real shots are not detected, lower it.

## How it works

The CDM324 radar emits a continuous 24.125 GHz signal. When a golf club or ball moves through the beam, the reflected signal is Doppler-shifted by:

```
f_doppler = 2 × v × f_carrier / c
```

The ESP32 samples the Doppler IF signal at 40 kHz and runs a 1024-point FFT every ~25 ms. Two peaks are searched for in each frame — the lower frequency is interpreted as club head speed and the higher as ball speed. Carry and total distance are estimated using empirical per-club multipliers.

## License

MIT
