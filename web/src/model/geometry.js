// Neck geometry and the LED -> fretboard mapping.
//
// Everything here is a setting, not a constant: the real strips are only
// "roughly" one LED per fret, and nobody has opened the guitar yet. On the
// device this whole object is remotely adjustable.

export const DEFAULT_GEOMETRY = {
  scaleLength: 648,     // mm, 25.5" Strat scale
  frets: 22,
  ledsPerStrip: 22,
  mapping: 'fret-midpoint', // or 'even'
  firstFret: 0,         // index of the fret space holding LED 0
  reversed: [false, false], // per side: does the strip run body -> nut?
  nutWidth: 43,         // mm, for drawing only
  heelWidth: 56,
};

// Distance from the nut to fret n, in mm.
export function fretDistance(n, scaleLength) {
  return scaleLength * (1 - Math.pow(2, -n / 12));
}

// Fractional fret number at a distance from the nut. Inverse of fretDistance.
export function fretAt(mm, scaleLength) {
  const r = 1 - mm / scaleLength;
  return r <= 0 ? Infinity : -12 * Math.log2(r);
}

// Positions of every LED on one strip, nut-first, in mm from the nut.
function stripPositions(g) {
  const out = [];
  const lastFret = fretDistance(g.frets, g.scaleLength);
  for (let i = 0; i < g.ledsPerStrip; i++) {
    if (g.mapping === 'even') {
      const t = g.ledsPerStrip === 1 ? 0 : i / (g.ledsPerStrip - 1);
      out.push(t * lastFret);
    } else {
      // Sit each LED in the middle of a fret space, which is where a side dot goes.
      const a = fretDistance(g.firstFret + i, g.scaleLength);
      const b = fretDistance(g.firstFret + i + 1, g.scaleLength);
      out.push((a + b) / 2);
    }
  }
  return out;
}

// The per-pixel constants the effect evaluator reads. Built once per geometry
// change, then reused every frame.
export function buildPixels(g) {
  const mm = stripPositions(g);
  const lo = mm[0];
  const hi = mm[mm.length - 1];
  const span = hi - lo || 1;
  const pixels = [];

  for (let side = 0; side < 2; side++) {
    for (let i = 0; i < g.ledsPerStrip; i++) {
      // The strip may be soldered in either direction; n is the electrical
      // index, the position is the physical one.
      const pos = g.reversed[side] ? mm[g.ledsPerStrip - 1 - i] : mm[i];
      pixels.push({
        side,
        n: i,
        mm: pos,
        u: (pos - lo) / span,
        fret: fretAt(pos, g.scaleLength),
      });
    }
  }
  return pixels;
}

// Fixed per-pixel randoms. Seeded so the simulator and the firmware agree.
export function buildRandoms(count) {
  const out = new Float32Array(count);
  let s = 0x2545f491;
  for (let i = 0; i < count; i++) {
    s ^= s << 13; s >>>= 0;
    s ^= s >>> 17;
    s ^= s << 5; s >>>= 0;
    out[i] = (s >>> 8) / 16777216;
  }
  return out;
}

export const INLAY_FRETS = [3, 5, 7, 9, 15, 17, 19, 21];
export const DOUBLE_INLAY_FRETS = [12, 24];
