// Builds the golden vector set. Shared by the generator and the checker so the
// two can never disagree about how a case is set up.

import { Engine, FRAME_RATE, DEFAULT_OUTPUT } from '../src/model/engine.js';
import { BUILTIN_DEFINITIONS } from '../src/model/effects.js';
import { compile } from '../src/lang/compile.js';
import { encodeProgram, toBase64 } from '../src/lang/serialize.js';
import { FORMAT_VERSION } from '../src/lang/ops.js';

// Pinned in full rather than spread from the defaults: these vectors are the
// contract the firmware evaluator is held to, and they must not quietly change
// because somebody adjusted a default.
const GEOMETRY = {
  scaleLength: 648,
  frets: 22,
  ledsPerStrip: 22,
  mapping: 'even',
  firstFret: 0,
  reversed: [false, false],
  nutWidth: 43,
  heelWidth: 56,
  stripSpacing: 30,
};
const OUTPUT = { ...DEFAULT_OUTPUT, brightnessCeiling: 1, currentBudget: 100000 };
const FRAMES = [0, 1, 2, 5, 17, 59, 60, 121, 300];

const hex = (bytes) => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');

// Off-default but not degenerate: spread across the middle of each range, so a
// parameter wired to the wrong slot shows up without any effect landing on a
// value that renders nothing.
function paramsFor(program) {
  return Float32Array.from(
    program.params.map((p, i) => p.min + (0.35 + 0.3 * (((i * 5 + 2) % 7) / 6)) * (p.max - p.min))
  );
}

// Not a real effect: this one exists to pin the arithmetic edges that the two
// evaluators are most likely to disagree about.
const TORTURE = {
  id: '_edges',
  source: `param k 0..4 = 1.5 "K"

z    = floor(u * 2)
dz   = 1 / z
mneg = mod(-fret, 3)
nz   = noise(fret * 1.7 + t)
hz   = hash(n, side)
pw   = pow(u + 0.1, k)
lg   = log(u)
sq   = sqrt(u - 0.5)
tn   = tan(u * PI) * 0.001
v    = sat(0.15 * dz + 0.15 * mneg + 0.2 * nz + 0.2 * hz + 0.1 * pw + 0.05 * lg + 0.1 * sq + tn)
h    = mod(fret * 37 - t * 90, 360)
s    = 1 - rnd * 0.5
`,
};

export function buildVectors() {
  const cases = [];

  for (const def of [...BUILTIN_DEFINITIONS, TORTURE]) {
    const program = compile(def.source);
    const params = paramsFor(program);
    const knob = 0.73;
    const sw = 2;

    const engine = new Engine(GEOMETRY, OUTPUT);
    engine.setLayers([{ program, params, mask: null, blend: 'normal' }]);
    engine.knob = knob;
    engine.knobTarget = null;
    engine.sw = sw;

    const expect = [];
    const want = new Set(FRAMES);
    const maxFrame = Math.max(...FRAMES);
    for (let f = 0; f <= maxFrame; f++) {
      const out = engine.step();
      if (want.has(f)) expect.push(hex(out));
    }

    cases.push({
      id: def.id,
      source: def.source,
      program: toBase64(encodeProgram(program)),
      params: Array.from(params),
      knob,
      sw,
      frames: FRAMES,
      expect,
    });
  }

  return {
    formatVersion: FORMAT_VERSION,
    frameRate: FRAME_RATE,
    note: 'Expected output is the 8-bit RGB frame, side 0 first, as lowercase hex. ' +
          'The firmware evaluator must reproduce these from the base64 program alone.',
    geometry: GEOMETRY,
    output: OUTPUT,
    cases,
  };
}

export { GEOMETRY, OUTPUT, FRAMES, hex };
