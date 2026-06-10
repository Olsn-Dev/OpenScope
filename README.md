![OpenScope](docs/hero.png)

# OpenScope — DIY Golf Launch Monitor

A radar-based golf launch monitor. Measures ball speed, club speed,
**launch angle**, **side angle**, carry distance and total distance
using three 24 GHz Doppler radar modules and an ESP32 microcontroller.
Battery-powered, no phone required.

## Features

- Ball speed (km/h or mph)
- Club head speed
- **Launch angle** — measured from ground/top radar Doppler ratio
- **Side angle** — L/R deviation in degrees (e.g. `R 2.3°` or `STRAIGHT`)
- Carry & total distance — physics-corrected using actual launch angle
- Smash factor
- 3.5" color **touch** TFT display (ILI9488 + XPT2046)
- Per-club statistics (avg, best carry) stored in flash
- Deep sleep with one-button wake
- Calibration mode with live FFT spectrum

## Technology

Three **CDM324 24 GHz K-band Doppler radars** feed into an LM358
preamplifier. The ESP32 runs a 1024-point FFT on all three channels
simultaneously and solves for side angle, launch angle and true ball
speed using the 3D Doppler equations:

```
Radar L & R (ground, 90° V-formation, 45° per arm):
  f_L = k·cos(α)·cos(β − 45°)
  f_R = k·cos(α)·cos(β + 45°)

  Side angle β:  f_L/f_R = cos(β−45°)/cos(β+45°)  → binary search

Radar T (top, tilted 20° upward):
  F = (f_L + f_R) / (2·cos(β)·cos(45°))    [horizontal speed proxy]
  tan(α) = (f_T/F − cos(β)·cos(20°)) / sin(20°)   → launch angle α

True ball speed:  k = F / cos(α),   v = k · 0.022384 km/h
```

## Project Phases

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Hardware & wiring | 🟡 In progress (v0.7) |
| 2 | Software & calibration | 🟡 In progress (v0.7) |
| 3 | Physical prototyping | 🔲 Not started |
| 4 | Housing & battery optimization | 🔲 Not started |
| 5 | Testing & validation | 🔲 Not started |

## Repository Structure

```
golf-launch-monitor/
├── src/
│   └── main.cpp          # ESP32 firmware (PlatformIO)
├── docs/
│   ├── bom.md            # Bill of materials with part numbers
│   ├── wiring.md         # Wiring diagram, radar mounting angles
│   └── openscope-ui.png  # UI mockup
├── platformio.ini        # PlatformIO build configuration
└── README.md
```

## Quick Start

1. Order components — see [docs/bom.md](docs/bom.md)
2. Wire up the circuit — see [docs/wiring.md](docs/wiring.md)
3. Mount the radars at the correct angles (see below)
4. Install [PlatformIO](https://platformio.org/)
5. Clone this repo and open in VS Code
6. Build & upload: `pio run --target upload`
7. Calibrate — see below

## Radar Mounting

Place the entire unit **behind the golfer**, ~0.5–1 m back and slightly
to the side — the same setup as commercial systems like Trackman.
The ball flies *away* from the unit; Doppler works identically.

```
Top view:

 [Unit]  ←  0.5–1 m  →  [Golfer]  →  ●  →  →  →  target
              behind                  ball
```

### Ground radars — V-formation (Radar L & R)

```
Top view (looking down):

            ↑  target / ball flight

            /\   ← 90° at the vertex
           /  \
          / 45°\ 45°
        [L]    [R]
       GPIO34  GPIO35

V-tip points toward target. Unit placed 0.5–1 m behind golfer.
```

- Mount both flat on the ground, each arm **45° from the shot line**.
- Aim the V-tip toward the ball impact point.
- Place **0.5–1 m behind the golfer**, slightly to the side of the swing path.

### Top radar — launch angle (Radar T)

```
Side view:

  [Unit]         [Golfer]    ●  →  ↗  Ball trajectory
  0.5–1 m back              tee  /
  Radar T ──────────────────────►  20° above horizontal (GPIO32)
```

- Mount tilted **20° upward**. Use a printed wedge or protractor.
- If you use a different angle, update `RADAR_T_ANGLE_DEG` in `src/config.h`.

## Controls

**Touch screen + one Power button.** All navigation is by touch; the only
physical control is Power.

| Where | Touch | Power |
|-------|-------|-------|
| Ready | Tap **club circle** → next club · tap **SETTINGS** bar | Hold 2 s → sleep |
| Result | Tap **anywhere** → dismiss | Hold 2 s → sleep |
| Settings | Tap a **row** · tap **DONE** → exit | Hold 2 s → exit |
| Calibration | `[−10]` `[SAVE]` `[+10]` buttons | Hold 2 s → save + exit |

> On first boot (or via Settings → **Touch Cal.**) the unit runs a quick
> 4-corner touch calibration, stored in flash.

## Display Layout

The firmware has four screens:

### Ready / Result — shared 3×2 tile grid

```
┌──────────┬──────────┬──────────┐
│  CLUB    │  BALL    │  LAUNCH  │
│  98      │  152     │  19.4    │
│  km/h    │  km/h    │  °       │
├──────────┼──────────┼──────────┤
│  CARRY   │  TOTAL   │   (7I)   │
│  187     │  209     │          │
│  m       │  m       │          │
└──────────┴──────────┴──────────┘
 R 2.3°                 smash 1.55
```

- **Launch** tile turns green when a valid angle is computed; `--` (dimmed) if unavailable.
- **Side angle** shown bottom-left: `R 2.3°`, `L 1.1°`, or `STRAIGHT` (< 0.5°).

### Settings screen

Reached by tapping the **SETTINGS** bar on the main screen.

```
┌─────────────────────────────────────────────┐
│  Settings                      tap an item   │
├─────────────────────────────────────────────┤
│▌ Units                               km/h   │
│▌ Reset Stats                            7I  │
│▌ Radar Cal.                             ►   │
│▌ Touch Cal.                             ►   │
├─────────────────────────────────────────────┤
│                    DONE                      │
└─────────────────────────────────────────────┘
```

| Item | Action |
|------|--------|
| Units | Toggle km/h ↔ mph |
| Reset Stats | Clears avg/best for the active club |
| Radar Cal. | Opens the detection-threshold calibration screen |
| Touch Cal. | Re-runs the 4-corner touch calibration |

## Calibration

The detection threshold tells the firmware what signal level counts as
a real shot vs. background noise. It is saved to flash.

### Enter calibration

From the main screen, tap **SETTINGS** → tap **Radar Cal.**

```
┌──────────────────────────────────────────────────────┐
│  CALIBRATION MODE              tap the buttons below  │
├──────────────────────────────────────────────────────┤
│  [live FFT spectrum — teal bars below threshold,      │
│   red bars above — yellow line = threshold]           │
├───────────────┬───────────────┬──────────────────────┤
│ NOISE FLOOR   │ PEAK MAG      │ MAX SEEN             │
│ 14.2          │ 14.2          │ 14.2                 │
├───────────────┴───────────────┴──────────────────────┤
│ PEAK 0 Hz = 0.0 km/h                                  │
│ THRESHOLD 80         SUGGESTED 58                     │
├───────────────┬───────────────┬──────────────────────┤
│     −10       │     SAVE      │       +10            │
└───────────────┴───────────────┴──────────────────────┘
```

### Steps

1. Leave the device still for ~30 s. Note **NOISE FLOOR** (typically 8–20).
2. Take a full practice swing. Note **MAX SEEN** (typically 200–600).
3. Tap **`−10`** / **`+10`** to move the yellow threshold line between
   noise floor and MAX SEEN. The **SUGGESTED** value (`noise × 4`) is a
   good starting point.
4. Tap **SAVE** (or hold **Power 2 s**) to save and return.

| Measurement | Typical value |
|-------------|--------------|
| Noise floor | 10–20 |
| Shot peak | 200–600 |
| Good threshold | 60–100 |

> Raise the threshold if you get false triggers. Lower it if real shots
> are not detected.

## How it works

The CDM324 emits a continuous 24.125 GHz signal. Moving objects
Doppler-shift the reflected signal:

```
f_d = 2 × v × f_c / c
v [km/h] = f_d [Hz] × 0.022384
```

The ESP32 samples both radar channels simultaneously at 40 kHz and runs
a 1024-point FFT (~25 ms window). Two peaks per frame are searched —
the lower frequency is club head speed, the higher is ball speed.

**Side angle** is computed from the L/R frequency ratio (independent of
speed and launch angle) via a 40-iteration binary search. **Launch angle**
is then solved directly using the three-radar 3D formula — no binary
search needed. Ball speed `k` is fully corrected for both angles, and
carry is scaled using the trajectory shape (`sin(2α)`) relative to each
club's typical launch angle.

## UI Mockup

![OpenScope UI — all four screens](docs/openscope-ui.png?v=2)

## License

MIT
