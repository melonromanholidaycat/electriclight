// Opcode and built-in tables. THIS FILE IS THE CONTRACT between the JavaScript
// evaluator (simulator) and the C++ evaluator (firmware). Indices are wire
// format: never renumber, only append. Bump FORMAT_VERSION if you do.

export const FORMAT_VERSION = 1;

export const OP = {
  CONST: 1,   // u8 index into constant pool
  VAR: 2,     // u8 index into VARS
  PARAM: 3,   // u8 index into the effect's parameter list
  LOAD: 4,    // u8 local slot
  STORE: 5,   // u8 local slot (pops)

  ADD: 10, SUB: 11, MUL: 12, DIV: 13, MOD: 14, NEG: 15,

  LT: 20, LE: 21, GT: 22, GE: 23, EQ: 24, NE: 25,
  AND: 26, OR: 27, NOT: 28,

  SELECT: 30, // cond a b -> a if cond != 0 else b (both already evaluated)

  CALL: 40,   // u8 index into FUNCS

  END: 255,
};

// Per-pixel inputs, in wire order.
export const VARS = [
  't',      // 0  seconds since the effect started, on the fixed 60 Hz clock
  'fret',   // 1  fractional fret number at this LED (0 = nut)
  'u',      // 2  normalised physical distance along the lit span, 0..1
  'side',   // 3  0 = bass side, 1 = treble side
  'n',      // 4  LED index within its own strip, 0-based
  'count',  // 5  LEDs per strip
  'nfrets', // 6  number of frets on the neck
  'knob',   // 7  potentiometer, 0..1
  'sw',     // 8  five-way switch, 0..4
  'prev',   // 9  this pixel's v output on the previous frame
  'rnd',    // 10 fixed per-pixel random, 0..1, stable for the life of the frame loop
];

export const VAR_INDEX = Object.fromEntries(VARS.map((v, i) => [v, i]));

// Built-in functions, in wire order. arity is fixed per function.
export const FUNCS = [
  { name: 'abs', arity: 1 },
  { name: 'min', arity: 2 },
  { name: 'max', arity: 2 },
  { name: 'clamp', arity: 3 },
  { name: 'floor', arity: 1 },
  { name: 'ceil', arity: 1 },
  { name: 'round', arity: 1 },
  { name: 'fract', arity: 1 },
  { name: 'mod', arity: 2 },
  { name: 'sign', arity: 1 },
  { name: 'sqrt', arity: 1 },
  { name: 'pow', arity: 2 },
  { name: 'exp', arity: 1 },
  { name: 'log', arity: 1 },
  { name: 'sin', arity: 1 },
  { name: 'cos', arity: 1 },
  { name: 'tan', arity: 1 },
  { name: 'atan2', arity: 2 },
  { name: 'step', arity: 2 },
  { name: 'smoothstep', arity: 3 },
  { name: 'mix', arity: 3 },
  { name: 'sat', arity: 1 },
  { name: 'tri', arity: 1 },
  { name: 'gauss', arity: 2 },
  { name: 'hash', arity: 2 },
  { name: 'noise', arity: 1 },
];

export const FUNC_INDEX = Object.fromEntries(FUNCS.map((f, i) => [f.name, i]));

export const CONSTANTS = { PI: Math.PI, TAU: Math.PI * 2, E: Math.E };

// Output slots are fixed locals so the evaluator never has to look them up.
export const OUT = { h: 0, s: 1, v: 2 };
export const OUT_NAMES = ['h', 's', 'v'];
