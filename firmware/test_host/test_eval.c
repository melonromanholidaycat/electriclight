// Host test for the effect evaluator: the same golden vectors the simulator is
// held to, run through the C that will run on the guitar.
//
// The interesting checks are in el_selftest.c, because that code also ships to
// the device. What is here around it is the part that only makes sense on a
// host: the decoder's refusals, and the tables that have to match ops.js.

#include <stdio.h>
#include <string.h>

#include "el_engine.h"
#include "el_eval.h"
#include "el_program.h"
#include "el_selftest.h"

static int failures = 0;
static int passed = 0;

static void check(bool cond, const char *what)
{
    if (cond) { passed++; return; }
    failures++;
    printf("  x %s\n", what);
}

// Where the code starts in a minimal program: a 12-byte header plus one f32
// constant. The decoder tests corrupt bytes by offset, and getting this wrong
// once already made a test pass for the wrong reason.
#define HEADER_BYTES 12
#define CODE_AT (HEADER_BYTES + 4)

// A valid, minimal program: CONST 0 -> STORE h, CONST 0 -> STORE s,
// CONST 0 -> STORE v, END. Built by hand so the decoder tests can corrupt it.
static int build_minimal(uint8_t *buf)
{
    static const uint8_t code[] = {
        1, 0, 5, 0,
        1, 0, 5, 1,
        1, 0, 5, 2,
        255,
    };
    int o = 0;
    buf[o++] = 'E'; buf[o++] = 'L'; buf[o++] = 'F'; buf[o++] = 'X';
    buf[o++] = 1;               // version
    buf[o++] = 3;               // locals
    buf[o++] = 1;               // stack
    buf[o++] = 0;               // params
    buf[o++] = 1; buf[o++] = 0; // consts
    buf[o++] = sizeof code; buf[o++] = 0;
    buf[o++] = 0; buf[o++] = 0; buf[o++] = 0; buf[o++] = 0; // const 0 = 0.0f
    memcpy(buf + o, code, sizeof code);
    return o + (int)sizeof code;
}

int main(void)
{
    printf("effect evaluator\n");

    // --- the tables that must match web/src/lang/ops.js ----------------------
    // FUNC_ARITY in el_eval.c is written out by hand, because C has no
    // equivalent of reading a function's own .length the way eval.js does. That
    // makes it the one table here that can drift silently, so it is checked
    // against the generated copy of ops.js rather than against a comment.
    const el_vectors_contract_t *ops = el_vectors_contract();
    check(el_func_count() == ops->func_count, "built-in count matches ops.js");
    check(EL_VAR_SLOTS == ops->var_count, "variable count matches ops.js");
    for (int i = 0; i < ops->func_count && i < el_func_count(); i++) {
        if (el_func_arity((uint8_t)i) == ops->func_arity[i]) { passed++; continue; }
        failures++;
        printf("  x built-in %d arity: ops.js says %d, el_eval.c says %d\n",
               i, ops->func_arity[i], el_func_arity((uint8_t)i));
    }
    check(EL_FORMAT_VERSION == ops->format_version,
          "wire format version matches vectors.json");
    check(EL_FRAME_RATE == ops->frame_rate,
          "frame rate matches vectors.json");

    // --- the decoder refuses what it should ----------------------------------
    uint8_t buf[256];
    int len = build_minimal(buf);
    el_effect_t effect;

    check(el_program_decode(&effect, buf, (size_t)len) == EL_DECODE_OK,
          "a well-formed program decodes");

    uint8_t bad[256];
    memcpy(bad, buf, (size_t)len);
    bad[0] = 'X';
    check(el_program_decode(&effect, bad, (size_t)len) == EL_DECODE_MAGIC,
          "a blob that is not a program is refused");

    memcpy(bad, buf, (size_t)len);
    bad[4] = EL_FORMAT_VERSION + 1;
    check(el_program_decode(&effect, bad, (size_t)len) == EL_DECODE_VERSION,
          "a newer format version is refused, not guessed at");

    check(el_program_decode(&effect, buf, 8) == EL_DECODE_SHORT,
          "a truncated header is refused");
    check(el_program_decode(&effect, buf, (size_t)len - 1) == EL_DECODE_SHORT,
          "a truncated body is refused");

    memcpy(bad, buf, (size_t)len);
    bad[len - 1] = 99; // an opcode that does not exist, in place of END
    check(el_program_decode(&effect, bad, (size_t)len) == EL_DECODE_BAD_CODE,
          "an unknown opcode is refused");

    memcpy(bad, buf, (size_t)len);
    bad[len - 1] = 10; // ADD, with an empty stack, and no END
    check(el_program_decode(&effect, bad, (size_t)len) == EL_DECODE_BAD_CODE,
          "a program that underflows the stack is refused");

    memcpy(bad, buf, (size_t)len);
    bad[CODE_AT + 3] = 7; // STORE into local 7, of which there are 3
    check(el_program_decode(&effect, bad, (size_t)len) == EL_DECODE_BAD_CODE,
          "an out-of-range local is refused");

    memcpy(bad, buf, (size_t)len);
    bad[5] = 2; // two locals, but h, s and v are three
    check(el_program_decode(&effect, bad, (size_t)len) == EL_DECODE_BAD_CODE,
          "a program with no room for h, s and v is refused");

    // Refusal has to be total: nothing about a rejected program may end up
    // running. The guitar decodes an uploaded effect once, and this is where
    // that decision is made.
    memcpy(bad, buf, (size_t)len);
    bad[len - 1] = 99;
    memset(&effect, 0, sizeof effect);
    el_program_decode(&effect, bad, (size_t)len);
    float vars[EL_VAR_SLOTS] = { 0 };
    float out[3];
    check(!el_eval(&effect.program, vars, NULL, out),
          "the evaluator also refuses the bytecode the decoder rejected");

    // --- the golden vectors ---------------------------------------------------
    el_selftest_result_t r;
    bool ok = el_selftest(&r, NULL, NULL);
    char line[160];
    el_selftest_describe(&r, line, sizeof line);
    if (ok) {
        passed++;
        printf("  golden vectors: %s\n", line);
    } else {
        failures++;
        printf("  x golden vectors: %s\n", line);
    }

    printf("\n%d passed, %d failed\n", passed, failures);
    return failures ? 1 : 0;
}
