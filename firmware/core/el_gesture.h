#pragma once
#include <stdbool.h>
#include <stdint.h>

// Recognises the gesture that brings the radio up: a sweep of the five-way from
// one end position to the other, shortly after the firmware starts.
//
// No ESP-IDF here on purpose. This is the part with the interesting edge cases,
// so it is plain C, built into the firmware and run natively by the host tests.

#define EL_SWITCH_POSITIONS  5
#define EL_SWITCH_FIRST      0
#define EL_SWITCH_LAST       (EL_SWITCH_POSITIONS - 1)

// The rotary switch is break-before-make: while it is turning, no contact is
// closed and nothing reads low. That is normal mid-sweep and a fault at rest,
// and the two must not be confused.
#define EL_SWITCH_IN_TRANSIT (-1)

typedef struct {
    uint32_t started_ms;
    uint32_t window_ms;
    bool saw_first;
    bool saw_last;
    bool detected;
} el_gesture_t;

void el_gesture_begin(el_gesture_t *g, uint32_t now_ms, uint32_t window_ms);

// Feed every sampled position, including EL_SWITCH_IN_TRANSIT.
void el_gesture_update(el_gesture_t *g, uint32_t now_ms, int position);

bool el_gesture_detected(const el_gesture_t *g);
bool el_gesture_window_open(const el_gesture_t *g, uint32_t now_ms);
