#pragma once

// ─── 3-button input ───────────────────────────────────────────────────────────
// UP / DOWN / OK, each wired GPIO → GND (internal pull-ups). OK doubles as the
// power button: a short press is a select, holding it BTN_LONG_MS fires
// BTN_OK_LONG (power off), and it wakes the unit from deep sleep.
//
// Events are edge-style: UP/DOWN fire on press and then auto-repeat while
// held (for scrolling lists / adjusting values); OK fires on *release* so a
// short press can be told apart from a power-off hold.

enum BtnEvent { BTN_NONE = 0, BTN_UP, BTN_DOWN, BTN_OK, BTN_OK_LONG };

// Configure the button GPIOs. Call once in setup().
void buttons_init();

// Non-blocking poll; returns at most one event per call. Call it frequently
// (every few ms) from every UI loop.
BtnEvent buttons_poll();
