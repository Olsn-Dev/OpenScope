#include <Arduino.h>
#include "config.h"
#include "buttons.h"

// Per-button debounce + repeat state. `stable` is the debounced level
// (true = pressed); raw flips only become stable after BTN_DEBOUNCE_MS.
struct BtnState {
    uint8_t  pin;
    bool     raw;         // last raw reading (pressed = true)
    bool     stable;      // debounced state
    uint32_t t_raw;       // when the raw reading last changed
    uint32_t t_press;     // when the debounced press began
    uint32_t t_repeat;    // last auto-repeat event
    bool     long_fired;  // OK only: BTN_OK_LONG already emitted for this hold
};

static BtnState s_up, s_down, s_ok;

static void init_btn(BtnState& b, uint8_t pin)
{
    pinMode(pin, INPUT_PULLUP);
    b = { pin, false, false, millis(), 0, 0, false };
}

void buttons_init()
{
    init_btn(s_up,   PIN_BTN_UP);
    init_btn(s_down, PIN_BTN_DOWN);
    init_btn(s_ok,   PIN_BTN_OK);
}

// Debounce one button; returns +1 on a clean press edge, -1 on a clean
// release edge, 0 otherwise.
static int debounce(BtnState& b)
{
    const bool raw = (digitalRead(b.pin) == LOW);
    const uint32_t now = millis();
    if (raw != b.raw) { b.raw = raw; b.t_raw = now; }
    if (raw == b.stable || now - b.t_raw < BTN_DEBOUNCE_MS) return 0;
    b.stable = raw;
    if (raw) { b.t_press = now; b.t_repeat = now; b.long_fired = false; return 1; }
    return -1;
}

// UP/DOWN: event on press, then auto-repeat while held.
static bool nav_event(BtnState& b)
{
    if (debounce(b) == 1) return true;
    if (b.stable) {
        const uint32_t now = millis();
        if (now - b.t_press >= BTN_REPEAT_DELAY_MS &&
            now - b.t_repeat >= BTN_REPEAT_MS) {
            b.t_repeat = now;
            return true;
        }
    }
    return false;
}

BtnEvent buttons_poll()
{
    if (nav_event(s_up))   return BTN_UP;
    if (nav_event(s_down)) return BTN_DOWN;

    const int edge = debounce(s_ok);
    if (s_ok.stable && !s_ok.long_fired &&
        millis() - s_ok.t_press >= BTN_LONG_MS) {
        s_ok.long_fired = true;          // fires while still held
        return BTN_OK_LONG;
    }
    if (edge == -1 && !s_ok.long_fired) return BTN_OK;   // short press = release
    return BTN_NONE;
}
