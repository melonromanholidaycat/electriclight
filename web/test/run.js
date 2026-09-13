import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { compile, CompileError } from '../src/lang/compile.js';
import { createRunner } from '../src/lang/eval.js';
import { encodeProgram, decodeProgram, toBase64, fromBase64 } from '../src/lang/serialize.js';
import { VARS, FORMAT_VERSION } from '../src/lang/ops.js';
import { Engine } from '../src/model/engine.js';
import { fretDistance, fretAt, buildPixels, DEFAULT_GEOMETRY } from '../src/model/geometry.js';
import { defaultLibrary, buildLayers, presetsByDefinition, resolveValues, programFor } from '../src/model/library.js';
import { buildVectors, GEOMETRY, OUTPUT } from './vectors.js';

const here = dirname(fileURLToPath(import.meta.url));

let passed = 0;
const failures = [];

function test(name, fn) {
  try { fn(); passed++; }
  catch (err) { failures.push(`${name}: ${err.message}`); }
}
function assert(cond, msg) { if (!cond) throw new Error(msg || 'assertion failed'); }
function near(a, b, tol, msg) {
  if (!(Math.abs(a - b) <= tol)) throw new Error(`${msg || ''} expected ${b}, got ${a}`);
}
function throws(fn, match) {
  try { fn(); } catch (err) {
    assert(err instanceof CompileError, `wrong error type: ${err}`);
    assert(!match || err.message.includes(match), `expected "${match}" in "${err.message}"`);
    return;
  }
  throw new Error('expected a compile error');
}

// --- evaluating a one-off expression ----------------------------------------

function evalSource(source, vars = {}, values = {}) {
  const program = compile(source);
  const run = createRunner(program);
  const v = new Float32Array(VARS.length);
  for (const [k, val] of Object.entries(vars)) v[VARS.indexOf(k)] = val;
  const params = resolveValues(program, values);
  const out = new Float32Array(3);
  run(v, params, out);
  return { h: out[0], s: out[1], v: out[2], program };
}

// --- language ----------------------------------------------------------------

test('arithmetic and precedence', () => {
  near(evalSource('v = 1 + 2 * 3').v, 7, 0);
  near(evalSource('v = (1 + 2) * 3').v, 9, 0);
  near(evalSource('v = 10 - 3 - 2').v, 5, 0);
  near(evalSource('v = 2 * 3 % 4').v, 2, 0);
  near(evalSource('v = -2 + 5').v, 3, 0);
});

test('comparisons and logic yield 0 or 1', () => {
  near(evalSource('v = 2 > 1').v, 1, 0);
  near(evalSource('v = 2 < 1').v, 0, 0);
  near(evalSource('v = 1 && 0').v, 0, 0);
  near(evalSource('v = 1 || 0').v, 1, 0);
  near(evalSource('v = !0').v, 1, 0);
  near(evalSource('v = 1 < 2 && 3 >= 3').v, 1, 0);
});

test('ternary picks a branch', () => {
  near(evalSource('v = 1 ? 0.25 : 0.75').v, 0.25, 1e-6);
  near(evalSource('v = 0 ? 0.25 : 0.75').v, 0.75, 1e-6);
  near(evalSource('v = side < 0.5 ? 0.1 : 0.9', { side: 1 }).v, 0.9, 1e-6);
});

test('division by zero is zero, not infinity', () => {
  near(evalSource('v = 1 / 0').v, 0, 0);
  near(evalSource('v = 1 / (u - u)', { u: 0.3 }).v, 0, 0);
  assert(Number.isFinite(evalSource('v = 0 / 0').v), 'NaN escaped');
});

test('mod follows the divisor sign, like GLSL', () => {
  near(evalSource('v = mod(-1, 3)').v, 2, 1e-6);
  near(evalSource('v = mod(7, 3)').v, 1, 1e-6);
  near(evalSource('v = mod(1, 0)').v, 0, 0);
});

test('guarded built-ins never emit NaN', () => {
  for (const src of ['v = sqrt(-4)', 'v = log(0)', 'v = log(-1)', 'v = pow(-2, 0.5)', 'v = gauss(1, 0)']) {
    assert(Number.isFinite(evalSource(src).v), `${src} produced a non-finite value`);
  }
});

test('built-ins behave', () => {
  near(evalSource('v = clamp(5, 0, 1)').v, 1, 0);
  near(evalSource('v = fract(2.25)').v, 0.25, 1e-6);
  near(evalSource('v = step(0.5, 0.7)').v, 1, 0);
  near(evalSource('v = step(0.5, 0.3)').v, 0, 0);
  near(evalSource('v = smoothstep(0, 1, 0.5)').v, 0.5, 1e-6);
  near(evalSource('v = mix(0, 10, 0.25)').v, 2.5, 1e-6);
  near(evalSource('v = tri(0)').v, 0, 1e-6);
  near(evalSource('v = tri(0.5)').v, 1, 1e-6);
  near(evalSource('v = sign(-3)').v, -1, 0);
  near(evalSource('v = gauss(0, 0.5)').v, 1, 1e-6);
});

test('hash and noise are deterministic and bounded', () => {
  const a = evalSource('v = hash(3, 7)').v;
  const b = evalSource('v = hash(3, 7)').v;
  assert(a === b, 'hash is not stable');
  assert(a >= 0 && a < 1, `hash out of range: ${a}`);
  const seen = new Set();
  for (let i = 0; i < 64; i++) seen.add(evalSource(`v = hash(${i}, 1)`).v);
  assert(seen.size > 55, `hash is clustering: ${seen.size} distinct of 64`);
  for (let i = 0; i < 32; i++) {
    const n = evalSource(`v = noise(${i / 3})`).v;
    assert(n >= 0 && n <= 1, `noise out of range: ${n}`);
  }
});

test('params clamp to their declared range', () => {
  const src = 'param a 0..1 = 0.5 "A"\nv = a\n';
  near(evalSource(src, {}, { a: 5 }).v, 1, 0);
  near(evalSource(src, {}, { a: -5 }).v, 0, 0);
  near(evalSource(src, {}, {}).v, 0.5, 0);
});

test('param declarations parse, including step and labels', () => {
  const p = compile('param speed 0.1..5 = 1.25 "Speed" step:0.05\nv = speed / 5\n').params[0];
  assert(p.name === 'speed' && p.label === 'Speed', 'name or label wrong');
  near(p.min, 0.1, 0); near(p.max, 5, 0); near(p.def, 1.25, 0); near(p.step, 0.05, 0);
});

test('outputs default so a partial effect still runs', () => {
  const r = evalSource('v = 0.5');
  near(r.h, 0, 0); near(r.s, 1, 0); near(r.v, 0.5, 0);
});

test('locals can be reassigned', () => {
  near(evalSource('d = 3\nd = d * 2\nv = d').v, 6, 0);
});

test('a statement may span lines while brackets are open', () => {
  near(evalSource('v = max(\n  0.25,\n  0.75\n)').v, 0.75, 0);
});

test('comments are ignored', () => {
  near(evalSource('# leading\nv = 0.5 # trailing\n').v, 0.5, 0);
});

test('bad source is rejected with a line number', () => {
  throws(() => compile('v = nope'), 'unknown name');
  throws(() => compile('v = sin(1, 2)'), 'takes 1 argument');
  throws(() => compile('v = nosuchfn(1)'), 'unknown function');
  throws(() => compile('v = 1 +'), 'unexpected end');
  throws(() => compile('v = (1 + 2'), 'unbalanced');
  throws(() => compile('param 0..1 = 2\nv = 1'), 'bad param');
  throws(() => compile('param a 0..1 = 2 "A"\nv = a'), 'outside range');
  throws(() => compile('param a 1..0 = 0.5 "A"\nv = a'), 'max must exceed min');
  throws(() => compile('sin = 3\nv = sin'), 'built-in name');
  throws(() => compile('param sin 0..1 = 0.5 "S"\nv = sin'), 'built-in name');
  throws(() => compile('v = later\nlater = 1'), "unknown name 'later'");
  throws(() => compile('v = 1 $ 2'), 'unexpected character');
  const err = (() => { try { compile('v = 1\nv = nope\n'); } catch (e) { return e; } })();
  assert(err.line === 2, `expected line 2, got ${err.line}`);
});

// --- wire format -------------------------------------------------------------

test('programs survive encode, base64 and decode', () => {
  const program = compile('param a 0..2 = 1 "A"\nv = sin(t * a) * 0.5 + 0.5\nh = 120\n');
  const round = decodeProgram(fromBase64(toBase64(encodeProgram(program))), program.params);
  assert(round.version === FORMAT_VERSION, 'version lost');
  assert(round.nLocals === program.nLocals, 'locals lost');
  assert(round.stack === program.stack, 'stack lost');
  assert(round.code.length === program.code.length, 'code length changed');
  for (let i = 0; i < program.code.length; i++) assert(round.code[i] === program.code[i], `code byte ${i}`);
  for (let i = 0; i < program.consts.length; i++) {
    assert(round.consts[i] === program.consts[i], `const ${i}`);
  }
  const out = new Float32Array(3);
  createRunner(round)(new Float32Array(VARS.length), Float32Array.from([1]), out);
  near(out[2], 0.5, 1e-6);
});

test('base64 round-trips every byte length', () => {
  for (let n = 0; n < 40; n++) {
    const bytes = Uint8Array.from({ length: n }, (_, i) => (i * 37 + 11) & 255);
    const back = fromBase64(toBase64(bytes));
    assert(back.length === n, `length ${back.length} != ${n}`);
    for (let i = 0; i < n; i++) assert(back[i] === bytes[i], `byte ${i} of ${n}`);
  }
});

// --- geometry ----------------------------------------------------------------

test('fret spacing follows the real geometry', () => {
  const L = 648;
  near(fretDistance(0, L), 0, 0);
  near(fretDistance(12, L), L / 2, 1e-9);
  near(fretDistance(1, L), 36.36, 0.02);
  near(fretAt(fretDistance(7, L), L), 7, 1e-9);
  assert(fretDistance(2, L) - fretDistance(1, L) > fretDistance(13, L) - fretDistance(12, L),
    'spacing should narrow toward the body');
});

test('pixels carry both fret and physical position', () => {
  const px = buildPixels({ ...DEFAULT_GEOMETRY, ledsPerStrip: 22 });
  assert(px.length === 44, `expected 44 pixels, got ${px.length}`);
  near(px[0].u, 0, 1e-9);
  near(px[21].u, 1, 1e-9);
  assert(px[0].side === 0 && px[22].side === 1, 'sides are not grouped as expected');
  assert(px[0].fret < px[21].fret, 'fret should increase toward the body');
});

test('a reversed strip keeps its electrical index but moves physically', () => {
  const g = { ...DEFAULT_GEOMETRY, ledsPerStrip: 22, reversed: [false, true] };
  const px = buildPixels(g);
  near(px[22].u, 1, 1e-9);
  assert(px[22].n === 0, 'electrical index should be unchanged');
});

// --- output chain ------------------------------------------------------------

test('the brightness ceiling is out of an effect reach', () => {
  const program = compile('v = 1\ns = 0\n');
  const mk = (ceiling) => {
    const e = new Engine({ ...GEOMETRY }, { ...OUTPUT, brightnessCeiling: ceiling });
    e.setLayers([{ program, params: new Float32Array(0), mask: null, blend: 'normal' }]);
    e.knob = 1;
    e.knobTarget = null;
    return e.step()[0];
  };
  assert(mk(1) === 255, `full ceiling should reach 255, got ${mk(1)}`);
  assert(mk(0.5) < 160 && mk(0.5) > 100, `half ceiling looks wrong: ${mk(0.5)}`);
  assert(mk(0) === 0, 'zero ceiling should be dark');
});

test('the current limiter holds the budget', () => {
  const program = compile('v = 1\ns = 0\n');
  const e = new Engine({ ...GEOMETRY }, { ...OUTPUT, brightnessCeiling: 1, currentBudget: 300 });
  e.setLayers([{ program, params: new Float32Array(0), mask: null, blend: 'normal' }]);
  e.knob = 1;
  e.knobTarget = null;
  e.step();
  assert(e.limited, 'should have limited');
  assert(e.current <= 300 * 1.02, `over budget: ${e.current} mA`);
});

test('masks keep a layer off the pixels it does not own', () => {
  const program = compile('v = 1\ns = 0\n');
  const e = new Engine({ ...GEOMETRY }, { ...OUTPUT, brightnessCeiling: 1 });
  e.setLayers([{
    program, params: new Float32Array(0), blend: 'normal',
    mask: { fromFret: -1, toFret: 999, sides: [true, false] },
  }]);
  e.knob = 1; e.knobTarget = null;
  const out = e.step();
  const perStrip = GEOMETRY.ledsPerStrip * 3;
  assert(out[0] === 255, 'bass side should be lit');
  assert(out[perStrip] === 0, 'treble side should be dark');
});

test('prev feeds back exactly one frame', () => {
  const program = compile('v = t < 0.005 ? 1 : prev * 0.5\ns = 0\n');
  const e = new Engine({ ...GEOMETRY }, { ...OUTPUT, brightnessCeiling: 1, gamma: 1 });
  e.setLayers([{ program, params: new Float32Array(0), mask: null, blend: 'normal' }]);
  e.knob = 1; e.knobTarget = null;
  near(e.step()[0], 255, 0);
  near(e.step()[0], 128, 1);
  near(e.step()[0], 64, 1);
});

// --- library -----------------------------------------------------------------

test('every built-in effect compiles', () => {
  for (const def of defaultLibrary().definitions) {
    const { program, error } = programFor(def);
    assert(program, `${def.id}: ${error}`);
    assert(program.params.length > 0, `${def.id} exposes no parameters`);
  }
});

test('the default library fills all five switch slots', () => {
  const lib = defaultLibrary();
  assert(lib.slots.length === 5, 'wrong slot count');
  for (const id of lib.slots) assert(lib.presets.some((p) => p.id === id), `slot ${id} dangles`);
});

test('presets group under their definition', () => {
  const lib = defaultLibrary();
  const { groups, orphans } = presetsByDefinition(lib);
  assert(orphans.length === 0, 'nothing should be orphaned in v1');
  assert(groups.every((g) => g.def), 'group without a definition');
  const total = groups.reduce((n, g) => n + g.presets.length, 0);
  assert(total === lib.presets.length, 'presets lost during grouping');
});

test('a preset survives its definition losing and gaining params', () => {
  const lib = defaultLibrary();
  const def = lib.definitions.find((d) => d.id === 'comet');
  const preset = lib.presets.find((p) => p.layers[0].defId === 'comet');
  preset.layers[0].values = { speed: 2, vanished: 99 };
  def.source = 'param speed 0..4 = 1 "Speed"\nparam added 0..1 = 0.25 "Added"\nv = speed / 4 + added\n';
  const { layers, errors } = buildLayers(lib, preset);
  assert(!errors.length, errors.join());
  near(layers[0].params[0], 2, 0);
  near(layers[0].params[1], 0.25, 0);
});

// --- golden vectors ----------------------------------------------------------

test('golden vectors still match', () => {
  const stored = JSON.parse(readFileSync(join(here, 'vectors.json'), 'utf8'));
  const fresh = buildVectors();
  assert(stored.formatVersion === fresh.formatVersion, 'format version moved');
  assert(stored.cases.length === fresh.cases.length,
    `case count changed (${stored.cases.length} stored, ${fresh.cases.length} now)`);
  for (let i = 0; i < fresh.cases.length; i++) {
    const a = stored.cases[i];
    const b = fresh.cases[i];
    assert(a.id === b.id, `case ${i} is now "${b.id}", was "${a.id}"`);
    assert(a.program === b.program, `${b.id}: bytecode changed`);
    for (let f = 0; f < b.expect.length; f++) {
      assert(a.expect[f] === b.expect[f],
        `${b.id} frame ${b.frames[f]} differs.\n  stored ${a.expect[f].slice(0, 48)}...\n  now    ${b.expect[f].slice(0, 48)}...`);
    }
  }
});

test('golden vectors run from the wire format alone', () => {
  // This is the path the firmware takes: base64 in, pixels out, no compiler.
  const stored = JSON.parse(readFileSync(join(here, 'vectors.json'), 'utf8'));
  for (const c of stored.cases) {
    const program = decodeProgram(fromBase64(c.program));
    const engine = new Engine(stored.geometry, stored.output);
    engine.setLayers([{
      program, params: Float32Array.from(c.params), mask: null, blend: 'normal',
    }]);
    engine.knob = c.knob;
    engine.knobTarget = null;
    engine.sw = c.sw;
    const want = new Set(c.frames);
    let seen = 0;
    for (let f = 0; f <= c.frames[c.frames.length - 1]; f++) {
      const out = engine.step();
      if (!want.has(f)) continue;
      const hex = Array.from(out, (x) => x.toString(16).padStart(2, '0')).join('');
      assert(hex === c.expect[seen], `${c.id} frame ${f} differs when run from bytecode`);
      seen++;
    }
  }
});

// --- report ------------------------------------------------------------------

if (failures.length) {
  console.error(`\n${failures.length} failed, ${passed} passed\n`);
  for (const f of failures) console.error(`  x ${f}`);
  process.exit(1);
}
console.log(`${passed} tests passed`);
