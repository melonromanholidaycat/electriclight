#include "el_gesture.h"

void el_gesture_begin(el_gesture_t *g, uint32_t now_ms, uint32_t window_ms)
{
    if (!g) return;
    g->started_ms = now_ms;
    g->window_ms = window_ms;
    g->saw_first = false;
    g->saw_last = false;
    g->detected = false;
}

bool el_gesture_window_open(const el_gesture_t *g, uint32_t now_ms)
{
    if (!g) return false;
    // Unsigned subtraction, so this stays correct when the millisecond counter
    // wraps rather than closing the window for the next 49 days.
    return (uint32_t)(now_ms - g->started_ms) < g->window_ms;
}

void el_gesture_update(el_gesture_t *g, uint32_t now_ms, int position)
{
    if (!g || g->detected) return;
    if (!el_gesture_window_open(g, now_ms)) return;

    // In transit is not a position. Ignoring it rather than treating it as a
    // reset is what lets a sweep read as one movement.
    if (position < EL_SWITCH_FIRST || position > EL_SWITCH_LAST) return;

    if (position == EL_SWITCH_FIRST) g->saw_first = true;
    if (position == EL_SWITCH_LAST) g->saw_last = true;

    // Both ends, in either order. Requiring both means a four-notch travel,
    // which nobody performs by accident while picking up a guitar.
    if (g->saw_first && g->saw_last) g->detected = true;
}

bool el_gesture_detected(const el_gesture_t *g)
{
    return g && g->detected;
}
