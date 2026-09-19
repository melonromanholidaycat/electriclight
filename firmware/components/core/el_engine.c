#include "el_engine.h"

#include <math.h>
#include <string.h>

const el_output_t EL_DEFAULT_OUTPUT = {
    .brightness_ceiling = 0.5f,
    .gamma = 2.2f,
    .ma_per_led = 60.0f,    // WS2812B at full white, all three channels
    .current_budget = 1500.0f,
    .idle_current = 1.0f,
};

static void gamma_table(uint8_t *t, double gamma)
{
    for (int i = 0; i < 256; i++)
        t[i] = (uint8_t)el_js_round(255.0 * pow((double)i / 255.0, gamma));
}

// HSV -> linear RGB. A literal port of hsvToRgb() in web/src/model/engine.js,
// including the JavaScript remainder operator (fmod, sign of the dividend) and
// the wrap that lets an effect let its hue run away to 10000 degrees.
static void hsv_to_rgb(float hf, float sf, float vf, float *out)
{
    double h = hf, s = sf, v = vf;
    h = h - 360.0 * floor(h / 360.0);
    s = s < 0.0 ? 0.0 : (s > 1.0 ? 1.0 : s);
    v = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
    double c = v * s;
    double hp = h / 60.0;
    double x = c * (1.0 - fabs(fmod(hp, 2.0) - 1.0));
    double r = 0.0, g = 0.0, b = 0.0;
    if (hp < 1.0)      { r = c; g = x; }
    else if (hp < 2.0) { r = x; g = c; }
    else if (hp < 3.0) { g = c; b = x; }
    else if (hp < 4.0) { g = x; b = c; }
    else if (hp < 5.0) { r = x; b = c; }
    else               { r = c; b = x; }
    // A NaN hue falls through every comparison to this last branch in the
    // JavaScript too, and the NaN then survives to the quantiser below.
    double m = v - c;
    out[0] = (float)(r + m);
    out[1] = (float)(g + m);
    out[2] = (float)(b + m);
}

bool el_engine_init(el_engine_t *e, const el_geometry_t *g, const el_output_t *out)
{
    memset(e, 0, sizeof *e);
    if (!el_layout_build(&e->layout, g)) return false;
    e->geometry = *g;
    e->output = out ? *out : EL_DEFAULT_OUTPUT;
    gamma_table(e->gamma_lut, e->output.gamma);
    e->knob = 1.0f;
    e->sw = 0.0f;
    return true;
}

void el_engine_set_output(el_engine_t *e, const el_output_t *out)
{
    e->output = *out;
    gamma_table(e->gamma_lut, e->output.gamma);
}

void el_engine_set_layers(el_engine_t *e, const el_layer_t *layers, int count)
{
    if (count > EL_MAX_LAYERS) count = EL_MAX_LAYERS;
    for (int i = 0; i < count; i++) e->layers[i] = layers[i];
    e->layer_count = count;
    memset(e->prev, 0, sizeof e->prev);
}

void el_engine_reset(el_engine_t *e)
{
    e->frame = 0;
    memset(e->prev, 0, sizeof e->prev);
    memset(e->rgb, 0, sizeof e->rgb);
}

int el_engine_byte_count(const el_engine_t *e) { return e->layout.count * 3; }

// Effect output -> master brightness -> gamma -> ceiling -> current limit.
//
// The knob goes in before gamma because a player turning it down wants a
// perceptual dim. The ceiling goes in after, because it is a power control:
// half the ceiling has to mean half the current, which is only true on the
// linear side of gamma.
static void finish(el_engine_t *e)
{
    const el_output_t *o = &e->output;
    double master = e->knob;
    double per_channel = (double)o->ma_per_led / 3.0;
    int n = e->layout.count * 3;
    double sum = 0.0;

    for (int i = 0; i < n; i++) {
        double lin = (double)e->rgb[i] * master;
        double idx = el_js_round(lin * 255.0);
        double b;
        if (isnan(idx)) {
            // In the JavaScript the NaN indexes the lookup table, reads
            // undefined, and lands in a Uint8Array as 0. Same answer, said out
            // loud: a pixel whose maths went wrong is off, not full white.
            b = 0.0;
        } else {
            if (idx < 0.0) idx = 0.0;
            if (idx > 255.0) idx = 255.0;
            b = el_js_round((double)e->gamma_lut[(int)idx] * (double)o->brightness_ceiling);
        }
        e->out8[i] = (uint8_t)(b < 0.0 ? 0.0 : (b > 255.0 ? 255.0 : b));
        sum += b;
    }

    double idle = (double)e->layout.count * (double)o->idle_current;
    double current = (sum / 255.0) * per_channel + idle;
    if (current > (double)o->current_budget) {
        double room = (double)o->current_budget - idle;
        if (room < 0.0) room = 0.0;
        double denom = current - idle;
        if (denom < 1e-6) denom = 1e-6;
        double scale = room / denom;
        double rescaled = 0.0;
        for (int i = 0; i < n; i++) {
            double v = el_js_round((double)e->out8[i] * scale);
            e->out8[i] = (uint8_t)(v < 0.0 ? 0.0 : (v > 255.0 ? 255.0 : v));
            rescaled += e->out8[i];
        }
        current = (rescaled / 255.0) * per_channel + idle;
        e->limited = true;
    } else {
        e->limited = false;
    }
    e->current = (float)current;
}

const uint8_t *el_engine_step(el_engine_t *e)
{
    int n_px = e->layout.count;
    float vars[EL_VAR_SLOTS];
    float out[3];
    float tmp[3];

    memset(e->rgb, 0, sizeof(float) * (size_t)n_px * 3);

    vars[EL_VAR_T] = (float)((double)e->frame / EL_FRAME_RATE);
    vars[EL_VAR_COUNT] = e->geometry.leds_per_strip;
    vars[EL_VAR_NFRETS] = e->geometry.frets;
    vars[EL_VAR_KNOB] = e->knob;
    vars[EL_VAR_SW] = e->sw;

    for (int li = 0; li < e->layer_count; li++) {
        const el_layer_t *layer = &e->layers[li];
        float *prev = e->prev + (size_t)li * EL_MAX_PIXELS;

        for (int i = 0; i < n_px; i++) {
            const el_pixel_t *p = &e->layout.pixels[i];
            if (layer->mask.enabled) {
                if (!layer->mask.sides[p->side]) continue;
                if (p->fret < layer->mask.from_fret || p->fret > layer->mask.to_fret) continue;
            }
            vars[EL_VAR_FRET] = p->fret;
            vars[EL_VAR_U] = p->u;
            vars[EL_VAR_SIDE] = (float)p->side;
            vars[EL_VAR_N] = (float)p->n;
            vars[EL_VAR_PREV] = prev[i];
            vars[EL_VAR_RND] = e->layout.randoms[i];

            if (!el_eval(layer->program, vars, layer->params, out)) {
                out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f;
            }
            float v = out[2] < 0.0f ? 0.0f : (out[2] > 1.0f ? 1.0f : out[2]);
            prev[i] = v;

            hsv_to_rgb(out[0], out[1], v, tmp);
            int o = i * 3;
            if (layer->blend == EL_BLEND_ADD) {
                e->rgb[o] += tmp[0];
                e->rgb[o + 1] += tmp[1];
                e->rgb[o + 2] += tmp[2];
            } else if (layer->blend == EL_BLEND_MAX) {
                // Math.max, so a NaN channel propagates rather than being
                // quietly dropped in favour of whatever was underneath.
                for (int ch = 0; ch < 3; ch++) {
                    float a = e->rgb[o + ch], b = tmp[ch];
                    e->rgb[o + ch] = (isnan(a) || isnan(b)) ? (float)NAN : (a > b ? a : b);
                }
            } else {
                e->rgb[o] = tmp[0];
                e->rgb[o + 1] = tmp[1];
                e->rgb[o + 2] = tmp[2];
            }
        }
    }

    e->frame++;
    finish(e);
    return e->out8;
}
