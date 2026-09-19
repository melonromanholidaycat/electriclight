#include "el_eval.h"

#include <math.h>

// Port of web/src/lang/eval.js. Read that file beside this one: every helper
// here exists to reproduce a JavaScript semantic that C does not share.
//
//   f(x)          a store into a Float32Array - round the double to float
//   x | 0         ToInt32 - truncate, then wrap modulo 2^32
//   x >>> n       ToUint32 then logical shift
//   Math.imul     a 32-bit wrapping multiply
//   a % b         fmod, not GLSL mod
//
// Everything the JavaScript computes in double is computed in double here and
// rounded at the same points, so the two agree bit for bit wherever libm does.

// --- opcodes, mirroring OP in web/src/lang/ops.js ----------------------------

enum {
    OP_CONST = 1, OP_VAR = 2, OP_PARAM = 3, OP_LOAD = 4, OP_STORE = 5,
    OP_ADD = 10, OP_SUB = 11, OP_MUL = 12, OP_DIV = 13, OP_MOD = 14, OP_NEG = 15,
    OP_LT = 20, OP_LE = 21, OP_GT = 22, OP_GE = 23, OP_EQ = 24, OP_NE = 25,
    OP_AND = 26, OP_OR = 27, OP_NOT = 28,
    OP_SELECT = 30,
    OP_CALL = 40,
    OP_END = 255,
};

// --- JavaScript number semantics ---------------------------------------------

// Number.isFinite(x) ? x : 0. Applied to every division and every built-in
// result, exactly where eval.js applies it.
static inline float finite_or_zero(double x)
{
    return isfinite(x) ? (float)x : 0.0f;
}

// ToInt32: NaN and the infinities become 0, everything else truncates toward
// zero and wraps modulo 2^32. fmod is exact for integral doubles of any size,
// so this is right even for the absurd inputs an effect can produce.
static int32_t to_int32(double x)
{
    if (!isfinite(x)) return 0;
    double t = trunc(x);
    double m = fmod(t, 4294967296.0);
    if (m < 0.0) m += 4294967296.0;
    return (int32_t)(uint32_t)m;
}

// Math.round. Not C round(): JavaScript rounds a half toward +Infinity, C
// rounds it away from zero, and the two disagree on -2.5. The early returns
// are the ECMAScript special cases, which floor(x + 0.5) gets wrong (notably
// Math.round(0.49999999999999994), where the addition itself rounds up to 1).
static double js_round(double x)
{
    if (!isfinite(x)) return x;
    if (x == floor(x)) return x;
    if (x > 0.0 && x < 0.5) return 0.0;
    if (x < 0.0 && x >= -0.5) return -0.0;
    return floor(x + 0.5);
}

// --- deterministic noise ------------------------------------------------------
//
// uint32_t throughout. JavaScript's bitwise operators work on 32-bit integers
// and Math.imul wraps, so unsigned C arithmetic reproduces them exactly - the
// signedness only ever affects how a value prints, never its bits.

static uint32_t hash32(uint32_t x)
{
    x = (x ^ 61u) ^ (x >> 16);
    x = x + (x << 3);
    x = x ^ (x >> 4);
    x = x * 0x27d4eb2du;
    x = x ^ (x >> 15);
    return x;
}

// Math.floor(f(v) * 4096 + 0.5) | 0
static int32_t quant(float v)
{
    return to_int32(floor((double)v * 4096.0 + 0.5));
}

static float hash2(float a, float b)
{
    uint32_t h = hash32((uint32_t)quant(a) ^ ((uint32_t)quant(b) * 0x9e3779b1u));
    return (float)((double)(h >> 8) / 16777216.0);
}

static float hash_int(double i)
{
    uint32_t h = hash32((uint32_t)to_int32(i));
    return (float)((double)(h >> 8) / 16777216.0);
}

static float value_noise(float x)
{
    double i = floor((double)x);
    float t = (float)((double)x - i);
    float a = hash_int(i);
    float b = hash_int(i + 1.0);
    float w = (float)((double)(float)(t * t) * (double)(float)(3.0f - (float)(2.0f * t)));
    return (float)((double)a + (double)(float)((double)w * (double)(float)(b - a)));
}

// GLSL mod: the sign follows the divisor, and a zero divisor yields 0 rather
// than NaN. Note that the floor happens in double, as it does in JavaScript -
// a float32 floor would differ once x/y exceeds 2^24.
static float glsl_mod(float x, float y)
{
    if (y == 0.0f) return 0.0f;
    float q = x / y;
    float m = (float)((double)y * floor((double)q));
    return x - m;
}

static inline float sat(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

// Monotonic remap of 0..1 onto itself, identity at amount 0.
static float warp_pos(float x, float centre, float amount)
{
    float xx = sat(x);
    float c = sat(centre);
    float k = (float)pow(2.0, -(double)amount);
    if (!(k > 0.0f) || !isfinite(k)) return xx;
    if (c <= 0.0f) return (float)pow((double)xx, (double)k);
    if (c >= 1.0f) return (float)(1.0 - pow(1.0 - (double)xx, (double)k));
    if (xx < c) {
        float inner = (float)(1.0 - (double)(float)(xx / c));
        return (float)((double)c * (1.0 - pow((double)inner, (double)k)));
    }
    float denom = (float)(1.0 - (double)c);
    float inner = (float)((double)(float)(xx - c) / (double)denom);
    return (float)((double)c + (double)denom * pow((double)inner, (double)k));
}

static float smoothstep(float e0, float e1, float x)
{
    if (e1 == e0) return x < e0 ? 0.0f : 1.0f;
    float t = sat((float)((double)(float)(x - e0) / (double)(float)(e1 - e0)));
    return (float)((double)(float)(t * t) * (double)(float)(3.0f - (float)(2.0f * t)));
}

// --- built-ins, indexed exactly as FUNCS in web/src/lang/ops.js --------------

enum {
    FN_ABS = 0, FN_MIN, FN_MAX, FN_CLAMP, FN_FLOOR, FN_CEIL, FN_ROUND,
    FN_FRACT, FN_MOD, FN_SIGN, FN_SQRT, FN_POW, FN_EXP, FN_LOG,
    FN_SIN, FN_COS, FN_TAN, FN_ATAN2, FN_STEP, FN_SMOOTHSTEP, FN_MIX,
    FN_SAT, FN_TRI, FN_GAUSS, FN_HASH, FN_NOISE, FN_WARP,
    FN_COUNT,
};

// Arity per built-in, in the same order. In JavaScript this is the function's
// own .length; here it has to be written down, so the host test checks it
// against ops.js rather than trusting this comment.
static const uint8_t FUNC_ARITY[FN_COUNT] = {
    1, 2, 2, 3, 1, 1, 1,
    1, 2, 1, 1, 2, 1, 1,
    1, 1, 1, 2, 2, 3, 3,
    1, 1, 2, 2, 1, 3,
};

// Math.min / Math.max, not fminf / fmaxf. Two differences, both deliberate:
// the JavaScript ones propagate NaN rather than suppressing it, and they treat
// -0 as smaller than +0. Neither can reach the LEDs through the output chain,
// but an evaluator that is "the same except where it cannot matter" stops being
// checkable, so both are reproduced.
static inline float js_min(float a, float b)
{
    if (isnan(a) || isnan(b)) return (float)NAN;
    if (a == b) return signbit(a) ? a : b;
    return a < b ? a : b;
}

static inline float js_max(float a, float b)
{
    if (isnan(a) || isnan(b)) return (float)NAN;
    if (a == b) return signbit(a) ? b : a;
    return a > b ? a : b;
}

static double call_builtin(uint8_t fn, float a, float b, float c)
{
    switch (fn) {
    case FN_ABS:   return fabs((double)a);
    case FN_MIN:   return js_min(a, b);
    case FN_MAX:   return js_max(a, b);
    case FN_CLAMP: return js_min(js_max(a, b), c);
    case FN_FLOOR: return floor((double)a);
    case FN_CEIL:  return ceil((double)a);
    case FN_ROUND: return floor((double)a + 0.5);
    case FN_FRACT: return (float)((double)a - floor((double)a));
    case FN_MOD:   return glsl_mod(a, b);
    case FN_SIGN:  return a > 0.0f ? 1.0 : (a < 0.0f ? -1.0 : 0.0);
    case FN_SQRT:  return a <= 0.0f ? 0.0 : (float)sqrt((double)a);
    case FN_POW:   return (float)pow((double)a, (double)b);
    case FN_EXP:   return (float)exp((double)a);
    case FN_LOG:   return a <= 0.0f ? 0.0 : (float)log((double)a);
    case FN_SIN:   return (float)sin((double)a);
    case FN_COS:   return (float)cos((double)a);
    case FN_TAN:   return (float)tan((double)a);
    case FN_ATAN2: return (float)atan2((double)a, (double)b);
    case FN_STEP:  return b < a ? 0.0 : 1.0;
    case FN_SMOOTHSTEP: return smoothstep(a, b, c);
    case FN_MIX:   return (float)((double)a + (double)(float)((double)(float)(b - a) * (double)c));
    case FN_SAT:   return sat(a);
    case FN_TRI: {
        float w = (float)((double)a - floor((double)a));
        w = (float)((double)w * 2.0);
        w = (float)((double)w - 1.0);
        return 1.0 - fabs((double)w);
    }
    case FN_GAUSS: {
        if (b == 0.0f) return 0.0;
        float num = (float)((double)a * (double)a);
        float den = (float)((double)b * (double)b);
        float q = (float)((double)num / (double)den);
        return (float)exp(-(double)q);
    }
    case FN_HASH:  return hash2(a, b);
    case FN_NOISE: return value_noise(a);
    case FN_WARP:  return warp_pos(a, b, c);
    default:       return 0.0;
    }
}

// Math.round is not the same as floor(x + 0.5) in general, but FN_ROUND has to
// stay floor(a + 0.5) because that is literally what eval.js writes for the
// `round` built-in. js_round is for the output chain, which calls Math.round.
// Referenced here so a reader does not "fix" one into the other.
double el_js_round(double x) { return js_round(x); }

// --- the machine --------------------------------------------------------------

bool el_eval(const el_program_t *program,
             const float *vars,
             const float *params,
             float *out)
{
    float stack[EL_MAX_STACK];
    float locals[EL_MAX_LOCALS];
    const uint8_t *code = program->code;
    const float *consts = program->consts;
    uint16_t len = program->code_len;
    uint16_t pc = 0;
    int sp = 0;

    for (int i = 0; i < program->locals; i++) locals[i] = 0.0f;

    for (;;) {
        if (pc >= len) return false;
        uint8_t op = code[pc++];
        switch (op) {
        case OP_CONST: stack[sp++] = consts[code[pc++]]; break;
        case OP_VAR:   stack[sp++] = vars[code[pc++]]; break;
        case OP_PARAM: stack[sp++] = params[code[pc++]]; break;
        case OP_LOAD:  stack[sp++] = locals[code[pc++]]; break;
        case OP_STORE: locals[code[pc++]] = stack[--sp]; break;

        case OP_ADD: sp--; stack[sp - 1] = stack[sp - 1] + stack[sp]; break;
        case OP_SUB: sp--; stack[sp - 1] = stack[sp - 1] - stack[sp]; break;
        case OP_MUL: sp--; stack[sp - 1] = stack[sp - 1] * stack[sp]; break;
        case OP_DIV:
            sp--;
            stack[sp - 1] = stack[sp] == 0.0f
                ? 0.0f
                : finite_or_zero((double)stack[sp - 1] / (double)stack[sp]);
            break;
        case OP_MOD: sp--; stack[sp - 1] = glsl_mod(stack[sp - 1], stack[sp]); break;
        case OP_NEG: stack[sp - 1] = -stack[sp - 1]; break;

        case OP_LT: sp--; stack[sp - 1] = stack[sp - 1] <  stack[sp] ? 1.0f : 0.0f; break;
        case OP_LE: sp--; stack[sp - 1] = stack[sp - 1] <= stack[sp] ? 1.0f : 0.0f; break;
        case OP_GT: sp--; stack[sp - 1] = stack[sp - 1] >  stack[sp] ? 1.0f : 0.0f; break;
        case OP_GE: sp--; stack[sp - 1] = stack[sp - 1] >= stack[sp] ? 1.0f : 0.0f; break;
        case OP_EQ: sp--; stack[sp - 1] = stack[sp - 1] == stack[sp] ? 1.0f : 0.0f; break;
        case OP_NE: sp--; stack[sp - 1] = stack[sp - 1] != stack[sp] ? 1.0f : 0.0f; break;
        case OP_AND:
            sp--;
            stack[sp - 1] = (stack[sp - 1] != 0.0f && stack[sp] != 0.0f) ? 1.0f : 0.0f;
            break;
        case OP_OR:
            sp--;
            stack[sp - 1] = (stack[sp - 1] != 0.0f || stack[sp] != 0.0f) ? 1.0f : 0.0f;
            break;
        case OP_NOT: stack[sp - 1] = stack[sp - 1] == 0.0f ? 1.0f : 0.0f; break;

        case OP_SELECT:
            sp -= 2;
            stack[sp - 1] = stack[sp - 1] != 0.0f ? stack[sp] : stack[sp + 1];
            break;

        case OP_CALL: {
            uint8_t fn = code[pc++];
            if (fn >= FN_COUNT) return false;
            uint8_t arity = FUNC_ARITY[fn];
            sp -= arity;
            // The JavaScript reads three slots regardless of arity and lets the
            // surplus ones be ignored. Reading only what the built-in takes is
            // the same computation without touching uninitialised stack.
            float a = stack[sp];
            float b = arity > 1 ? stack[sp + 1] : 0.0f;
            float c = arity > 2 ? stack[sp + 2] : 0.0f;
            stack[sp] = finite_or_zero(call_builtin(fn, a, b, c));
            sp++;
            break;
        }

        case OP_END:
            out[0] = locals[EL_OUT_H];
            out[1] = locals[EL_OUT_S];
            out[2] = locals[EL_OUT_V];
            return true;

        default:
            return false;
        }
    }
}

// Exposed for the host test, which checks them against web/src/lang/ops.js
// rather than letting the two tables drift.
uint8_t el_func_arity(uint8_t fn) { return fn < FN_COUNT ? FUNC_ARITY[fn] : 0; }
int el_func_count(void) { return FN_COUNT; }
