#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The bytecode evaluator. This is the twin of web/src/lang/eval.js, and the two
// must produce identical output for web/test/vectors.json.
//
// It is a literal port rather than a reimplementation. Where the JavaScript
// rounds to float32, this rounds to float; where the JavaScript computes in
// double, this computes in double. That matters most for the transcendentals:
// JavaScript's Math.sin is double precision rounded to float32, and sinf()
// would differ from it in the last bit. So the maths here is done in double and
// rounded at exactly the points the JavaScript rounds.
//
// This looks like wasted work on a device whose output is 8 bits per channel,
// and the golden vectors will not argue with you: swapping every transcendental
// here for its single-precision version passes them. It is still wrong. The
// vectors prove agreement on the frames they contain; only the literal port
// gives equivalence, and equivalence is what lets the owner trust the browser
// about an effect nobody has ever rendered on the guitar.
//
// Measurements and the full argument: docs/decisions.md, "The firmware
// evaluator is a literal port, not a reimplementation".

// Number of per-pixel input slots (the length of VARS in web/src/lang/ops.js).
#define EL_VAR_SLOTS 11

// Hard ceilings the decoder enforces, so el_eval never needs a bounds check in
// its inner loop and never allocates.
#define EL_MAX_STACK  64
#define EL_MAX_LOCALS 64

// Per-pixel inputs, in the wire order defined by web/src/lang/ops.js.
enum {
    EL_VAR_T = 0,   // seconds since the effect started, on the fixed 60 Hz clock
    EL_VAR_FRET,    // fractional fret number at this LED (0 = nut)
    EL_VAR_U,       // normalised physical distance along the lit span, 0..1
    EL_VAR_SIDE,    // 0 = bass side, 1 = treble side
    EL_VAR_N,       // LED index within its own strip, 0-based
    EL_VAR_COUNT,   // LEDs per strip
    EL_VAR_NFRETS,  // number of frets on the neck
    EL_VAR_KNOB,    // potentiometer, 0..1
    EL_VAR_SW,      // five-way switch, 0..4
    EL_VAR_PREV,    // this pixel's v output on the previous frame
    EL_VAR_RND,     // fixed per-pixel random, 0..1
};

// Output locals, fixed slots so the evaluator never looks them up.
enum { EL_OUT_H = 0, EL_OUT_S = 1, EL_OUT_V = 2 };

typedef struct {
    const uint8_t *code;
    const float *consts;
    uint16_t code_len;
    uint16_t const_count;
    uint8_t locals;
    uint8_t stack;
    uint8_t params;
} el_program_t;

// Runs one pixel. `vars` is EL_VAR_SLOTS floats, `params` is program->params
// floats (may be NULL when the program takes none), `out` receives h, s, v.
//
// Returns false only on a malformed program - an unknown opcode or function
// index, or a missing END. A program that decoded cleanly cannot fail here, so
// the firmware refuses bad bytecode once, at upload, rather than every frame.
bool el_eval(const el_program_t *program,
             const float *vars,
             const float *params,
             float *out);

// JavaScript's Math.round, which is not C round(): a half goes toward
// +Infinity, not away from zero. The output chain in el_engine.c needs it.
double el_js_round(double x);

// The built-in table, exposed so the host test can check it against
// web/src/lang/ops.js instead of trusting a comment.
uint8_t el_func_arity(uint8_t fn);
int el_func_count(void);
