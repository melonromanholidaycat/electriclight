#include "el_geometry.h"

#include <math.h>
#include <stdlib.h>

// Measured on the guitar. See docs/hardware/README.md - these values are
// checked against that document by web/test/run.js, which fails if the two
// drift apart.
const el_geometry_t EL_DEFAULT_GEOMETRY = {
    .scale_length = 648.0f,
    .frets = 21,
    .leds_per_strip = 26,
    .mapping = EL_MAP_EVEN,
    .first_fret = 0,
    .nut_to_first_led = 20.0f,
    .last_led_to_last_fret = 20.0f,
    .reversed = { true, true },
};

// Distance from the nut to fret n, in mm. Frets are geometric and LED tape is
// not, which is why `fret` and `u` are two different coordinates rather than
// one with a scale factor.
static double fret_distance(double n, double scale_length)
{
    return scale_length * (1.0 - pow(2.0, -n / 12.0));
}

// Fractional fret number at a distance from the nut. Inverse of the above.
static double fret_at(double mm, double scale_length)
{
    double r = 1.0 - mm / scale_length;
    return r <= 0.0 ? INFINITY : -12.0 * log2(r);
}

static void strip_positions(double *out, const el_geometry_t *g)
{
    double last_fret = fret_distance(g->frets, g->scale_length);
    double from = g->nut_to_first_led;
    double to = last_fret - g->last_led_to_last_fret;

    for (int i = 0; i < g->leds_per_strip; i++) {
        if (g->mapping == EL_MAP_EVEN) {
            double t = g->leds_per_strip == 1 ? 0.0 : (double)i / (g->leds_per_strip - 1);
            out[i] = from + t * (to - from);
        } else {
            // Sit each LED in the middle of a fret space, where a side dot goes.
            double a = fret_distance(g->first_fret + i, g->scale_length);
            double b = fret_distance(g->first_fret + i + 1, g->scale_length);
            out[i] = (a + b) / 2.0;
        }
    }
}

float el_led_pitch(const el_geometry_t *g)
{
    double last_fret = fret_distance(g->frets, g->scale_length);
    double from = g->nut_to_first_led;
    double to = last_fret - g->last_led_to_last_fret;
    return g->leds_per_strip > 1 ? (float)((to - from) / (g->leds_per_strip - 1)) : 0.0f;
}

// Fixed per-pixel randoms. Seeded so the simulator and the firmware agree: the
// same xorshift32 as buildRandoms() in web/src/model/geometry.js, which is why
// it is written in uint32_t rather than reached for from a library.
static void build_randoms(float *out, int count)
{
    uint32_t s = 0x2545f491u;
    for (int i = 0; i < count; i++) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        out[i] = (float)((double)(s >> 8) / 16777216.0);
    }
}

bool el_layout_build(el_layout_t *out, const el_geometry_t *g)
{
    if (g->leds_per_strip == 0 || g->leds_per_strip > EL_MAX_LEDS_PER_STRIP) return false;

    // On the heap, not the stack. At EL_MAX_LEDS_PER_STRIP this is a kilobyte,
    // and one caller is app_main, whose task stack is 3.5 kB by default. A
    // stack overflow on a board with no serial console is a bad way to find out.
    double *mm = malloc(sizeof(double) * g->leds_per_strip);
    if (!mm) return false;
    strip_positions(mm, g);

    double lo = mm[0];
    double hi = mm[g->leds_per_strip - 1];
    double span = hi - lo;
    if (span == 0.0) span = 1.0;

    int k = 0;
    for (int side = 0; side < EL_SIDES; side++) {
        for (int i = 0; i < g->leds_per_strip; i++) {
            // The strip may be soldered in either direction: n is the electrical
            // index, the position is the physical one.
            double pos = g->reversed[side] ? mm[g->leds_per_strip - 1 - i] : mm[i];
            out->pixels[k].side = (uint8_t)side;
            out->pixels[k].n = (uint8_t)i;
            // Computed in double and stored as float, exactly as the JavaScript
            // does when it writes these into its Float32Array of variables.
            out->pixels[k].u = (float)((pos - lo) / span);
            out->pixels[k].fret = (float)fret_at(pos, g->scale_length);
            k++;
        }
    }
    free(mm);
    out->count = k;
    build_randoms(out->randoms, k);
    return true;
}
