# LED Guitar — project brief

A Strat-style guitar with two addressable LED strips running the length of the
fretboard, roughly one LED per fret on each side. Currently driven by an Arduino
Nano in the body cavity, powered by 6×AA. Being rebuilt around an ESP32-S3 so
effects can be designed, edited and deployed wirelessly.

---

## Working relationship

The owner directs this project; he does not want to write or hand-edit code.
Treat implementation as yours to own. Bring him decisions, trade-offs and things
that need a physical check — not diffs to review line by line.

He is German and prefers English for technical work. He wants the reasoning behind
a recommendation, not just the recommendation. Push back when something is wrong,
and don't leave superseded advice quietly standing.

---

## Constraints that drive every decision

**He has no computer — an iPhone only.** No local toolchain, no serial monitor,
no USB flashing. Builds run in CI, hosting is GitHub Pages, repo access from the
phone via a mobile git client.

**iOS Safari limits, non-negotiable:**

- No Web Bluetooth. WiFi is the only phone↔guitar transport.
- An HTTPS page cannot talk to a local HTTP device. So the control UI **must be
  served by the guitar itself**. The Pages copy can only ever be a simulator.
- File uploads from the Files app work, so manual firmware upload from the phone
  is a viable fallback.

**Cabled flashing sessions are scarce, not singular.** More are possible if needed,
but each one depends on borrowing hardware at an unknown date. Design so that one
is enough, and so that a mistake costs a delay rather than the project.

**The guiding rule:** anything that can't be changed over WiFi is effectively
permanent. Check every proposal against that.

---

## Hardware

**Confirmed:** ESP32-S3 as the new controller, bought in threes so a brick is a
board swap. Two LED strips in the neck. 6×AA in a rear cavity.

Both strips run along the fretboard itself, roughly 10 mm either side of the
centre line — not along the edges. That puts them only about 20 mm apart, so the
neck reads mainly as a single line of light and per-side effects are a subtle
split rather than two separate runs.

**Chosen and worth preserving:** S3 over the cheaper C3 because the C3 lacks an
FPU and is single-core, which would foreclose future signal-processing work. Costs
about 5% more system draw. Accepted.

**Needs changing during the rebuild:** a buck converter in place of the current
linear regulation (the present setup almost certainly caps achievable brightness);
level shifting on the LED data line; proper decoupling, since radio transmission
spikes can brown out the board; NiMH cells instead of alkalines, which sag badly
under load. Battery voltage sensing is cheap to add and worth having.

**Not yet verified — he will open the guitar and report.** Do not design around
assumptions here; ask:

- LED strip type. WS2812B is suspected but unconfirmed.
- Exact LED count per strip, and the spacing (roughly per-fret, not exactly).
- What currently regulates the battery voltage down.
- Existing wiring and connectors.
- Flash size on the ESP32-S3 boards, which fixes the partition layout and is
  one of the few things a cable is needed to change.

**Existing physical controls, both currently wired to the Nano and both worth
keeping:**

- A potentiometer controlling brightness, read digitally — so it can be remapped
  to anything.
- A five-way switch used to select between effects.

These matter more than they look. They mean **the guitar has to stay fully usable
with no phone present** — on stage, at rehearsal, with a flat phone battery. The
web UI is for designing and loading effects; the knob and switch are for playing.
Treat that as a design requirement, and treat both controls as remappable inputs
that the effect system can read.

**Deferred: audio-reactive lighting.** He has not committed to it. Do not build it.
Reserve a couple of pins and some physical space so a digital microphone could be
added in a later session — that is the whole cost of keeping the door open.

---

## Architecture

One repository holds the web UI, the firmware, and the CI pipeline.

**One web page, two contexts.** The same file is published to Pages as a standalone
simulator and embedded in the firmware as the live control UI, detecting at runtime
which it is. Never fork it — the sim drifting from the device is the main failure
mode to design against.

**Effects are data, not code.** The firmware carries an interpreter; an effect is a
small description it evaluates locally, per pixel, every frame. Nothing pixel-level
crosses the network. This is why adding an effect never means reflashing, and it is
the core idea the whole project rests on.

**Effects address the fretboard, not a strip.** Position should be expressed in
terms of fret and side, not a flat LED index. That's what makes neck-aware effects
possible, and it should survive every refactor.

**The effect format is settled — see [`docs/effect-format.md`](docs/effect-format.md).**
An effect is a few formulas evaluated per pixel against position and time, plus
named, typed, range-bounded parameters. It compiles in the browser to bytecode;
the firmware runs a fixed stack machine and never parses source. The format was
chosen to bound how much has to be implemented twice, since simulator/device
drift is the failure this project is built against.

Still open, to think through before they get locked in: whether effects need
layering or segments beyond the reserved data shape; how much further the
interpreter should grow. Propose options with trade-offs. Both the simulator and
the firmware are written against these choices, so changing one later means
changing both.

---

## What the firmware must guarantee

These exist because the device becomes hard to reach once the guitar is closed:

- Wireless firmware updates, with automatic recovery from a bad image.
- A minimal safe mode reachable at boot that ignores stored configuration.
- Remote logging. There is no serial monitor, so without this, diagnosis is
  guesswork on a very slow loop.
- Network fallback, so the guitar is always reachable even away from known WiFi.
- Radio off unless deliberately enabled — saves power and stops anyone connecting
  mid-set. Safe mode must override this, or a bad config makes the guitar
  unreachable from a phone.
- A brightness ceiling enforced in software, tunable remotely. This is the most
  effective runtime control available, far more so than any hardware choice.
- Refusal, with a clear message, of any effect using an opcode the firmware does
  not know — never silent misbehaviour.

Anything tied to physical wiring that might need tuning later should be exposed as
a remotely adjustable setting rather than a compile-time constant.

---

## Sequence

1. ~~**Start here: the simulator.**~~ Done. A single static page, published to
   Pages, drawing the neck and both strips and running effects live in the
   browser. Fret spacing follows the real geometry. The brightness knob and
   five-way switch are modelled in the UI, so the standalone playing experience
   is designed rather than retrofitted.
2. CI pipeline plus a minimal firmware that boots, serves a page and accepts a
   wireless update. Green in CI before hardware arrives.
3. The survival features above.
4. Bring the effect interpreter to the firmware, matching the simulator exactly.
   `web/test/vectors.json` is the contract it must reproduce.
5. Live control: the page detects it's on-device, pushes and stores effects.
6. First cabled session. Validate power, wiring and LED behaviour with known-good
   third-party firmware before trusting custom code, then flash all boards.
7. Effect design, remotely, from then on.
