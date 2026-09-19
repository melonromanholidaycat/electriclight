#include "el_selftest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el_engine.h"
#include "el_geometry.h"
#include "el_program.h"
#include "generated/el_vectors.h"

const el_vectors_contract_t *el_vectors_contract(void)
{
    static const el_vectors_contract_t contract = {
        .format_version = EL_VECTORS_FORMAT_VERSION,
        .frame_rate = EL_VECTORS_FRAME_RATE,
        .func_count = EL_VECTORS_FUNC_COUNT,
        .var_count = EL_VECTORS_VAR_COUNT,
        .func_arity = EL_VECTORS_FUNC_ARITY,
        .case_count = EL_VECTORS_CASE_COUNT,
        .byte_count = EL_VECTORS_BYTE_COUNT,
    };
    return &contract;
}

// Often enough that the longest uninterrupted stretch is a fraction of a
// second, rather than the seconds a whole case takes.
#define EL_SELFTEST_YIELD_EVERY 16

bool el_selftest(el_selftest_result_t *result, el_selftest_yield_fn yield, void *ctx)
{
    memset(result, 0, sizeof *result);
    result->cases_total = EL_VECTORS_CASE_COUNT;
    result->case_index = -1;

    el_engine_t *engine = malloc(sizeof *engine);
    el_effect_t *effect = malloc(sizeof *effect);
    if (!engine || !effect) {
        free(engine);
        free(effect);
        result->error = "out of memory";
        return false;
    }

    bool ok = true;

    for (int ci = 0; ci < EL_VECTORS_CASE_COUNT && ok; ci++) {
        const el_vector_case_t *c = &EL_VECTORS[ci];

        el_decode_err_t err = el_program_decode(effect, c->program, c->program_len);
        if (err != EL_DECODE_OK) {
            result->case_index = ci;
            result->case_id = c->id;
            result->error = el_decode_error(err);
            ok = false;
            break;
        }

        if (!el_engine_init(engine, &EL_VECTORS_GEOMETRY, c->output)) {
            result->case_index = ci;
            result->case_id = c->id;
            result->error = "geometry too large for this build";
            ok = false;
            break;
        }
        engine->knob = c->knob;
        engine->sw = c->sw;

        el_layer_t layer = {
            .program = &effect->program,
            .params = c->params,
            .mask = { .enabled = false },
            .blend = EL_BLEND_NORMAL,
        };
        el_engine_set_layers(engine, &layer, 1);

        // Every frame has to be rendered, not just the sampled ones: `prev`
        // makes the effect a state machine, so skipping a frame changes the
        // answer. That is the whole reason the clock is fixed.
        int next = 0;
        int last = c->frames[c->frame_count - 1];
        bool limited = false;
        for (int frame = 0; frame <= last; frame++) {
            if (yield && (frame % EL_SELFTEST_YIELD_EVERY) == 0) yield(ctx);
            const uint8_t *got = el_engine_step(engine);
            limited = limited || engine->limited;
            if (next >= c->frame_count || c->frames[next] != frame) continue;

            const uint8_t *want = c->expect + (size_t)next * EL_VECTORS_BYTE_COUNT;
            result->frames_checked++;
            next++;

            int n = el_engine_byte_count(engine);
            if (n != EL_VECTORS_BYTE_COUNT) {
                result->case_index = ci;
                result->case_id = c->id;
                result->error = "pixel count does not match the vectors";
                ok = false;
                break;
            }
            for (int b = 0; b < n; b++) {
                if (got[b] == want[b]) continue;
                result->case_index = ci;
                result->case_id = c->id;
                result->frame = frame;
                result->byte_index = b;
                result->expected = want[b];
                result->got = got[b];
                ok = false;
                break;
            }
            if (!ok) break;
        }
        // A case that was recorded as clipping must clip here too. Matching
        // pixels are not enough on their own: if the limiter silently stopped
        // running, the frames would still agree while the guitar drew whatever
        // it liked from the pack.
        if (ok && c->limited && !limited) {
            result->case_index = ci;
            result->case_id = c->id;
            result->error = "the current limiter did not run when it should have";
            ok = false;
        }
        if (ok) result->cases_passed++;
    }

    free(engine);
    free(effect);
    return ok;
}

int el_selftest_describe(const el_selftest_result_t *r, char *out, int max)
{
    if (max <= 0) return 0;
    int n;
    if (r->case_index < 0) {
        n = snprintf(out, (size_t)max, "%d/%d cases, %d frames, all match",
                     r->cases_passed, r->cases_total, r->frames_checked);
    } else if (r->error) {
        n = snprintf(out, (size_t)max, "%s: %s", r->case_id ? r->case_id : "?", r->error);
    } else {
        n = snprintf(out, (size_t)max,
                     "%s frame %d byte %d: expected %d, got %d",
                     r->case_id, r->frame, r->byte_index, r->expected, r->got);
    }
    if (n < 0) { out[0] = '\0'; return 0; }
    return n < max ? n : max - 1;
}
