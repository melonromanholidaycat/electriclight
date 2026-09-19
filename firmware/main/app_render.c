#include "app_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_inputs.h"
#include "app_leds.h"
#include "el_engine.h"
#include "el_program.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "generated/el_default_effects.h"

static const char *TAG = "render";

#define FRAME_US (1000000 / EL_FRAME_RATE)

// The render loop owns core 1. WiFi and the HTTP server live on core 0, and a
// frame that has to wait for a beacon is a frame that shows up late on the
// neck. This is the one place in the firmware where that matters.
#define RENDER_CORE 1
#define RENDER_PRIORITY 5
#define RENDER_STACK 4096

#define SLOT_COUNT EL_DEFAULT_SLOT_COUNT

typedef struct {
    el_effect_t effect;
    float params[EL_MAX_PARAMS];
    uint8_t param_count;
    // Points either at a built-in's static name or at `owned_name`, so a slot
    // replaced from the page does not leave the stats pointing at freed memory.
    const char *name;
    char owned_name[32];
    bool valid;
} slot_t;

static el_engine_t s_engine;
static slot_t s_slots[SLOT_COUNT];
static int s_slot = 0;
static int s_requested = 0;

static app_render_stats_t s_stats;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// The render task runs on core 1 and the HTTP server on core 0, so an upload
// can land in the middle of a frame. The engine's layer list points straight
// into the slot table, so swapping an effect while a frame is being stepped is
// a use-after-free in all but name. This guards the step and the swap against
// each other; it is held for about a frame's compute, which an upload can
// afford to wait for.
static SemaphoreHandle_t s_engine_lock;

// Loads the five effects a board plays before anyone has given it any. Step 5's
// stored library replaces these one slot at a time; until then this is what the
// neck shows, which is the same thing the simulator opens with.
static void load_defaults(void)
{
    for (int i = 0; i < SLOT_COUNT; i++) {
        const el_default_effect_t *d = &EL_DEFAULT_EFFECTS[i];
        el_decode_err_t err = el_program_decode(&s_slots[i].effect, d->program, d->program_len);
        if (err != EL_DECODE_OK) {
            // A built-in that does not decode is a build problem, not a runtime
            // one, so say so loudly and leave the slot dark rather than guess.
            ESP_LOGE(TAG, "default effect %d (%s) will not decode: %s",
                     i, d->name, el_decode_error(err));
            s_slots[i].valid = false;
            continue;
        }
        s_slots[i].param_count = d->param_count;
        for (int p = 0; p < d->param_count && p < EL_MAX_PARAMS; p++) {
            s_slots[i].params[p] = d->params[p];
        }
        s_slots[i].name = d->name;
        s_slots[i].valid = true;
    }
}

static void apply_slot(int slot)
{
    if (slot < 0 || slot >= SLOT_COUNT) return;
    if (!s_slots[slot].valid) return;

    el_layer_t layer = {
        .program = &s_slots[slot].effect.program,
        .params = s_slots[slot].params,
        .mask = { .enabled = false },
        .blend = EL_BLEND_NORMAL,
    };
    el_engine_set_layers(&s_engine, &layer, 1);
    el_engine_reset(&s_engine);
    s_slot = slot;
    ESP_LOGI(TAG, "slot %d: %s", slot, s_slots[slot].name);
}

void app_render_select(int slot)
{
    if (slot < 0 || slot >= SLOT_COUNT) return;
    s_requested = slot;
}

int app_render_slot_count(void) { return SLOT_COUNT; }

void app_render_set_output(const el_output_t *output)
{
    if (!s_engine_lock) return;
    xSemaphoreTake(s_engine_lock, portMAX_DELAY);
    el_engine_set_output(&s_engine, output);
    xSemaphoreGive(s_engine_lock);
    ESP_LOGI(TAG, "output: ceiling %.2f, gamma %.2f, budget %.0f mA",
             (double)output->brightness_ceiling, (double)output->gamma,
             (double)output->current_budget);
}

void app_render_get_output(el_output_t *out) { *out = s_engine.output; }

bool app_render_set_slots(const app_slot_update_t *updates, int count,
                          char *err_out, size_t err_max)
{
    // Decode everything into scratch first. The render task is running on the
    // other core off these structures, so nothing may be half-written: a batch
    // that fails has to leave the guitar playing exactly what it was.
    if (!s_engine_lock) {
        snprintf(err_out, err_max, "the render loop is not running");
        return false;
    }

    // Checked in one pass, then applied in another. Decoding is deterministic,
    // so a program that passes here passes again below - which means the second
    // pass cannot fail and the swap needs no rollback. One scratch buffer does
    // the checking; keeping a full copy per slot would be five kilobytes each,
    // and an upload already costs enough heap.
    el_effect_t *scratch = calloc(1, sizeof *scratch);
    if (!scratch) {
        snprintf(err_out, err_max, "out of memory");
        return false;
    }

    for (int i = 0; i < count; i++) {
        const app_slot_update_t *u = &updates[i];
        if (u->index < 0 || u->index >= SLOT_COUNT) {
            snprintf(err_out, err_max, "slot %d does not exist; there are %d",
                     u->index, SLOT_COUNT);
            free(scratch);
            return false;
        }
        const el_decode_err_t err =
            el_program_decode(scratch, u->program, u->program_len);
        if (err != EL_DECODE_OK) {
            snprintf(err_out, err_max, "slot %d (%s): %s",
                     u->index, u->name, el_decode_error(err));
            free(scratch);
            return false;
        }
        if (scratch->program.params > u->param_count) {
            snprintf(err_out, err_max, "slot %d (%s) wants %d parameters, %d given",
                     u->index, u->name, scratch->program.params, u->param_count);
            free(scratch);
            return false;
        }
    }
    free(scratch);

    // Past this point nothing can fail, so the swap is safe to do in place -
    // once the render task is out of the frame it was stepping.
    xSemaphoreTake(s_engine_lock, portMAX_DELAY);
    for (int i = 0; i < count; i++) {
        const app_slot_update_t *u = &updates[i];
        slot_t *slot = &s_slots[u->index];
        slot->valid = false; // the render task skips an invalid slot
        el_program_decode(&slot->effect, u->program, u->program_len);
        memcpy(slot->params, u->params, sizeof slot->params);
        slot->param_count = u->param_count;
        strlcpy(slot->owned_name, u->name, sizeof slot->owned_name);
        slot->name = slot->owned_name;
        slot->valid = true;
    }
    // Re-arm whatever is playing, in case it was one of the slots replaced.
    apply_slot(s_slot);
    xSemaphoreGive(s_engine_lock);
    return true;
}

static void render_task(void *arg)
{
    (void)arg;
    const TickType_t t0 = xTaskGetTickCount();
    uint64_t frame = 0;
    uint64_t sum_us = 0;
    uint32_t sum_n = 0;
    // Until a switch position has been seen even once, assume nothing is wired
    // to this board. A bare board on a desk has a floating ADC pin where the
    // potentiometer should be, and trusting it would render the neck at
    // whatever that pin happened to settle at - which we already know reads as
    // "the board is broken" to anyone watching.
    bool controls_seen = false;

    for (;;) {
        const int64_t started = esp_timer_get_time();

        app_inputs_t in;
        app_inputs_read(&in);
        // A switch mid-sweep reports EL_SWITCH_IN_TRANSIT and is ignored rather
        // than treated as a change: the neck should not flicker through four
        // effects while somebody turns the knob to the one they want.
        if (in.position >= 0) controls_seen = true;
        xSemaphoreTake(s_engine_lock, portMAX_DELAY);
        if (s_requested != s_slot) apply_slot(s_requested);
        if (in.position >= 0 && in.position != s_slot) apply_slot(in.position);
        s_engine.knob = controls_seen ? in.brightness : 1.0f;
        s_engine.sw = (float)s_slot;
        const uint8_t *rgb = el_engine_step(&s_engine);
        xSemaphoreGive(s_engine_lock);

        // Outside the lock on purpose. The frame buffer the strips are written
        // from is not touched by a slot swap, and this is the better part of a
        // millisecond to be holding anything.
        app_leds_write(rgb, s_engine.layout.count);

        const uint32_t took = (uint32_t)(esp_timer_get_time() - started);
        sum_us += took;
        sum_n++;

        portENTER_CRITICAL(&s_lock);
        s_stats.frames++;
        s_stats.last_us = took;
        if (took > s_stats.worst_us) s_stats.worst_us = took;
        if (took > FRAME_US) s_stats.late++;
        s_stats.avg_us = (uint32_t)(sum_us / sum_n);
        s_stats.current_ma = s_engine.current;
        s_stats.limited = s_engine.limited;
        s_stats.slot = s_slot;
        s_stats.effect = s_slots[s_slot].valid ? s_slots[s_slot].name : "none";
        portEXIT_CRITICAL(&s_lock);

        // Scheduled against an absolute deadline rather than a fixed delay.
        // One frame is 16.67 ms and a tick is 1 ms, so pdMS_TO_TICKS(1000/60)
        // truncates to 16 and the clock would run 4% fast - which would be
        // wrong in exactly the way the fixed clock exists to prevent. Deriving
        // each deadline from the frame number keeps the average exact and lets
        // a slow frame be absorbed rather than accumulated.
        frame++;
        const TickType_t target =
            t0 + (TickType_t)((frame * configTICK_RATE_HZ) / EL_FRAME_RATE);
        const TickType_t now = xTaskGetTickCount();
        if ((int32_t)(target - now) > 0) vTaskDelay(target - now);
    }
}

esp_err_t app_render_start(void)
{
    const app_config_t *cfg = app_config_get();
    (void)cfg;

    el_geometry_t geometry = EL_DEFAULT_GEOMETRY;
    if (!el_engine_init(&s_engine, &geometry, &EL_DEFAULT_OUTPUT)) {
        ESP_LOGE(TAG, "geometry will not fit this build");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = app_leds_init(geometry.leds_per_strip);
    if (err != ESP_OK) return err;

    s_engine_lock = xSemaphoreCreateMutex();
    if (!s_engine_lock) return ESP_ERR_NO_MEM;

    load_defaults();
    apply_slot(0);

    if (xTaskCreatePinnedToCore(render_task, "render", RENDER_STACK, NULL,
                                RENDER_PRIORITY, NULL, RENDER_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "rendering at %d Hz on core %d", EL_FRAME_RATE, RENDER_CORE);
    return ESP_OK;
}

void app_render_stats(app_render_stats_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_stats;
    portEXIT_CRITICAL(&s_lock);
}
