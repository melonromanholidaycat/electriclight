#pragma once
#include <stdbool.h>
#include <stdint.h>

// Neck geometry and the LED -> fretboard mapping. Twin of
// web/src/model/geometry.js; the measurements it is normally filled from live
// in docs/hardware/README.md and nowhere else.
//
// Nothing here is a constant. The whole struct is settings, because the strips
// are only roughly one LED per fret and every number in it was measured with a
// ruler on a guitar that has been apart more than once.

#define EL_MAX_LEDS_PER_STRIP 128
#define EL_SIDES 2
#define EL_MAX_PIXELS (EL_MAX_LEDS_PER_STRIP * EL_SIDES)

typedef enum {
    EL_MAP_EVEN = 0,      // a commercial tape: fixed pitch in millimetres
    EL_MAP_FRET_MIDPOINT, // a strip cut and re-spaced by hand, one LED per fret space
} el_mapping_t;

typedef struct {
    float scale_length;        // mm, nut to bridge
    uint8_t frets;
    uint8_t leds_per_strip;
    el_mapping_t mapping;
    uint8_t first_fret;        // fret space holding LED 0 (EL_MAP_FRET_MIDPOINT only)
    float nut_to_first_led;    // mm
    float last_led_to_last_fret; // mm
    // Both strips are fed from the body end, where the controller lives, so the
    // electrical index runs body -> nut: LED 0 sits at the highest fret.
    bool reversed[EL_SIDES];
} el_geometry_t;

// The per-pixel constants the evaluator reads. Built once per geometry change.
typedef struct {
    float fret; // fractional fret number, 0 = nut
    float u;    // normalised physical position along the lit span, 0..1
    uint8_t side;
    uint8_t n;  // electrical index within its own strip
} el_pixel_t;

typedef struct {
    el_pixel_t pixels[EL_MAX_PIXELS];
    float randoms[EL_MAX_PIXELS];
    int count;
} el_layout_t;

extern const el_geometry_t EL_DEFAULT_GEOMETRY;

// Returns false if the geometry asks for more LEDs than this build can hold.
bool el_layout_build(el_layout_t *out, const el_geometry_t *g);

// Centre-to-centre LED pitch in mm. A commercial tape has a fixed pitch, so
// this is a useful sanity check: it should land near a standard density
// (60/m is 16.67 mm, 30/m is 33.3 mm). If it does not, a measurement is off.
float el_led_pitch(const el_geometry_t *g);
