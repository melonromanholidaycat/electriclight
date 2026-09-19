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
  frets: 21,
  ledsPerStrip: 26,
  mapping: 'even',
  firstFret: 0,
  nutToFirstLed: 20,
  lastLedToLastFret: 20,
  reversed: [true, true],
  nutWidth: 43,
  heelWidth: 56,
  stripSpacing: 27,
};
// Two output profiles, because the output chain has two jobs and the first
// profile deliberately disables the second one.
//
//   unclipped  isolates the evaluator: ceiling wide open, budget effectively
//              infinite, so a disagreement can only come from the maths.
//   stage      is the guitar as it is actually played: a real brightness
//              ceiling and a supply that cannot deliver full white. This is
//              what pins the order of the chain - knob before gamma because a
//              dim should be perceptual, ceiling after gamma because it is a
//              power control - and it is the only thing that exercises the
//              current limiter at all.
//
// The first profile alone passed a build with the ceiling applied on the wrong
// side of gamma, which is how this second one came to exist.
const OUTPUTS = {
  unclipped: { ...DEFAULT_OUTPUT, brightnessCeiling: 1, currentBudget: 100000 },
  // The budget is deliberately below what the _full case draws (about 835 mA
  // at this ceiling and knob), because a limiter that never runs is a limiter
  // nobody has ever tested. The real supply's budget is a setting, not this.
  stage: { ...DEFAULT_OUTPUT, brightnessCeiling: 0.5, currentBudget: 600 },
};
const FRAMES = [0, 1, 2, 5, 17, 59, 60, 121, 300];
// Shorter, because every frame up to the last one has to be rendered to get
// there and the guitar runs these at boot. The chain is stateless: what the
// ceiling and the limiter do to frame 60 they do to frame 300.
const STAGE_FRAMES = [0, 5, 17, 60];

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
w0   = warp(u, 0, k - 2)
w1   = warp(u, 1, 2 - k)
v    = sat(0.15 * dz + 0.15 * mneg + 0.2 * nz + 0.2 * hz + 0.1 * pw + 0.05 * lg + 0.1 * sq + tn)
h    = mod(fret * 37 - t * 90 + 40 * w0 - 40 * w1, 360)
s    = 1 - rnd * 0.5
`,
};

function runCase(def, outputName, frames) {
  const program = compile(def.source);
  const params = paramsFor(program);
  const knob = 0.73;
  const sw = 2;

  const engine = new Engine(GEOMETRY, OUTPUTS[outputName]);
  engine.setLayers([{ program, params, mask: null, blend: 'normal' }]);
  engine.knob = knob;
  engine.knobTarget = null;
  engine.sw = sw;

  const expect = [];
  const want = new Set(frames);
  const maxFrame = Math.max(...frames);
  let limited = false;
  for (let f = 0; f <= maxFrame; f++) {
    const out = engine.step();
    limited = limited || engine.limited;
    if (want.has(f)) expect.push(hex(out));
  }

  return {
    id: outputName === 'unclipped' ? def.id : `${def.id}@${outputName}`,
    output: outputName,
    source: def.source,
    program: toBase64(encodeProgram(program)),
    params: Array.from(params),
    knob,
    sw,
    frames,
    // Recorded so the check below can insist that the stage profile really is
    // doing something, rather than quietly matching because nothing clipped.
    limited,
    expect,
  };
}

// The brightest frame the hardware can be asked for. Not a real effect either:
// it exists so the current limiter has something to limit, and so the top of
// the gamma curve is pinned at full scale rather than only in the middle.
const FULL = {
  id: '_full',
  source: `v = 1
s = 0
h = 0
`,
};

export function buildVectors() {
  const defs = [...BUILTIN_DEFINITIONS, TORTURE, FULL];
  const cases = [
    ...defs.map((d) => runCase(d, 'unclipped', FRAMES)),
    ...defs.map((d) => runCase(d, 'stage', STAGE_FRAMES)),
  ];

  if (!cases.some((c) => c.limited)) {
    throw new Error(
      'no case trips the current limiter, so nothing checks it. Lower ' +
      'OUTPUTS.stage.currentBudget or add a brighter effect.');
  }

  return {
    formatVersion: FORMAT_VERSION,
    frameRate: FRAME_RATE,
    note: 'Expected output is the 8-bit RGB frame, side 0 first, as lowercase hex. ' +
          'The firmware evaluator must reproduce these from the base64 program alone. ' +
          "Each case names the output profile it was rendered under.",
    geometry: GEOMETRY,
    outputs: OUTPUTS,
    cases,
  };
}

export { GEOMETRY, OUTPUTS, FRAMES, STAGE_FRAMES, hex };
