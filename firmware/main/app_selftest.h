#pragma once
#include <stdbool.h>
#include <stdint.h>

// Runs the golden vectors on the device and remembers the answer.
//
// The guitar has no serial console and gets opened rarely, so the question
// "does this firmware's evaluator still agree with the simulator on this
// silicon?" has to be answerable over WiFi, from a phone, without a rebuild.
// It is run once per boot and cached; /api/selftest re-runs it on demand.

typedef struct {
    bool ran;
    bool ok;
    uint32_t ms;       // how long the run took
    char summary[160]; // one line, ready for /api/status
} app_selftest_t;

// Runs the vectors and updates the cache. Takes a second or two: doubles are
// emulated in software on the ESP32-S3, and the evaluator is full of them by
// design. Do not call it from anywhere that owes somebody a frame.
const app_selftest_t *app_selftest_run(void);

// The cached result, which reads "not run" until the first run finishes.
const app_selftest_t *app_selftest_get(void);
