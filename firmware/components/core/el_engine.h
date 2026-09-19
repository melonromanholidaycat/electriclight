#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "el_eval.h"
#include "el_geometry.h"
#include "el_program.h"

// The render engine: fixed-rate effect clock, layer compositing, and the output
// chain that an effect is not allowed to reach into. Twin of
// web/src/model/engine.js.
//
// The clock is fixed at 60 Hz on purpose. Effects can read their own previous
// frame (`prev`), so a variable frame rate would make trails and decay look
// different on the device than in the simulator - exactly the drift this
// project is built to avoid. A late frame is dropped or repeated, never scaled.

#define EL_FRAME_RATE 60
#define EL_MAX_LAYERS 4

typedef enum {
    EL_BLEND_NORMAL = 0,
    EL_BLEND_ADD,
    EL_BLEND_MAX,
} el_blend_t;

typedef struct {
    bool enabled;
    bool sides[EL_SIDES];
    float from_fret;
    float to_fret;
} el_mask_t;

typedef struct {
    const el_program_t *program;
    const float *params;
    el_mask_t mask;
    el_blend_t blend;
} el_layer_t;

// The output chain's settings. All remotely tunable; none reachable from an
// effect, so no effect can brown out the board or cook the pack.
typedef struct {
    float brightness_ceiling; // the most useful runtime control we have
    float gamma;
    float ma_per_led;         // at full white, all three channels
    float current_budget;     // mA the supply is trusted to deliver
    float idle_current;       // mA per LED with the channels off
} el_output_t;

extern const el_output_t EL_DEFAULT_OUTPUT;

typedef struct {
    el_geometry_t geometry;
    el_layout_t layout;
    el_output_t output;
    uint8_t gamma_lut[256];

    el_layer_t layers[EL_MAX_LAYERS];
    int layer_count;

    uint32_t frame;
    float knob;    // 0..1
    float sw;      // 0..4

    float prev[EL_MAX_LAYERS * EL_MAX_PIXELS];
    float rgb[EL_MAX_PIXELS * 3];
    uint8_t out8[EL_MAX_PIXELS * 3];

    float current;  // mA drawn by the frame just rendered
    bool limited;   // true if the current limiter had to scale it back
} el_engine_t;

bool el_engine_init(el_engine_t *e, const el_geometry_t *g, const el_output_t *out);
void el_engine_set_output(el_engine_t *e, const el_output_t *out);
void el_engine_set_layers(el_engine_t *e, const el_layer_t *layers, int count);
void el_engine_reset(el_engine_t *e);

// Renders one frame and advances the clock. The result is e->out8, laid out
// side 0 first, three bytes per LED in the strips' electrical order.
const uint8_t *el_engine_step(el_engine_t *e);

int el_engine_byte_count(const el_engine_t *e);
