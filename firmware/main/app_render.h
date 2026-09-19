#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "el_program.h"
#include "esp_err.h"

// The frame loop: read the controls, run the effect, push the pixels.
//
// Fixed at 60 Hz because effects can read their own previous frame, so a
// variable rate would make trails and decay look different here than in the
// simulator - the drift this whole project is built to avoid. A late frame is
// dropped, never stretched.

// What a frame actually costs, which until step 5 was an estimate. Reported
// over /api/status so the question can be asked of a closed guitar.
typedef struct {
    uint32_t frames;       // rendered since boot
    uint32_t late;         // frames that overran their 16.7 ms slot
    uint32_t last_us;      // the most recent frame
    uint32_t worst_us;     // the worst since boot
    uint32_t avg_us;       // rolling mean
    float current_ma;      // what the output chain thinks the last frame draws
    bool limited;          // whether the current limiter had to pull it back
    int slot;              // the five-way position being played, 0..4
    const char *effect;    // its name
} app_render_stats_t;

// One switch position's worth of new effect, as it arrives from the page.
typedef struct {
    int index;                              // which of the five
    char name[32];
    uint8_t program[EL_MAX_PROGRAM_BYTES];  // ELFX, already base64-decoded
    size_t program_len;
    float params[EL_MAX_PARAMS];
    uint8_t param_count;
} app_slot_update_t;

// How many switch positions there are. The five-way decides this, not the page.
int app_render_slot_count(void);

// Replaces the given slots, all or nothing. Every program is decoded and
// verified first, so a batch with one bad effect in it changes nothing and the
// guitar keeps playing what it was. Returns false with a reason in `err_out`.
bool app_render_set_slots(const app_slot_update_t *updates, int count,
                          char *err_out, size_t err_max);

esp_err_t app_render_start(void);
void app_render_stats(app_render_stats_t *out);

// Switch to the effect in the given slot. Called by the five-way, and by the
// page when someone auditions an effect from the phone.
void app_render_select(int slot);
