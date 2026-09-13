// Neck geometry and the LED -> fretboard mapping.
//
// Everything here is a setting, not a constant: the real strips are only
// "roughly" one LED per fret, and nobody has opened the guitar yet. On the
// device this whole object is remotely adjustable.

// Measured on the guitar. See docs/hardware/.
export const DEFAULT_GEOMETRY = {
  scaleLength: 648,     // mm, 25.5" Strat scale
  frets: 21,
  ledsPerStrip: 26,
  // A commercial LED tape has a fixed pitch, so its LEDs are evenly spaced in
  // millimetres and cannot line up with frets, which are not. 'fret-midpoint'
  // stays available in case a strip was cut and re-spaced by hand.
  mapping: 'even', // or 'fret-midpoint'
  firstFret: 0,         // index of the fret space holding LED 0 ('fret-midpoint' only)
  nutToFirstLed: 20,    // mm from the nut to the first LED
  lastLedToLastFret: 20,// mm from the last LED to the last fret
  // Both strips are fed from the body end, where the controller lives, so the
  // electrical index runs body -> nut: LED 0 sits at the highest fret.
  reversed: [true, true],
  nutWidth: 43,         // mm, for drawing only
  heelWidth: 56,
  // Centre-to-centre distance between the two strips, in mm - the thing you can
  // actually get a ruler across. They run on the fretboard, out near the edges,
  // just inside the outer strings. Drawing only: effects address position along
  // the neck and cannot see where a strip sits across it.
  stripSpacing: 27,
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

// The span the LEDs actually occupy, in mm from the nut.
export function litSpan(g) {
  const lastFret = fretDistance(g.frets, g.scaleLength);
  return { from: g.nutToFirstLed ?? 0, to: lastFret - (g.lastLedToLastFret ?? 0) };
}

// Centre-to-centre LED pitch. A commercial tape has a fixed pitch, so this is a
// useful sanity check: it should land near a standard density (60/m is
// 16.67 mm, 30/m is 33.3 mm). If it does not, one of the measurements is off.
export function ledPitch(g) {
  const { from, to } = litSpan(g);
  return g.ledsPerStrip > 1 ? (to - from) / (g.ledsPerStrip - 1) : 0;
}

// Positions of every LED on one strip, nut-first, in mm from the nut.
function stripPositions(g) {
  const out = [];
  const { from, to } = litSpan(g);
  for (let i = 0; i < g.ledsPerStrip; i++) {
    if (g.mapping === 'even') {
      const t = g.ledsPerStrip === 1 ? 0 : i / (g.ledsPerStrip - 1);
      out.push(from + t * (to - from));
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
