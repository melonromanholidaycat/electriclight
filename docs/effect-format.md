# Effect format v1

This is the contract. The simulator and the firmware are two implementations of
it, and the golden vectors in `web/test/vectors.json` are what proves they agree.

## Why it is shaped this way

In one line: the choice was driven by how much has to be implemented twice and
stay bit-compatible forever, not by how expressive effects could be. The
alternatives that lost, and what would reopen any of this, are in
[`decisions.md`](decisions.md).

## Two things, not one

**A definition** is the maths plus a parameter schema, written in the language
below. Parameters are named, typed and range-bounded; values are clamped to
their declared range on the way in.

**A preset** is a name and a set of values for one definition's parameters.
Presets are grouped under their definition in the library, and a preset is what
a switch position points at.

## The five-way and the knob

The switch selects one of five **slots** directly. It is not an input to the
effect.

Each preset says what the knob does: master brightness by default, or any one of
that effect's named parameters. `knob` and `sw` are also readable from inside an
effect, but nothing in the stock library leans on that.

## The language

One statement per line. A line continues automatically while its brackets are
open. `#` starts a comment.

```
param speed 0.05..4 = 0.6 "Speed" step:0.01

head = fract(t * speed)
d    = abs(u - head)
v    = gauss(min(d, 1 - d), 0.06)
h    = 190
s    = 1
```

Assign `h` (hue in degrees, wrapped), `s` and `v` (both clamped to 0..1). They
start at `0`, `1`, `0`, so a half-written effect still runs. Locals are created
by assignment and may be reassigned.

`param name min..max = default "Label" [step:n]` declares a knob. The default
must lie inside the range. Values are clamped to the range on the way in, so a
preset can never push an effect somewhere it was not designed to go.

### Per-pixel inputs

| name | meaning |
|---|---|
| `t` | seconds, on the fixed 60 Hz clock |
| `fret` | fractional fret number, 0 at the nut |
| `u` | normalised physical distance along the lit span, 0..1 |
| `side` | 0 bass, 1 treble |
| `n` | LED index on its own strip, 0-based |
| `count` | LEDs per strip |
| `nfrets` | frets on the neck |
| `knob` | the potentiometer, 0..1 |
| `sw` | the five-way, 0..4 |
| `prev` | this pixel's `v` on the previous frame |
| `rnd` | a fixed random per pixel, 0..1 |

`fret` and `u` are separate because fret spacing is geometric —
`d(n) = L·(1 − 2^(−n/12))` — while LED spacing is a fixed tape pitch, so a wave
travelling at constant speed in `u` and the same wave stepping through `fret`
are different effects. The *Standing Wave* effect crossfades between them, which
is the quickest way to see it.

`prev` is the only state an effect has: one frame, one float per pixel, and it
is why the clock rate is fixed rather than free-running.

### Operators

`+ - * / %`, comparisons `< <= > >= == !=`, logic `&& || !`, and `cond ? a : b`.
Comparisons and logic yield 0 or 1. Both branches of `?:` are evaluated.

### Functions

`abs min max clamp floor ceil round fract mod sign sqrt pow exp log sin cos tan
atan2 step smoothstep mix sat tri gauss hash noise warp`

Constants: `PI`, `TAU`, `E`.

- `clamp(x, lo, hi)`, `mix(a, b, t)`, `step(edge, x)`, `smoothstep(e0, e1, x)`
- `sat(x)` clamps to 0..1; `tri(x)` is a 0..1 triangle wave of period 1
- `gauss(d, w)` is `exp(-d²/w²)` — the shape of nearly every pulse and comet
- `hash(a, b)` is a stable pseudo-random in 0..1; `noise(x)` is smooth 1D value noise
- `warp(x, centre, amount)` — see below

### warp

`warp` remaps 0..1 onto itself, monotonically, with both ends pinned. `amount`
of 0 is the identity. Positive `amount` bunches a pattern together around
`centre` and stretches it at the ends; negative does the reverse. Inputs outside
0..1 are clamped.

It knows nothing about frets: it is a plain coordinate warp, and the effect
decides what to feed it — physical position, fret position, or a crossfade. It
belongs to the effect rather than to the engine; see
[`decisions.md`](decisions.md).

Implementation, which both evaluators must match exactly:

```
k = 2^(-amount)                       , identity when amount = 0
x < centre : centre · (1 − (1 − x/centre)^k)
x ≥ centre : centre + (1 − centre) · ((x − centre)/(1 − centre))^k
centre ≤ 0 : x^k          centre ≥ 1 : 1 − (1 − x)^k
```

### Arithmetic rules that both implementations must honour

These exist because a NaN pixel is far worse than a wrong one, and because the
two evaluators have to agree bit for bit.

1. Every value is **float32**.
2. **Division or modulo by zero yields 0**, never infinity.
3. Any **non-finite** result of a division or a function call collapses to 0.
   `sqrt` of a negative is 0; `log` of zero or less is 0.
4. `mod` is GLSL-style: `x - y·floor(x/y)`, so the sign follows the divisor.
   `mod(-1, 3)` is 2, not -1.
5. `hash` and `noise` are defined by the exact integer arithmetic in
   `web/src/lang/eval.js`, not by "some random function". Port it literally.

### The clock is fixed at 60 Hz

Both implementations step whole 1/60 s frames. The simulator drops or repeats
frames rather than scaling time; it never scales `t` to a real frame interval.
This is required because effects can read their own previous frame.

## The output chain

Applied after the effect, and unreachable from inside it:

```
effect v -> knob (if bound to brightness) -> gamma -> brightness ceiling -> current limit -> 8-bit
```

The knob goes in before gamma and the ceiling after it; the current limiter then
scales the whole frame to fit the configured budget. An effect can therefore
never brown out the board or cook the pack, whatever it computes. Why that
order: [`decisions.md`](decisions.md).

## Bytecode

Compilation happens in the browser. The firmware never parses source; it stores
the source as an opaque blob so the editor can show it again, and executes this:

```
magic  "ELFX"        4 bytes
u8     version       currently 1
u8     nLocals
u8     stack         maximum stack depth
u8     nParams
u16LE  nConsts
u16LE  codeLen
f32LE  consts[nConsts]
u8     code[codeLen]
```

The code is RPN over a float32 stack. Opcodes and their operands are listed in
`web/src/lang/ops.js`, which is the authoritative table — **indices are wire
format: append, never renumber.** In practice an effect compiles to 70–110
bytes.

Appending a function is backward compatible for decoding, but an effect that
uses a new one will not run on firmware built before it existed. So the firmware
must **reject an unknown opcode or function index by refusing the effect**, with
a message naming the index — never by running it anyway or by crashing. The page
is served by the device, so the two normally ship together; this matters when an
effect is carried over from the Pages simulator, which is always newer.

Reserved limits: 64 locals, 256 constants, 32 params, stack depth 32, 4096 bytes
of code.

## Layers

A preset stores a **list** of layers, each with a definition, values, a mask
(fret range and sides) and a blend mode. Nothing in v1 creates more than one
layer, and the UI does not expose them.

`normal`, `add` and `max` blends and mask evaluation are already implemented and
tested, so the path is real rather than theoretical. Why the shape is reserved
rather than added later: [`decisions.md`](decisions.md).

## Changing any of this

`FORMAT_VERSION` in `web/src/lang/ops.js` gates the wire format. Bump it when
opcodes, semantics or the binary layout change, then regenerate the golden
vectors with `npm run vectors` **and read the diff** — a vector that changes
without you intending it to is the firmware and the simulator about to disagree.
