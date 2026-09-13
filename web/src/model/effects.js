// Built-in effect definitions. A definition is maths plus a parameter schema;
// what the owner edits are presets - named sets of values for these params.

export const BUILTIN_DEFINITIONS = [
  {
    id: 'breathe',
    name: 'Breathe',
    note: 'Whole neck, slow swell. The quiet one.',
    source: `param bpm    10..200 = 40   "Breaths / min"
param hue    0..360  = 280  "Colour"
param depth  0..1    = 0.8  "Depth"
param base   0..1    = 0.05 "Floor"

phase = t * bpm / 60
wave  = 0.5 - 0.5 * cos(phase * TAU)
v     = sat(base + (1 - base) * (1 - depth + depth * wave))
h     = hue
s     = 1
`,
  },
  {
    id: 'comet',
    name: 'Comet',
    note: 'Head travels in physical space, tail decays through prev.',
    source: `param speed  0.05..4   = 0.6  "Speed"
param width  0.01..0.4 = 0.06 "Head width"
param hue    0..360    = 190  "Colour"
param tail   0..0.98   = 0.88 "Tail"
param split  0..180    = 30   "Side split"

head = fract(t * speed)
d    = abs(u - head)
d    = min(d, 1 - d)
v    = max(gauss(d, width), prev * tail)
h    = hue + side * split
s    = 1
`,
  },
  {
    id: 'fretchase',
    name: 'Fret Chase',
    note: 'Steps fret by fret, so it slows visually toward the body.',
    source: `param rate  0.5..12   = 4    "Frets / second"
param glow  0.05..3   = 0.6  "Glow (frets)"
param hue   0..360    = 40   "Colour"
param tail  0..0.98   = 0.6  "Tail"

pos = mod(t * rate, nfrets + 1)
d   = abs(fret - pos)
v   = max(gauss(d, glow), prev * tail)
h   = hue
s   = 1
`,
  },
  {
    id: 'standingwave',
    name: 'Standing Wave',
    note: 'The Space knob crossfades physical spacing to fret spacing - the clearest demo of why both exist.',
    source: `param freq  0.5..8  = 2    "Waves over neck"
param speed -3..3   = 0.5  "Speed"
param space 0..1    = 0    "Physical .. fret"
param hue   0..360  = 160  "Colour"
param wash  0..120  = 60   "Hue spread"

pos = mix(u, fret / nfrets, space)
v   = sat(0.5 + 0.5 * sin((pos * freq - t * speed) * TAU))
h   = hue + pos * wash
s   = 1
`,
  },
  {
    id: 'sparkle',
    name: 'Sparkle',
    note: 'hash() fires pixels, prev fades them. No state beyond one frame.',
    source: `param density 0..0.2     = 0.02 "Density"
param decay   0.5..0.995 = 0.92 "Decay"
param hue     0..360     = 50   "Colour"
param jitter  0..180     = 20   "Hue jitter"
param bed     0..0.3     = 0    "Background"

seed = n + side * count
fire = hash(seed, floor(t * 60)) < density
v    = max(fire, max(prev * decay, bed))
h    = hue + (rnd - 0.5) * jitter
s    = 1
`,
  },
  {
    id: 'sides',
    name: 'Split Sides',
    note: 'Static two-tone. Useful for checking the strips are mapped the right way round.',
    source: `param bass   0..360 = 210 "Bass side"
param treble 0..360 = 330 "Treble side"
param level  0..1   = 0.7 "Level"
param taper  0..1   = 0.5 "Taper to body"

h = side < 0.5 ? bass : treble
v = sat(level * mix(1, 1 - u, taper))
s = 1
`,
  },
];

// A few presets so the library has something under each definition on day one.
export const BUILTIN_PRESETS = [
  { defId: 'comet', name: 'Blue Runner', values: {} },
  { defId: 'comet', name: 'Slow Amber', values: { speed: 0.18, hue: 30, tail: 0.94, width: 0.1 } },
  { defId: 'breathe', name: 'Standby', values: { bpm: 12, hue: 265, depth: 0.7, base: 0.02 } },
  { defId: 'fretchase', name: 'Walk Up', values: {} },
  { defId: 'standingwave', name: 'Physical', values: { space: 0 } },
  { defId: 'standingwave', name: 'Per Fret', values: { space: 1, freq: 3 } },
  { defId: 'sparkle', name: 'Embers', values: { hue: 20, density: 0.04, decay: 0.95, jitter: 40 } },
  { defId: 'sides', name: 'Wiring Check', values: { taper: 1 } },
];
