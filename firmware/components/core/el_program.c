#include "el_program.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// The wire format stores little-endian IEEE-754 float32 and so does the ESP32,
// but a memcpy says that out loud and costs nothing.
static float rd32f(const uint8_t *p)
{
    uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

// Walks the code once and proves that el_eval can run it without a single
// bounds check: every operand is in range, the stack neither overflows nor
// underflows, and the last thing that happens is END. A program that fails here
// is rejected at upload, so a bad effect can never take the guitar dark on
// stage - it is refused while the player is still looking at the phone.
static el_decode_err_t verify(const el_effect_t *e)
{
    const el_program_t *p = &e->program;
    int sp = 0;
    uint16_t pc = 0;
    bool ended = false;

    while (pc < p->code_len) {
        uint8_t op = p->code[pc++];
        int pops = 0, pushes = 0, operand = -1;

        switch (op) {
        case 1: operand = p->const_count; pushes = 1; break;  // CONST
        case 2: operand = EL_VAR_SLOTS;   pushes = 1; break;  // VAR
        case 3: operand = p->params;      pushes = 1; break;  // PARAM
        case 4: operand = p->locals;      pushes = 1; break;  // LOAD
        case 5: operand = p->locals;      pops = 1;   break;  // STORE

        case 10: case 11: case 12: case 13: case 14:          // ADD..MOD
        case 20: case 21: case 22: case 23: case 24: case 25: // LT..NE
        case 26: case 27:                                     // AND, OR
            pops = 2; pushes = 1; break;

        case 15: case 28:                                     // NEG, NOT
            pops = 1; pushes = 1; break;

        case 30: pops = 3; pushes = 1; break;                 // SELECT

        case 40: {                                            // CALL
            if (pc >= p->code_len) return EL_DECODE_BAD_CODE;
            uint8_t fn = p->code[pc++];
            if (fn >= el_func_count()) return EL_DECODE_BAD_CODE;
            pops = el_func_arity(fn);
            pushes = 1;
            break;
        }

        case 255:                                             // END
            ended = true;
            break;

        default:
            return EL_DECODE_BAD_CODE;
        }

        if (ended) break;

        if (operand >= 0) {
            if (pc >= p->code_len) return EL_DECODE_BAD_CODE;
            if (p->code[pc++] >= operand) return EL_DECODE_BAD_CODE;
        }
        sp -= pops;
        if (sp < 0) return EL_DECODE_BAD_CODE;
        sp += pushes;
        if (sp > EL_MAX_STACK) return EL_DECODE_BAD_CODE;
    }

    if (!ended) return EL_DECODE_BAD_CODE;
    // The three outputs are fixed locals, so a program without them is not one.
    if (p->locals < 3) return EL_DECODE_BAD_CODE;
    return EL_DECODE_OK;
}

el_decode_err_t el_program_decode(el_effect_t *out, const uint8_t *bytes, size_t len)
{
    if (len < 12) return EL_DECODE_SHORT;
    if (bytes[0] != 'E' || bytes[1] != 'L' || bytes[2] != 'F' || bytes[3] != 'X')
        return EL_DECODE_MAGIC;
    if (bytes[4] != EL_FORMAT_VERSION) return EL_DECODE_VERSION;

    uint8_t locals = bytes[5];
    uint8_t stack = bytes[6];
    uint8_t params = bytes[7];
    uint16_t n_consts = rd16(bytes + 8);
    uint16_t code_len = rd16(bytes + 10);

    if ((size_t)12 + (size_t)n_consts * 4 + code_len > len) return EL_DECODE_SHORT;
    if (n_consts > EL_MAX_CONSTS) return EL_DECODE_LIMITS;
    if (code_len > EL_MAX_PROGRAM_BYTES) return EL_DECODE_LIMITS;
    if (locals > EL_MAX_LOCALS) return EL_DECODE_LIMITS;
    if (params > EL_MAX_PARAMS) return EL_DECODE_LIMITS;
    // stack is the compiler's own high-water mark. verify() is what actually
    // keeps el_eval in bounds - it recomputes the depth from the code rather
    // than believing this byte - but a header that already asks for too much
    // can be turned away before anything is copied.
    if (stack > EL_MAX_STACK) return EL_DECODE_LIMITS;

    for (uint16_t i = 0; i < n_consts; i++) out->consts[i] = rd32f(bytes + 12 + i * 4);
    memcpy(out->code, bytes + 12 + (size_t)n_consts * 4, code_len);

    out->program.code = out->code;
    out->program.consts = out->consts;
    out->program.code_len = code_len;
    out->program.const_count = n_consts;
    out->program.locals = locals;
    out->program.stack = stack;
    out->program.params = params;

    return verify(out);
}

const char *el_decode_error(el_decode_err_t err)
{
    switch (err) {
    case EL_DECODE_OK:       return "ok";
    case EL_DECODE_SHORT:    return "truncated";
    case EL_DECODE_MAGIC:    return "not an effect program";
    case EL_DECODE_VERSION:  return "newer effect format than this firmware knows";
    case EL_DECODE_LIMITS:   return "program too large for this firmware";
    case EL_DECODE_BAD_CODE: return "malformed bytecode";
    }
    return "unknown";
}
