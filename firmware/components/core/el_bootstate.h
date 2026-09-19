#pragma once
#include <stdbool.h>
#include <stdint.h>

// What mode the firmware comes up in, and why. Pure policy, no ESP-IDF: the
// decisions here are the ones that decide whether a closed guitar can be
// reached at all, so they are worth testing without needing a guitar.

// Three consecutive boots that never reached a healthy state. Two is too eager
// - a single unlucky power cycle mid-boot should not change how the instrument
// behaves - and four makes the rescue take too long to arrive.
#define EL_BOOTLOOP_THRESHOLD 3

typedef enum {
    EL_BOOT_NORMAL = 0,
    EL_BOOT_SAFE,          // reached without anyone present, by boot-loop count
} el_boot_mode_t;

typedef enum {
    EL_RADIO_OFF = 0,      // the default, and what the brief asks for
    EL_RADIO_ON,           // gesture performed: stored configuration, normal service
    EL_RADIO_SAFE,         // stored configuration ignored, compiled-in fallback AP
} el_radio_mode_t;

// The count to persist as this boot begins. Saturates rather than wrapping,
// because wrapping would quietly walk out of safe mode.
uint8_t el_bootstate_next(uint8_t stored);

// What to persist once the firmware has proved itself. Also applies to safe
// mode: a rescue that came up and was reachable has done its job, and the next
// power cycle should be ordinary again rather than stuck.
uint8_t el_bootstate_on_healthy(void);

el_boot_mode_t el_bootstate_mode(uint8_t count, uint8_t threshold);

// always_on exists because the controls are not wired yet: on a bare board no
// gesture is performable, and without this the radio could never come up. It is
// a stored setting, and it is turned off once the guitar can answer for itself.
el_radio_mode_t el_radio_decide(el_boot_mode_t boot,
                                bool gesture,
                                bool brightness_low,
                                bool always_on);

const char *el_radio_name(el_radio_mode_t mode);
