#pragma once
#include "el_bootstate.h"

// Decides how this boot behaves: whether the radio comes up, on whose terms,
// and whether stored configuration is trusted at all.
//
// The policy itself lives in firmware/core and is tested on the host. This file
// is only the plumbing: persistence, timing, and reading the controls.

// Counts this boot as unhealthy until proved otherwise, then watches the
// five-way for the gesture. Blocks for at most the configured window.
el_radio_mode_t app_mode_decide(void);

// Called once the firmware has proved itself. Clears the boot-loop count, so
// the next power cycle is ordinary again.
void app_mode_mark_healthy(void);

el_radio_mode_t app_mode_current(void);
el_boot_mode_t app_mode_boot(void);
uint8_t app_mode_boot_count(void);
