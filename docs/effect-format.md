# Effect format v1

This is the contract. The simulator and the firmware are two implementations of
it, and the golden vectors in `web/test/vectors.json` are what proves they agree.

## Why it is shaped this way

The failure mode this project is most exposed to is the simulator drifting away
from the device. So the thing to minimise is not how expressive effects can be,
it is **how much has to be implemented twice**.

A catalogue of built-in effect kernels grows that surface forever: every new
effect is another kernel to write in C++ and again in JavaScript, and every one
is another chance for the two to disagree. An expression language does not. The
parser and compiler live only in the browser; the firmware receives bytecode and
runs a stack machine over a fixed set of about thirty opcodes. That surface is
**bounded and constant**: it does not grow when effects are added.

The other consequence is that adding an effect never means reflashing. Firmware
updates do go over the air, so that is a convenience rather than a necessity —
but a convenience measured in seconds instead of a CI build, an OTA push and the
risk that comes with one.

## Two things, not one

**A definition** is the maths plus a parameter schema. Written in the language
below. Changing one is programming.

**A preset** is a name and a set of values for that definition's parameters.
Made by dragging sliders. Presets are grouped under their definition in the
library, and a preset is what a switch position points at.

This split is why parameters are named, typed and range-bounded rather than
generic: the schema is the entire interface between someone who writes maths and
someone who is playing a guitar.

## The five-way and the knob

The switch selects one of five **slots** directly. It is not an input to the
effect. Predictable beats clever when you are mid-set, and "what is position 3?"
should have a one-word answer.

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

**`fret` and `u` are both here on purpose.** Fret spacing is geometric —
`d(n) = L·(1 − 2^(−n/12))` — so a wave travelling at constant speed in `u` and
the same wave stepping through `fret` are completely different effects. Offering
only one of them would silently foreclose half of what a neck can do. The
*Standing Wave* effect has a slider that crossfades between them, which is the
quickest way to see the difference.

`prev` is what makes trails, decay and fire possible. Without it an effect is a
pure function of position and time and cannot remember anything.

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

It knows nothing about frets. That is the point: squeezing part of the neck is
useful whether or not the LEDs line up with anything, so it is a plain
coordinate warp and the effect decides what to feed it — physical position, fret
position, or a crossfade of the two.

**Why it lives in effects rather than in the engine.** A global warp applied to
`u` before effects saw it would be free for every effect and tunable in one
place. It was not done that way because it would blur two different things: how
the LEDs are actually spaced (calibration, measured once the guitar is open) and
how squashed you want a pattern to look (artistic, per preset). With them
separate, a neck that looks wrong is unambiguously one or the other. This is
worth revisiting once the real LED positions are known.

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

Effects can read their own previous frame, so a variable frame rate would make
trails behave differently on the device than in the simulator. Both step whole
1/60 s frames. The simulator drops or repeats frames rather than scaling time.

## The output chain

Applied after the effect, and unreachable from inside it:

```
effect v -> knob (if bound to brightness) -> gamma -> brightness ceiling -> current limit -> 8-bit
```

The knob goes in **before** gamma because a player turning it down wants a
perceptual dim. The ceiling goes in **after**, because it is a power control:
half the ceiling has to mean half the current, which is only true on the linear
side of gamma. The current limiter then scales the whole frame to fit the
configured budget. An effect can therefore never brown out the board or cook the
pack, whatever it computes.

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

The shape is reserved now because it is the one part of the format that is
genuinely expensive to retrofit: the simulator, the firmware and the storage
format are all written against it. Adding a second layer later is then a UI
change. `normal`, `add` and `max` blends and mask evaluation are already
implemented and tested, so the path is real rather than theoretical.

## Changing any of this

`FORMAT_VERSION` in `web/src/lang/ops.js` gates the wire format. Bump it when
opcodes, semantics or the binary layout change, then regenerate the golden
vectors with `npm run vectors` **and read the diff** — a vector that changes
without you intending it to is the firmware and the simulator about to disagree.
