# Decisions

Why things are the way they are, and what was rejected on the way. The point of
this file is that nobody has to re-litigate a settled choice from scratch — and
that when one *should* be reopened, the trigger for doing so is written down.

[`effect-format.md`](effect-format.md) is the specification. This is the
reasoning behind it.

---

## The effect format is an expression language, not a catalogue of effects

**Chosen:** an effect is a few formulas evaluated per pixel, compiled in the
browser to ~30 opcodes of bytecode that the firmware executes.

**Rejected:**

- *A catalogue of built-in kernels* (`{type:"comet", speed:1.2}`). Smallest
  interpreter and the nicest UI, but every new kind of effect is another kernel
  written twice, in C++ and in JavaScript. This is the WLED model, and it is why
  WLED is an enormous C++ codebase.
- *A layered compositor* — generators, blend modes, masks. Genuinely pleasant to
  use and it answers the layering question directly, but the generator set still
  lives in firmware, so it has the catalogue's ceiling with a much larger UI to
  build. Its data shape is reserved; see below.
- *A hybrid* of the two. Best ceiling, most work, two mental models, and the
  real risk that the simulator never ships.
- *Real code — Lua or WASM.* You would still need an authoring language to
  produce it from a phone, so you end up designing the expression language
  anyway, having first paid for a sizable runtime and lost float determinism.

**Why:** the metric that matters is not expressiveness, it is **parity surface** —
how much has to be implemented twice and stay bit-compatible forever. The brief
names simulator/device drift as the main failure mode. Every rejected option's
parity surface grows as effects are added. This one's does not: one evaluator,
~30 opcodes, constant.

The secondary benefit is that adding an effect never means reflashing. Firmware
updates do go over the air, so that is convenience rather than necessity — but
convenience measured in seconds instead of a CI build, an OTA push and the risk
that comes with one.

**What would reopen it:** if authoring expressions turns out to be the wrong
interface even for someone comfortable with maths, the catalogue model is the
fallback — but see the definition/preset split, which exists to make that
unnecessary.

## The compiler lives in the browser; only the evaluator ships

**Why:** it is what keeps the parity surface small. A parser is the fiddly part,
and this way it is written once. An evaluator is mechanical and testable against
golden vectors, which a parser is not.

## Definitions and presets are different things

A **definition** is the maths plus a parameter schema. A **preset** is a named
set of values for it.

The brief says the owner does not want to write or hand-edit code, and also that
effects must be data so that adding one never means reflashing. Those pull
against each other only if "effect" is one thing. Split in two, definitions are
authored in maths and shipped as data with no reflash, and presets are made by
dragging sliders on a phone. This is also the answer to "how many parameters,
and should they be named and typed" — **named, typed and range-bounded, always**,
because the schema is the entire interface between the maths and the playing.

Presets are grouped under their definition in the library, at the owner's
request.

## The five-way selects slots; it is not an input to effects

**Rejected:** feeding the switch position to effects as an input, which is more
flexible and would have needed no slot machinery.

**Why:** predictable beats clever mid-set. "What is position 3?" should have a
one-word answer, and the guitar has to stay fully playable with no phone. `sw`
is still readable from inside an effect for anyone who wants it; nothing in the
stock library does.

## Effects get both fret position and physical position

`fret` is the fractional fret number; `u` is normalised physical distance.

Fret spacing is geometric, `d(n) = L·(1 − 2^(−n/12))`. LED spacing is a fixed
tape pitch. **These are genuinely different coordinate systems on this
instrument** — confirmed once the strip turned out to be ordinary tape — so a wave
travelling at constant speed in `u` and the same wave stepping through `fret`
are different effects. Offering only one would silently foreclose half of what a
neck can do.

`warp()` came later, at the owner's suggestion, to squeeze or stretch a pattern
anywhere along the neck without reference to frets at all.

## warp lives inside effects, not in the engine

A global warp applied to `u` before effects saw it would be free for every
effect and tunable in one place.

**Why not:** it would blur two different things — how the LEDs are actually
spaced (calibration, measurable) and how squashed a pattern should look
(artistic, per preset). Kept apart, a neck that looks wrong is unambiguously one
or the other.

**What would reopen it:** if in practice every effect wants the same squeeze,
that is evidence the warp belongs in the instrument rather than the effect.

## Effects can read one frame of their own history

**Why:** without it an effect is a pure function of position and time, which
rules out trails, decay and fire entirely. One float per pixel buys back that
whole class. The cost is that the frame rate then has to be fixed — see below.

## The effect clock runs at a fixed rate on both sides

Because `prev` exists, a variable frame rate would make every trail behave
differently on the device than in the simulator — exactly the drift this project
is designed against. Both step whole frames at the rate fixed in
[`effect-format.md`](effect-format.md); the simulator drops or repeats frames
rather than scaling time. It is also what makes golden vectors possible at all.

## Arithmetic is pinned, not left to the platform

float32 everywhere; division or modulo by zero yields 0; non-finite results of
divisions and calls collapse to 0; `mod` is GLSL-style. A NaN pixel is far worse
than a wrong one, and the two evaluators have to agree.

*Correction, step 4:* an earlier note here said float32 is free on the S3
because it has an FPU. The S3's FPU is **single precision only**, and that turns
out to matter, because the firmware evaluator computes in double wherever the
JavaScript does — see the next decision. Those doubles are emulated in software.
Measured cost: the full golden-vector run takes about 34 ms on a desktop, and
the heaviest single frame is well inside the 16.7 ms a 60 Hz frame allows even
after scaling for the S3. The FPU is still the reason to prefer the S3 over the
C3; it is just not the whole story.

## The firmware evaluator is a literal port, not a reimplementation

`el_eval.c` reproduces `eval.js` operation by operation: double where the
JavaScript is double, float32 where it rounds, `uint32_t` where it uses bitwise
operators, `floor(x + 0.5)` where it calls `Math.round`. It is slower and uglier
than a natural C evaluator would be.

**Why, when the output is only 8 bits per channel:** because the golden vectors
cannot tell the difference. Replacing every transcendental with its
single-precision version changes **nothing** in all 104 golden frames, though
`sinf` and `(float)sin` disagree on about 1.3% of inputs — two roundings, to
float32 and then to 8 bits, absorb it.

So the vectors prove agreement on 104 frames; only the literal port gives
equivalence. Equivalence is what lets the owner trust the simulator about an
effect nobody has ever rendered on the guitar, which is the entire point of
having a simulator. The same measurement says the residual risk from the host's
libm and newlib's disagreeing in the last double bit is far smaller still.

**What would reopen it:** a per-frame budget problem on real hardware. The fix
then is to do less work per frame, not to quietly drop to float — that trades
away the property the project is built on.

## The guitar checks itself, and reports rather than decides

The golden vectors are compiled into the firmware (about 21 kB including the
code). `GET /api/selftest` runs them on demand; a fresh image runs them once,
after it has already confirmed itself.

**Why on the device at all**, when CI runs the same check: CI cannot see a
miscompile at a different optimisation level, a half-written OTA, flash that has
started to rot, or a build where the generated vectors and the evaluator came
from different commits. The guitar has no serial console, so without this the
only way to ask whether the firmware still renders what the browser drew is to
notice that a gig looked wrong.

**Why it does not gate the rollback decision:** a self-test that crashed would
turn one wrong pixel into a boot loop, and a slow one would delay confirming an
image that is working fine. What to do about a failure is a decision made on a
phone, not in the dark. It runs only on a new image because an unchanged
firmware has an unchanged answer, and the answer costs seconds of emulated
double arithmetic.

## The knob goes in before gamma, the ceiling after

`effect → knob → gamma → brightness ceiling → current limit → 8-bit`.

A player turning the knob down wants a *perceptual* dim, which is the pre-gamma
side. The ceiling is a *power* control: half the ceiling must mean half the
current, which is only true on the linear side. None of it is reachable from an
effect, so no effect can brown out the board or cook the pack.

## Presets store a list of layers that nothing yet creates

Every preset holds `layers: [...]` with exactly one entry, each with a mask and
a blend mode. Masks and the `normal`/`add`/`max` blends are implemented and
tested.

Layering is the one part of the format that is expensive to retrofit — the
simulator, the firmware and the storage format are all written against the
top-level shape. Reserving it now is nearly free; adding a second layer later
becomes a UI change. Still open: whether layering is wanted at all.

## Even LED spacing is the default mapping

**Why:** the strips are continuous commercial tape, so the pitch is fixed in
millimetres and physically cannot track frets. This was assumed the other way
round at first, on the strength of the brief's opening line, and the photographs
corrected it.

`fret-midpoint` mapping stays available in case a strip is ever cut and re-spaced
by hand, which is the only way the other mapping could become true.

## Geometry is expressed in units you can measure

Strip spacing is centre-to-centre in millimetres, because that is what a ruler
gives — not an offset from a centre line, and not a dimensionless inset. Two
earlier parameterisations were replaced for exactly this reason. The Setup page
derives the implied LED density and says so, so a mismeasurement announces
itself rather than quietly skewing every effect.

## The page is one self-contained file

Sources are ES modules so node can unit-test them; `web/build.js` inlines them
in a hand-maintained order into a single HTML file with no external requests.

One artefact means the Pages copy and the on-device copy cannot be different
builds of different pieces, and the firmware embeds one gzipped blob rather than
serving a tree. The module order being hand-maintained is the cost; the build
fails loudly if it is wrong.

## Golden vectors are the contract, not a regression test

`web/test/vectors.json` holds the exact 8-bit output of every built-in effect at
chosen frames, plus two synthetic cases: one that exercises the arithmetic edges
and one that is simply full white. The firmware evaluator is held to it: base64
bytecode in, identical pixels out. Its geometry is pinned in full rather than
spread from the defaults, so the contract cannot drift when someone adjusts a
default.

Every case runs under **two output profiles**. `unclipped` opens the brightness
ceiling and sets an effectively infinite current budget, so a disagreement can
only come from the maths. `stage` uses a real ceiling and a supply that cannot
deliver full white.

The second profile exists because the first one, on its own, passed a firmware
build with the brightness ceiling applied on the *wrong side of gamma* — the
ordering decided two sections above, and the one that governs how much current
the pack actually delivers. With the ceiling pinned at 1, that ordering is a
no-op and nothing noticed. The generator now refuses to write a vector set in
which no case trips the current limiter.

Regenerating it is `npm run vectors`, and the diff must be read. A vector that
changes unintentionally is the firmware and the simulator about to disagree.

## The radio is enabled by a gesture, not a switch position

**Chosen:** sweeping the five-way from one end to the other within a few seconds
of switching on. Mechanics in [`firmware-plan.md`](firmware-plan.md).

**Rejected:**

- *A nominated five-way position at switch-on.* Simplest to build and simplest
  to explain, and useless: that is wherever the switch was last left.
- *Brightness at minimum at switch-on.* Nobody sets that deliberately, and a
  dark neck would be its own feedback — but it is exactly where the knob gets
  left after a session, so the radio would come up on its own eventually.
- *Both conditions together.* Two coincidences rather than one, and good enough
  in practice. Rejected only because the gesture costs no more to build.

**Why:** the brief wants the radio off so that nobody can connect mid-set, and
every position-based trigger fails the same way — positions persist, so sooner
or later the instrument powers up already holding the combination. A gesture has
no resting state to leave it in.

The cost is discoverability: a gesture cannot be worked out by looking at the
instrument. That is acceptable for a control used by one person who has it
written down, and unacceptable for anything else — which is why the same
reasoning should not be reached for again without arguing it afresh.

**What would reopen it:** anyone other than the owner needing to get the guitar
online.

## The firmware is built on ESP-IDF

**Chosen:** Espressif's own SDK, native.

**Rejected:** *Arduino-ESP32*, which is faster to write and has the larger
library ecosystem, and *Arduino as an ESP-IDF component*, which offers both at
the cost of two sets of conventions in one codebase.

**Why:** almost everything this firmware has to do well is a survival feature —
OTA with automatic rollback, a safe mode that ignores stored configuration,
partition control, remote logging, deliberate radio control. Under ESP-IDF each
of those is a supported platform API; under Arduino most are something bolted on
top. The owner cannot attach a debugger or read a serial port, so "the platform
does this properly" is worth more than "this is quick to write", and the writing
is my time rather than his. Reproducible CI in the official Docker image is a
secondary benefit.

**What would reopen it:** if someone else ends up maintaining the firmware and
knows Arduino rather than IDF, the hybrid is the escape hatch and does not
require starting over.

## The guitar is called electriclight on the network

`electriclight.local` over mDNS on a known network, and `electriclight` as the
fallback access point. The mDNS name is a remotely editable setting; the AP name
ships in the image.

## The fallback AP password is in the repository, and is therefore not a secret

**Chosen:** a known password, committed, documented, and treated as a deterrent
rather than a defence.

**Rejected:** injecting it at build time from a repository secret, which would
keep it out of git. GitHub secrets cannot be read back, so the owner could not
recover it — and this AP is the last way back when stored configuration has gone
bad.

**Why:** losing access to the guitar is a far worse outcome than a stranger
connecting to it. The AP only exists when the guitar cannot reach a known
network, the attacker has to be in radio range, and the worst they can do is
change the lights. Recoverability beats secrecy at this threat model.

**What would reopen it:** the repository going private, which makes the
committed password meaningfully secret at no cost — or the guitar being used
somewhere the lights genuinely matter.

## CI enables Pages itself

`configure-pages` is given `enablement: true` so the first run turns Pages on
over the API. The owner has a phone and no computer; every settings page he does
not have to find is worth the line of YAML.

## The trunk is not called main

The repository was empty when the first branch was pushed, so GitHub made
`claude/led-guitar-brief-wrjr81` the default branch. CI keys its deploy off the
repository's actual default branch rather than a hardcoded name, so renaming it
in settings would need no code change.
