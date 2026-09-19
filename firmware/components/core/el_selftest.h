#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Runs the golden vectors from web/test/vectors.json against this build's
// evaluator, and reports the first byte that disagrees.
//
// This is not really here to catch a last-bit disagreement between the host's
// libm and newlib's: measurement says two roundings absorb differences far
// larger than that (see el_eval.h). It is here for the failures CI genuinely
// cannot see - a miscompile at a different optimisation level, a half-written
// OTA, flash that has started to rot, a build where the generated vectors and
// the evaluator came from different commits.
//
// The guitar has no serial console. Without this, the only way to ask whether
// the firmware still renders what the browser drew is to notice that a gig
// looked wrong.

typedef struct {
    int cases_total;
    int cases_passed;
    int frames_checked;
    // Everything below describes the first disagreement only; case_index is -1
    // when there was none.
    int case_index;
    const char *case_id;
    int frame;      // the effect frame number, not the index into the vector
    int byte_index; // into the frame, side 0 first, three bytes per LED
    int expected;
    int got;
    const char *error; // set when a program would not even decode
} el_selftest_result_t;

// Called periodically during a run so the caller can let something else happen.
// This exists because the run is long: twenty seconds on an ESP32-S3, measured,
// because the evaluator computes in double on a chip whose FPU is single
// precision only. Twenty seconds of one core without yielding starves the idle
// task and trips the task watchdog.
typedef void (*el_selftest_yield_fn)(void *ctx);

// Returns true if every case matched. Needs about 12 kB of heap for the
// duration of the call and releases it before returning. `yield` may be NULL,
// which is right for a host test and wrong for anything running an RTOS.
bool el_selftest(el_selftest_result_t *result, el_selftest_yield_fn yield, void *ctx);

// What the generated vectors record about web/src/lang/ops.js. Exposed so a
// test can compare it with what el_eval.c believes, without pulling the whole
// vector table into a second translation unit.
typedef struct {
    int format_version;
    int frame_rate;
    int func_count;
    int var_count;
    const uint8_t *func_arity; // func_count entries
    int case_count;
    int byte_count;            // bytes in one frame
} el_vectors_contract_t;

const el_vectors_contract_t *el_vectors_contract(void);

// One golden program, in each of the two forms it exists in: the bytes, and the
// base64 the browser sends. Exposed so a test can check that the firmware's
// decoder inverts the browser's encoder.
const uint8_t *el_vectors_program(int index, size_t *len_out);
const char *el_vectors_program_b64(int index);

// Formats a result into a single line for /api/status and the boot log.
// Always NUL-terminates. Returns the number of characters written.
int el_selftest_describe(const el_selftest_result_t *r, char *out, int max);
