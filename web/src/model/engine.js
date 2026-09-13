// The render engine: fixed-rate effect clock, layer compositing, and the
// output chain that the effect itself is not allowed to reach into.
//
// The clock is fixed at 60 Hz on purpose. Effects can read their own previous
// frame (`prev`), so a variable frame rate would make trails and decay look
// different on the device than in the simulator - exactly the drift this
// project is built to avoid. The simulator therefore steps whole 1/60 s frames
// and drops or repeats them rather than scaling time.

import { createRunner } from '../lang/eval.js';
import { VARS } from '../lang/ops.js';
import { buildPixels, buildRandoms } from './geometry.js';

export const FRAME_RATE = 60;
export const FRAME_MS = 1000 / FRAME_RATE;

export const DEFAULT_OUTPUT = {
  brightnessCeiling: 0.5, // remotely tunable; the most useful runtime control we have
  gamma: 2.2,
  mAPerLed: 60,           // WS2812B at full white, all three channels
  currentBudget: 1500,    // mA the supply is trusted to deliver
  idleCurrent: 1,         // mA per LED with the channels off
};

function gammaTable(gamma) {
  const t = new Uint8Array(256);
  for (let i = 0; i < 256; i++) t[i] = Math.round(255 * Math.pow(i / 255, gamma));
  return t;
}

function hsvToRgb(h, s, v, out) {
  h = h - 360 * Math.floor(h / 360);
  s = s < 0 ? 0 : s > 1 ? 1 : s;
  v = v < 0 ? 0 : v > 1 ? 1 : v;
  const c = v * s;
  const hp = h / 60;
  const x = c * (1 - Math.abs((hp % 2) - 1));
  let r = 0, g = 0, b = 0;
  if (hp < 1) { r = c; g = x; }
  else if (hp < 2) { r = x; g = c; }
  else if (hp < 3) { g = c; b = x; }
  else if (hp < 4) { g = x; b = c; }
  else if (hp < 5) { r = x; b = c; }
  else { r = c; b = x; }
  const m = v - c;
  out[0] = r + m; out[1] = g + m; out[2] = b + m;
}

export class Engine {
  constructor(geometry, output = DEFAULT_OUTPUT) {
    this.output = { ...output };
    this.gammaLut = gammaTable(this.output.gamma);
    this.frame = 0;
    this.knob = 1;
    this.sw = 0;
    this.layers = [];
    this.setGeometry(geometry);
  }

  setGeometry(g) {
    this.geometry = g;
    this.pixels = buildPixels(g);
    this.randoms = buildRandoms(this.pixels.length);
    this.rgb = new Float32Array(this.pixels.length * 3);
    this.out8 = new Uint8Array(this.pixels.length * 3);
    this.vars = new Float32Array(VARS.length);
    this.scratch = new Float32Array(3);
    this.tmpRgb = new Float32Array(3);
    this.resetHistory();
  }

  setOutput(patch) {
    Object.assign(this.output, patch);
    this.gammaLut = gammaTable(this.output.gamma);
  }

  resetHistory() {
    const n = this.pixels.length * Math.max(1, this.layers.length);
    this.prev = new Float32Array(n);
  }

  // layers: [{ program, params: Float32Array, mask, blend }]
  setLayers(layers) {
    this.layers = layers.map((l) => ({ ...l, run: createRunner(l.program) }));
    this.resetHistory();
  }

  reset() {
    this.frame = 0;
    this.resetHistory();
    this.rgb.fill(0);
  }

  step() {
    const px = this.pixels;
    const nPx = px.length;
    const t = this.frame / FRAME_RATE;
    const vars = this.vars;
    const out = this.scratch;
    const rgb = this.rgb;
    const tmp = this.tmpRgb;

    rgb.fill(0);

    vars[0] = t;
    vars[5] = this.geometry.ledsPerStrip;
    vars[6] = this.geometry.frets;
    vars[7] = this.knob;
    vars[8] = this.sw;

    for (let li = 0; li < this.layers.length; li++) {
      const layer = this.layers[li];
      const prevBase = li * nPx;
      const mask = layer.mask;

      for (let i = 0; i < nPx; i++) {
        const p = px[i];
        if (mask) {
          if (!mask.sides[p.side]) continue;
          if (p.fret < mask.fromFret || p.fret > mask.toFret) continue;
        }
        vars[1] = p.fret;
        vars[2] = p.u;
        vars[3] = p.side;
        vars[4] = p.n;
        vars[9] = this.prev[prevBase + i];
        vars[10] = this.randoms[i];

        layer.run(vars, layer.params, out);
        const v = out[2] < 0 ? 0 : out[2] > 1 ? 1 : out[2];
        this.prev[prevBase + i] = v;

        hsvToRgb(out[0], out[1], v, tmp);
        const o = i * 3;
        if (layer.blend === 'add') {
          rgb[o] += tmp[0]; rgb[o + 1] += tmp[1]; rgb[o + 2] += tmp[2];
        } else if (layer.blend === 'max') {
          rgb[o] = Math.max(rgb[o], tmp[0]);
          rgb[o + 1] = Math.max(rgb[o + 1], tmp[1]);
          rgb[o + 2] = Math.max(rgb[o + 2], tmp[2]);
        } else {
          rgb[o] = tmp[0]; rgb[o + 1] = tmp[1]; rgb[o + 2] = tmp[2];
        }
      }
    }

    this.frame++;
    return this.finish();
  }

  // Effect output -> master brightness -> gamma -> ceiling -> current limit.
  //
  // The knob goes in before gamma because a player turning it down wants a
  // perceptual dim. The ceiling goes in after, because it is a power control:
  // half the ceiling has to mean half the current, which is only true on the
  // linear side of gamma. Neither is reachable from an effect - an effect can
  // never brown out the board or cook the pack.
  finish() {
    const { brightnessCeiling, mAPerLed, currentBudget, idleCurrent } = this.output;
    const master = this.masterBrightness();
    const lut = this.gammaLut;
    const rgb = this.rgb;
    const out = this.out8;
    const perChannel = mAPerLed / 3;
    let sum = 0;

    for (let i = 0; i < out.length; i++) {
      const lin = rgb[i] * master;
      const b = Math.round(lut[Math.max(0, Math.min(255, Math.round(lin * 255)))] * brightnessCeiling);
      out[i] = b;
      sum += b;
    }

    const nPx = this.pixels.length;
    let current = (sum / 255) * perChannel + nPx * idleCurrent;
    if (current > currentBudget) {
      const room = Math.max(0, currentBudget - nPx * idleCurrent);
      const scale = room / Math.max(1e-6, current - nPx * idleCurrent);
      let rescaled = 0;
      for (let i = 0; i < out.length; i++) {
        out[i] = Math.round(out[i] * scale);
        rescaled += out[i];
      }
      current = (rescaled / 255) * perChannel + nPx * idleCurrent;
      this.limited = true;
    } else {
      this.limited = false;
    }

    this.current = current;
    return out;
  }

  masterBrightness() {
    return this.knobTarget === null || this.knobTarget === undefined ? this.knob : 1;
  }
}
