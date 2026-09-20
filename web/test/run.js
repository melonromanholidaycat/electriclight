import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { compile, CompileError } from '../src/lang/compile.js';
import { createRunner } from '../src/lang/eval.js';
import { encodeProgram, decodeProgram, toBase64, fromBase64 } from '../src/lang/serialize.js';
import { VARS, FORMAT_VERSION } from '../src/lang/ops.js';
import { Engine, DEFAULT_OUTPUT } from '../src/model/engine.js';
import { fretDistance, fretAt, buildPixels, ledPitch, litSpan, DEFAULT_GEOMETRY } from '../src/model/geometry.js';
import { defaultLibrary, buildLayers, presetsByDefinition, resolveValues, programFor } from '../src/model/library.js';
import { buildVectors, GEOMETRY, OUTPUTS } from './vectors.js';
import { manifestFor, VARIANTS, PARTITION_TABLE_OFFSET } from '../gen-flasher.js';
import { buildEffectsPayload } from '../src/ui/device.js';

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

test('warp is identity at zero, monotonic, and pinned at both ends', () => {
  const at = (x, c, a) => evalSource(`v = warp(${x}, ${c}, ${a})`).v;
  for (let i = 0; i <= 10; i++) near(at(i / 10, 0.5, 0), i / 10, 1e-6, 'identity');
  for (const c of [0, 0.25, 0.5, 0.9, 1]) {
    for (const a of [-3, -1, 0, 1, 3]) {
      near(at(0, c, a), 0, 1e-6, `start pinned (c=${c} a=${a})`);
      near(at(1, c, a), 1, 1e-6, `end pinned (c=${c} a=${a})`);
      let last = -1;
      for (let i = 0; i <= 20; i++) {
        const y = at(i / 20, c, a);
        assert(y >= last - 1e-6, `not monotonic at c=${c} a=${a}, x=${i / 20}`);
        assert(y >= 0 && y <= 1, `left 0..1 at c=${c} a=${a}: ${y}`);
        last = y;
      }
    }
  }
  // Positive amount bunches the pattern up at the centre, negative spreads it.
  const spread = (a) => at(0.6, 0.5, a) - at(0.4, 0.5, a);
  assert(spread(2) > spread(0), 'positive amount should squeeze at the centre');
  assert(spread(-2) < spread(0), 'negative amount should stretch at the centre');
  // Inputs outside 0..1 are clamped rather than undefined.
  near(at(-5, 0.5, 1), 0, 1e-6, 'below range');
  near(at(5, 0.5, 1), 1, 1e-6, 'above range');
});

test('warp knows nothing about frets', () => {
  // It is a plain coordinate remap: same numbers in, same numbers out, whatever
  // the neck looks like.
  const a = evalSource('v = warp(0.3, 0.7, 1.5)', { nfrets: 22 }).v;
  const b = evalSource('v = warp(0.3, 0.7, 1.5)', { nfrets: 24 }).v;
  assert(a === b, 'warp should not depend on the neck');
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
  // Direction pinned rather than taken from the defaults, which describe the
  // real guitar and are fed from the body end.
  const px = buildPixels({ ...DEFAULT_GEOMETRY, ledsPerStrip: 22, reversed: [false, false] });
  assert(px.length === 44, `expected 44 pixels, got ${px.length}`);
  near(px[0].u, 0, 1e-9);
  near(px[21].u, 1, 1e-9);
  assert(px[0].side === 0 && px[22].side === 1, 'sides are not grouped as expected');
  assert(px[0].fret < px[21].fret, 'fret should increase toward the body');
});

// docs/hardware/README.md is the only place the measurements live. This reads
// them back out of it, so the code cannot drift from the documentation without
// the build saying so.
function documentedMeasurements() {
  const md = readFileSync(join(here, '..', '..', 'docs', 'hardware', 'README.md'), 'utf8');
  const out = {};
  for (const line of md.split('\n')) {
    const row = line.match(/^\|([^|]*)\|([^|]*)\|\s*`([A-Za-z]+)`\s*\|/);
    if (!row) continue;
    const value = row[2].match(/-?\d+(?:\.\d+)?/);
    if (value) out[row[3]] = parseFloat(value[0]);
  }
  return out;
}

test('the simulator defaults match the documented measurements', () => {
  const documented = documentedMeasurements();
  const keys = Object.keys(documented);
  assert(keys.length >= 6, `only found ${keys.length} keyed rows in docs/hardware/README.md`);
  for (const [key, value] of Object.entries(documented)) {
    assert(key in DEFAULT_GEOMETRY, `docs/hardware/README.md documents '${key}', which is not a geometry setting`);
    near(DEFAULT_GEOMETRY[key], value, 1e-9,
      `'${key}' is ${DEFAULT_GEOMETRY[key]} in geometry.js but ${value} in docs/hardware/README.md.`);
  }
});

// The firmware carries its own copy of these numbers, because it cannot import
// a JavaScript module. Two copies is one too many, so the second one is read
// back out of the C and compared - the same trick as the documentation check
// above, pointed at the other twin.
function firmwareStruct(file, name) {
  const src = readFileSync(join(here, '..', '..', 'firmware', 'components', 'core', file), 'utf8');
  const block = src.match(new RegExp(`${name}\\s*=\\s*\\{([\\s\\S]*?)\\n\\};`));
  assert(block, `${file} no longer defines ${name}`);
  const out = {};
  // The alternation is for brace initialisers like `.reversed = { true, true }`,
  // whose own comma would otherwise end the value early.
  for (const m of block[1].matchAll(/\.([a-z_0-9]+)\s*=\s*(\{[^}]*\}|[^,{}]+),/g)) {
    out[m[1]] = m[2].trim();
  }
  return out;
}

const snake = (s) => s.replace(/[A-Z]/g, (c) => `_${c.toLowerCase()}`);
const cNumber = (v) => parseFloat(v.replace(/f$/, ''));

// Settings the simulator needs only to draw a picture. The firmware drives LEDs
// and never draws a neck, so it deliberately does not carry these.
const DRAWING_ONLY = ['nutWidth', 'heelWidth', 'stripSpacing'];

test('the firmware geometry matches the simulator', () => {
  const c = firmwareStruct('el_geometry.c', 'EL_DEFAULT_GEOMETRY');
  for (const [key, value] of Object.entries(DEFAULT_GEOMETRY)) {
    const name = snake(key);
    if (DRAWING_ONLY.includes(key)) {
      assert(!(name in c), `'${key}' is drawing-only, but el_geometry.c carries it`);
      continue;
    }
    assert(name in c, `el_geometry.c has no '${name}' for the simulator's '${key}'`);
    if (key === 'mapping') {
      const want = value === 'even' ? 'EL_MAP_EVEN' : 'EL_MAP_FRET_MIDPOINT';
      assert(c[name] === want, `mapping is '${value}' in geometry.js but ${c[name]} in el_geometry.c`);
    } else if (key === 'reversed') {
      assert(c[name] === `{ ${value[0]}, ${value[1]} }`,
        `reversed is [${value}] in geometry.js but ${c[name]} in el_geometry.c`);
    } else {
      near(cNumber(c[name]), value, 1e-6,
        `'${key}' is ${value} in geometry.js but ${c[name]} in el_geometry.c`);
    }
  }
});

test('the firmware output chain defaults match the simulator', () => {
  const c = firmwareStruct('el_engine.c', 'EL_DEFAULT_OUTPUT');
  for (const [key, value] of Object.entries(DEFAULT_OUTPUT)) {
    // mAPerLed -> ma_per_led: the leading lowercase run is one word.
    const name = snake(key).replace(/^m_a/, 'ma');
    assert(name in c, `el_engine.c has no '${name}' for the simulator's '${key}'`);
    near(cNumber(c[name]), value, 1e-6,
      `'${key}' is ${value} in engine.js but ${c[name]} in el_engine.c`);
  }
});

// docs/effect-format.md publishes the limits an effect may rely on. The firmware
// is what enforces them, so the two are checked against each other rather than
// both being written down and hoped about.
// Any document that quotes the size of the golden vector set has to be right
// about it. The set grows; prose does not notice.
// --- what the page sends the guitar -------------------------------------------

test('the default library compiles into a payload the firmware accepts', () => {
  const { payload, problems } = buildEffectsPayload(defaultLibrary());
  assert(problems.length === 0, `default library will not compile: ${problems.join('; ')}`);
  assert(payload.slots.length === 5, `payload has ${payload.slots.length} slots, expected 5`);
  for (const slot of payload.slots) {
    assert(slot && slot.program, 'a default slot produced no program');
    // The firmware decodes this and nothing else, so it has to survive the
    // round trip through base64 and the wire format.
    const program = decodeProgram(fromBase64(slot.program));
    assert(program.nParams === slot.params.length,
      `${slot.name}: ${slot.params.length} values sent for ${program.nParams} parameters`);
  }
});

test('the payload fits in the buffer the firmware reserves for it', () => {
  // A payload that outgrew the device's limit would be refused with a message
  // nobody would connect to having added an effect. Cheaper to notice here.
  const header = readFileSync(join(here, '..', '..', 'firmware', 'main', 'app_effects.h'), 'utf8');
  const limit = header.match(/#define\s+APP_EFFECTS_MAX_JSON\s+(\d+)/);
  assert(limit, 'app_effects.h no longer states a payload limit');

  const { payload } = buildEffectsPayload(defaultLibrary());
  const size = JSON.stringify(payload).length;
  assert(size < Number(limit[1]),
    `the default payload is ${size} bytes and the firmware accepts ${limit[1]}`);
  // Half the budget spent before the owner has written a single effect of their
  // own would be a limit that is about to become a problem.
  assert(size < Number(limit[1]) / 2,
    `the default payload already uses ${Math.round((size / Number(limit[1])) * 100)}% ` +
    'of the firmware\'s limit; raise APP_EFFECTS_MAX_JSON before it bites');
});

// --- the web flasher ----------------------------------------------------------
//
// It gets one shot on a borrowed computer, so the things that would waste that
// shot are checked here rather than discovered there.

// The manifest derives the otadata and app offsets from the same CSV the
// firmware is built from, so those two cannot drift and are not worth asserting.
// What is worth asserting is everything the CSV does not say: which slot the app
// belongs in, and the two addresses that come from the chip rather than the
// layout.
test('the flasher writes the app to the first OTA slot', () => {
  for (const variant of VARIANTS) {
    const csv = readFileSync(join(here, '..', '..', 'firmware', 'partitions', `${variant}.csv`), 'utf8');
    const apps = csv.split('\n')
      .filter((l) => !l.trim().startsWith('#') && l.includes(','))
      .map((l) => l.split(',').map((c) => c.trim()))
      .filter((c) => c[1] === 'app')
      .map((c) => ({ name: c[0], offset: parseInt(c[3], 16) }))
      .sort((a, b) => a.offset - b.offset);
    assert(apps.length >= 2, `${variant}.csv has ${apps.length} app partitions; OTA needs two`);

    const parts = Object.fromEntries(
      manifestFor(variant).builds[0].parts.map((p) => [p.path, p.offset]));
    // A cable flash must land in the slot a factory-fresh otadata points at,
    // which is the lowest-addressed one. Writing the app to the other slot
    // produces a board that flashes cleanly and boots nothing.
    assert(parts['electriclight.bin'] === apps[0].offset,
      `${variant}: the app is written to 0x${parts['electriclight.bin'].toString(16)}, ` +
      `but the first OTA slot (${apps[0].name}) is at 0x${apps[0].offset.toString(16)}`);
  }
});

test('the flasher targets the chip the firmware is built for', () => {
  const sdkconfig = readFileSync(join(here, '..', '..', 'firmware', 'sdkconfig.defaults'), 'utf8');
  const target = sdkconfig.match(/^CONFIG_IDF_TARGET="([^"]+)"/m);
  assert(target, 'sdkconfig.defaults no longer names a target');

  // Neither of these comes from the partition table. The bootloader address is a
  // property of the chip: the S3 boots from 0, the original ESP32 from 0x1000.
  // Flashing an S3 image to 0x1000 leaves a board that never starts, and the
  // symptom looks identical to a dead board.
  const BOOTLOADER_AT = { esp32s3: 0, esp32c3: 0, esp32s2: 0x1000, esp32: 0x1000 };
  const FAMILY = { esp32s3: 'ESP32-S3', esp32c3: 'ESP32-C3', esp32s2: 'ESP32-S2', esp32: 'ESP32' };
  const want = BOOTLOADER_AT[target[1]];
  assert(want !== undefined, `no bootloader offset known for target ${target[1]}`);

  for (const variant of VARIANTS) {
    const build = manifestFor(variant).builds[0];
    assert(build.chipFamily === FAMILY[target[1]],
      `${variant}: the flasher offers ${build.chipFamily} but the firmware targets ${target[1]}`);
    const boot = build.parts.find((p) => p.path === 'bootloader.bin');
    assert(boot.offset === want,
      `${variant}: bootloader at 0x${boot.offset.toString(16)}, but ${target[1]} boots from 0x${want.toString(16)}`);
  }
});

test('the partition table offset is not overridden in sdkconfig', () => {
  // The flasher writes the partition table to a hard offset. If a build ever
  // moves it, the flasher would quietly write it to the old address.
  const defaults = readFileSync(join(here, '..', '..', 'firmware', 'sdkconfig.defaults'), 'utf8')
    + readFileSync(join(here, '..', '..', 'firmware', 'sdkconfig.16mb'), 'utf8');
  const override = defaults.match(/^CONFIG_PARTITION_TABLE_OFFSET=(\S+)/m);
  assert(!override || parseInt(override[1], 16) === PARTITION_TABLE_OFFSET,
    `sdkconfig sets the partition table offset to ${override && override[1]}, ` +
    `but the flasher writes it to 0x${PARTITION_TABLE_OFFSET.toString(16)}`);
});

test('the flasher pins its dependency to an exact version', () => {
  const page = readFileSync(join(here, '..', 'flasher', 'flash.html'), 'utf8');
  const url = page.match(/https:\/\/unpkg\.com\/esp-web-tools@([^/]+)\//);
  assert(url, 'the flasher no longer loads esp-web-tools from a pinned URL');
  assert(/^\d+\.\d+\.\d+$/.test(url[1]),
    `esp-web-tools is pinned to "${url[1]}", which is not an exact version. ` +
    'A flasher that changes under you between testing and a borrowed laptop is not a tool.');
});

test('the flasher does not promise a light nothing drives yet', () => {
  // The page is read by somebody standing over a borrowed laptop deciding
  // whether the flash worked. Telling him to look for an LED that no firmware
  // lights would make a good board look like a dead one.
  const page = readFileSync(join(here, '..', 'flasher', 'flash.html'), 'utf8');
  // Checking that the pin is mentioned is not enough, and this test passed for a
  // week while the LED stayed dark: app_leds.c named the pin and set up its RMT
  // channel, and nothing ever asked it to light. What matters is that something
  // outside the driver calls it every frame.
  const callers = ['main.c', 'app_render.c', 'app_http.c', 'app_mode.c']
    .map((f) => readFileSync(join(here, '..', '..', 'firmware', 'main', f), 'utf8')).join('');
  const driven = /app_leds_onboard\s*\(/.test(callers);
  const promised = /own RGB LED|onboard LED|watch the LED/i.test(page);
  assert(driven || !promised,
    'the flasher tells the owner to look at the onboard LED, but nothing drives it');
});

test('the flasher quotes the real access point credentials', () => {
  // The page tells the owner what network to join to confirm the flash worked,
  // while the borrowed computer is still in the room. Credentials that had
  // drifted from the firmware would send him looking for a network that is not
  // there, and the conclusion he would draw is that the board is dead.
  const page = readFileSync(join(here, '..', 'flasher', 'flash.html'), 'utf8');
  const identity = readFileSync(join(here, '..', '..', 'firmware', 'main', 'app_identity.h'), 'utf8');
  for (const name of ['ELECTRICLIGHT_AP_SSID', 'ELECTRICLIGHT_AP_PASSWORD']) {
    const value = identity.match(new RegExp(`${name}\\s+"([^"]+)"`));
    assert(value, `app_identity.h no longer defines ${name}`);
    assert(page.includes(`>${value[1]}<`),
      `the flasher does not quote ${name} ("${value[1]}") as the firmware defines it`);
  }
});

test('the flasher tells the truth about which browsers work', () => {
  // The owner has only an iPhone. A page that let him believe he could flash
  // from it would cost him a wasted evening rather than a wasted click.
  const page = readFileSync(join(here, '..', 'flasher', 'flash.html'), 'utf8');
  assert(/'serial' in navigator/.test(page), 'the flasher no longer checks for Web Serial');
  assert(/iOS/.test(page), 'the flasher no longer says that iOS cannot flash');
});

test('documented golden-vector counts match the vector set', () => {
  const stored = JSON.parse(readFileSync(join(here, 'vectors.json'), 'utf8'));
  const frames = stored.cases.reduce((n, c) => n + c.frames.length, 0);
  let found = 0;
  for (const doc of ['README.md', 'AGENTS.md', 'docs/decisions.md', 'docs/effect-format.md']) {
    const text = readFileSync(join(here, '..', '..', ...doc.split('/')), 'utf8');
    for (const m of text.matchAll(/(\d+) golden frames/g)) {
      found++;
      assert(Number(m[1]) === frames,
        `${doc} says ${m[1]} golden frames; there are ${frames}`);
    }
  }
  assert(found >= 1, 'no document quotes the golden frame count any more');
});

test('the documented format limits match the firmware constants', () => {
  const md = readFileSync(join(here, '..', '..', 'docs', 'effect-format.md'), 'utf8');
  const headers = readFileSync(join(here, '..', '..', 'firmware', 'components', 'core', 'el_program.h'), 'utf8')
    + readFileSync(join(here, '..', '..', 'firmware', 'components', 'core', 'el_eval.h'), 'utf8');

  let found = 0;
  for (const line of md.split('\n')) {
    const row = line.match(/^\|([^|]*)\|\s*(\d+)\s*\|\s*`(EL_MAX_[A-Z_]+)`\s*\|/);
    if (!row) continue;
    found++;
    const [, label, value, name] = row;
    const def = headers.match(new RegExp(`#define\\s+${name}\\s+(\\d+)`));
    assert(def, `effect-format.md documents ${name}, which the firmware does not define`);
    assert(def[1] === value,
      `${label.trim()} is ${value} in effect-format.md but ${def[1]} in the firmware (${name})`);
  }
  assert(found >= 5, `only found ${found} documented limits in effect-format.md`);
});

test('the measured geometry lands on a standard tape density', () => {
  // The measurements only come out near a real strip pitch if they are right,
  // so this is a free check on the whole set of them.
  const g = DEFAULT_GEOMETRY;
  const perMetre = 1000 / ledPitch(g);
  near(perMetre, 60, 1, 'implied strip density');
  const span = litSpan(g);
  near(span.from, g.nutToFirstLed, 1e-9, 'nut to first LED');
  near(span.to, fretDistance(g.frets, g.scaleLength) - g.lastLedToLastFret, 1e-9,
    'last LED to last fret');
});

test('both strips are fed from the body, so index 0 sits at the last fret', () => {
  const g = DEFAULT_GEOMETRY;
  const n = g.ledsPerStrip;
  const px = buildPixels(g);
  assert(px.length === n * 2, `expected ${n * 2} pixels, got ${px.length}`);
  for (const side of [0, 1]) {
    const first = px[side * n];
    const last = px[side * n + n - 1];
    assert(first.n === 0 && last.n === n - 1, `electrical index should run 0..${n - 1}`);
    near(first.u, 1, 1e-9, `side ${side} index 0 should be at the body`);
    near(last.u, 0, 1e-9, `side ${side} index 25 should be at the nut`);
  }
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
    const e = new Engine({ ...GEOMETRY }, { ...OUTPUTS.unclipped, brightnessCeiling: ceiling });
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
  const e = new Engine({ ...GEOMETRY }, { ...OUTPUTS.unclipped, brightnessCeiling: 1, currentBudget: 300 });
  e.setLayers([{ program, params: new Float32Array(0), mask: null, blend: 'normal' }]);
  e.knob = 1;
  e.knobTarget = null;
  e.step();
  assert(e.limited, 'should have limited');
  assert(e.current <= 300 * 1.02, `over budget: ${e.current} mA`);
});

test('masks keep a layer off the pixels it does not own', () => {
  const program = compile('v = 1\ns = 0\n');
  const e = new Engine({ ...GEOMETRY }, { ...OUTPUTS.unclipped, brightnessCeiling: 1 });
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
  const e = new Engine({ ...GEOMETRY }, { ...OUTPUTS.unclipped, brightnessCeiling: 1, gamma: 1 });
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

// --- the page/firmware contract ----------------------------------------------

test('the page and the firmware agree on how a guitar identifies itself', () => {
  // The page decides it is live rather than a simulator by fetching api/status
  // and matching one string. The firmware puts that string there. Nothing else
  // connects the two, so nothing else would notice if one of them changed.
  const page = readFileSync(join(here, '..', 'src', 'main.js'), 'utf8');
  const identity = readFileSync(join(here, '..', '..', 'firmware', 'main', 'app_identity.h'), 'utf8');

  // Matched on the property, not the variable holding it: this caught a rename
  // once, which is the guard working, but the name is not the thing it is
  // supposed to be protecting.
  const wanted = page.match(/\.device === '([^']+)'/);
  assert(wanted, 'web/src/main.js no longer compares a .device field to a literal');

  const declared = identity.match(/#define\s+ELECTRICLIGHT_DEVICE_ID\s+"([^"]+)"/);
  assert(declared, 'app_identity.h no longer defines ELECTRICLIGHT_DEVICE_ID');

  assert(wanted[1] === declared[1],
    `the page looks for device "${wanted[1]}" but the firmware reports "${declared[1]}"`);

  const endpoint = page.match(/fetch\('([^']+)'/);
  assert(endpoint && endpoint[1] === 'api/status',
    `the page fetches "${endpoint && endpoint[1]}"; the firmware serves /api/status`);
  assert(/"\/api\/status"/.test(readFileSync(join(here, '..', '..', 'firmware', 'main', 'app_http.c'), 'utf8')),
    'the firmware no longer routes /api/status');
});

// --- golden vectors ----------------------------------------------------------

test('golden vectors still match', () => {
  const stored = JSON.parse(readFileSync(join(here, 'vectors.json'), 'utf8'));
  const fresh = buildVectors();
  assert(stored.formatVersion === fresh.formatVersion, 'format version moved');
  // The stage profile only earns its place if something actually clips. If a
  // changed default quietly stops tripping the limiter, this says so rather
  // than letting the vectors keep passing for the wrong reason.
  assert(stored.cases.some((c) => c.limited),
    'no stored case trips the current limiter, so nothing checks it');
  assert(stored.cases.length === fresh.cases.length,
    `case count changed (${stored.cases.length} stored, ${fresh.cases.length} now)`);
  for (let i = 0; i < fresh.cases.length; i++) {
    const a = stored.cases[i];
    const b = fresh.cases[i];
    assert(a.id === b.id, `case ${i} is now "${b.id}", was "${a.id}"`);
    assert(a.output === b.output, `${b.id}: output profile changed`);
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
    const engine = new Engine(stored.geometry, stored.outputs[c.output]);
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
