// Bytecode evaluator. This file has a twin in the firmware (C++): the two must
// produce identical output for the golden vectors in web/test/vectors.json.
//
// Rules that keep the two in step:
//   * every value is float32 (hence the Float32Array scratch buffers)
//   * division or modulo by zero yields 0, never Infinity or NaN
//   * any non-finite result of a division or built-in call collapses to 0
//   * mod() is GLSL-style (sign follows the divisor), not C fmod()

import { OP, FUNC_INDEX, VARS } from './ops.js';

const F32 = new Float32Array(1);
const f = (x) => { F32[0] = x; return F32[0]; };
const finite = (x) => (Number.isFinite(x) ? x : 0);

// --- deterministic noise -----------------------------------------------------

function hash32(x) {
  x = (x ^ 61) ^ (x >>> 16);
  x = (x + (x << 3)) | 0;
  x = x ^ (x >>> 4);
  x = Math.imul(x, 0x27d4eb2d);
  x = x ^ (x >>> 15);
  return x >>> 0;
}

const quant = (v) => Math.floor(f(v) * 4096 + 0.5) | 0;

function hash2(a, b) {
  const h = hash32((quant(a) ^ Math.imul(quant(b), 0x9e3779b1)) | 0);
  return f((h >>> 8) / 16777216);
}

function hashInt(i) {
  const h = hash32(i | 0);
  return f((h >>> 8) / 16777216);
}

function valueNoise(x) {
  const xf = f(x);
  const i = Math.floor(xf);
  const t = f(xf - i);
  const a = hashInt(i);
  const b = hashInt(i + 1);
  const w = f(f(t * t) * f(3 - f(2 * t)));
  return f(a + f(w * f(b - a)));
}

const glslMod = (x, y) => (y === 0 ? 0 : f(x - f(y * Math.floor(f(x / y)))));
const sat = (x) => (x < 0 ? 0 : x > 1 ? 1 : x);

function smoothstep(e0, e1, x) {
  if (e1 === e0) return x < e0 ? 0 : 1;
  const t = sat(f(f(x - e0) / f(e1 - e0)));
  return f(f(t * t) * f(3 - f(2 * t)));
}

// --- built-ins, indexed exactly as FUNCS in ops.js ---------------------------

const CALLS = [
  (a) => Math.abs(a),
  (a, b) => Math.min(a, b),
  (a, b) => Math.max(a, b),
  (a, b, c) => Math.min(Math.max(a, b), c),
  (a) => Math.floor(a),
  (a) => Math.ceil(a),
  (a) => Math.floor(a + 0.5),
  (a) => f(a - Math.floor(a)),
  (a, b) => glslMod(a, b),
  (a) => (a > 0 ? 1 : a < 0 ? -1 : 0),
  (a) => (a <= 0 ? 0 : f(Math.sqrt(a))),
  (a, b) => f(Math.pow(a, b)),
  (a) => f(Math.exp(a)),
  (a) => (a <= 0 ? 0 : f(Math.log(a))),
  (a) => f(Math.sin(a)),
  (a) => f(Math.cos(a)),
  (a) => f(Math.tan(a)),
  (a, b) => f(Math.atan2(a, b)),
  (a, b) => (b < a ? 0 : 1),
  (a, b, c) => smoothstep(a, b, c),
  (a, b, c) => f(a + f(f(b - a) * c)),
  (a) => sat(a),
  (a) => f(1 - Math.abs(f(f(f(a - Math.floor(a)) * 2) - 1))),
  (a, b) => (b === 0 ? 0 : f(Math.exp(f(-f(f(a * a) / f(b * b)))))),
  (a, b) => hash2(a, b),
  (a) => valueNoise(a),
];

// --- the machine -------------------------------------------------------------

export function createRunner(program) {
  const code = program.code;
  const consts = program.consts;
  const stack = new Float32Array(program.stack + 8);
  const locals = new Float32Array(program.nLocals);

  return function run(vars, params, out) {
    let sp = 0;
    let pc = 0;
    locals.fill(0);

    for (;;) {
      const op = code[pc++];
      switch (op) {
        case OP.CONST: stack[sp++] = consts[code[pc++]]; break;
        case OP.VAR: stack[sp++] = vars[code[pc++]]; break;
        case OP.PARAM: stack[sp++] = params[code[pc++]]; break;
        case OP.LOAD: stack[sp++] = locals[code[pc++]]; break;
        case OP.STORE: locals[code[pc++]] = stack[--sp]; break;

        case OP.ADD: sp--; stack[sp - 1] = stack[sp - 1] + stack[sp]; break;
        case OP.SUB: sp--; stack[sp - 1] = stack[sp - 1] - stack[sp]; break;
        case OP.MUL: sp--; stack[sp - 1] = stack[sp - 1] * stack[sp]; break;
        case OP.DIV: sp--; stack[sp - 1] = stack[sp] === 0 ? 0 : finite(stack[sp - 1] / stack[sp]); break;
        case OP.MOD: sp--; stack[sp - 1] = glslMod(stack[sp - 1], stack[sp]); break;
        case OP.NEG: stack[sp - 1] = -stack[sp - 1]; break;

        case OP.LT: sp--; stack[sp - 1] = stack[sp - 1] < stack[sp] ? 1 : 0; break;
        case OP.LE: sp--; stack[sp - 1] = stack[sp - 1] <= stack[sp] ? 1 : 0; break;
        case OP.GT: sp--; stack[sp - 1] = stack[sp - 1] > stack[sp] ? 1 : 0; break;
        case OP.GE: sp--; stack[sp - 1] = stack[sp - 1] >= stack[sp] ? 1 : 0; break;
        case OP.EQ: sp--; stack[sp - 1] = stack[sp - 1] === stack[sp] ? 1 : 0; break;
        case OP.NE: sp--; stack[sp - 1] = stack[sp - 1] !== stack[sp] ? 1 : 0; break;
        case OP.AND: sp--; stack[sp - 1] = stack[sp - 1] !== 0 && stack[sp] !== 0 ? 1 : 0; break;
        case OP.OR: sp--; stack[sp - 1] = stack[sp - 1] !== 0 || stack[sp] !== 0 ? 1 : 0; break;
        case OP.NOT: stack[sp - 1] = stack[sp - 1] === 0 ? 1 : 0; break;

        case OP.SELECT:
          sp -= 2;
          stack[sp - 1] = stack[sp - 1] !== 0 ? stack[sp] : stack[sp + 1];
          break;

        case OP.CALL: {
          const fn = code[pc++];
          const arity = CALLS[fn].length;
          sp -= arity;
          stack[sp] = finite(CALLS[fn](stack[sp], stack[sp + 1], stack[sp + 2]));
          sp++;
          break;
        }

        case OP.END:
          out[0] = locals[0];
          out[1] = locals[1];
          out[2] = locals[2];
          return;

        default:
          throw new Error(`bad opcode ${op} at ${pc - 1}`);
      }
    }
  };
}

export const VAR_COUNT = VARS.length;
export const _internals = { hash2, hashInt, valueNoise, glslMod, FUNC_INDEX };
