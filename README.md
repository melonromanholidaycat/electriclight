# electriclight

A Strat with two addressable LED strips down the neck, rebuilt around an
ESP32-S3 so effects can be designed, edited and deployed over WiFi. See
[`AGENTS.md`](AGENTS.md) for the project brief and the constraints behind it.

## Where this is

**Step 1 of 7: the simulator.** Everything here runs in a browser today. There
is no firmware yet. The guitar has been measured but not opened.

```
web/src/lang/     the effect language: tokeniser, parser, compiler, bytecode evaluator
web/src/model/    neck geometry, render engine, effect library
web/src/ui/       canvas neck, knob, five-way switch
web/build.js      inlines it all into one self-contained page
web/test/         unit tests, golden vectors, browser smoke test
docs/             specification, decisions, firmware plan, hardware photographs
```

## The documents

| | |
|---|---|
| [`AGENTS.md`](AGENTS.md) | the brief: constraints that drive every decision. Read first. |
| [`docs/effect-format.md`](docs/effect-format.md) | what an effect is — the contract the firmware is held to |
| [`docs/decisions.md`](docs/decisions.md) | why it is that way, what was rejected, and what would reopen it |
| [`docs/firmware-plan.md`](docs/firmware-plan.md) | what must be true before and during the cabled session |
| [`docs/hardware/`](docs/hardware/) | photographs and every measured number |

`CLAUDE.md` points at `AGENTS.md`; they are not two documents.

## Running it

```sh
node web/build.js      # -> dist/index.html, one self-contained file
node web/test/run.js   # language, engine and golden-vector tests, no dependencies
```

The smoke test needs Chromium:

```sh
npm ci && npx playwright install chromium
node web/test/smoke.mjs
```

CI runs all three on every push. On the default branch it also publishes
`dist/index.html` to GitHub Pages, enabling Pages itself on the first run.

## The two contexts

`dist/index.html` is a single file with no external requests. Published to Pages
it is a simulator; served by the guitar it is the live control surface. It works
out which by asking for `api/status` at start-up and falling back to simulator
when nothing answers.

It is never forked. The simulator drifting away from the device is the main
failure this project is designed against, which is also why effects are data
rather than code, and why the golden vectors exist.

## The effect format

An effect is a few formulas evaluated per pixel, per frame, against fret
position, physical position, time and the previous frame, with `warp()` to
squeeze or stretch a pattern anywhere along the neck. It compiles in the
browser to 70–110 bytes of bytecode; the firmware only ever runs the bytecode.

Definitions are maths. Presets are named sets of slider values, grouped under
the definition they came from. The five-way switch picks one of five slots; the
knob is master brightness unless a preset says otherwise. The guitar stays fully
playable with no phone present.

[`docs/effect-format.md`](docs/effect-format.md) is the specification the
firmware will be written against.

## The guitar

21 frets on a 648 mm scale. Two separate 26-LED strips on the face of the
fretboard, 27 mm apart, fed from the body end — so LED 0 sits at the last fret.
20 mm clear of the nut, 20 mm short of the last fret, which works out to a
16.61 mm pitch: a standard 60/m tape, and only consistent with a 21-fret neck.
52 LEDs, 3.1 A at full white.

[`docs/hardware/`](docs/hardware/) has the photographs and the arithmetic.

## Next

2. CI plus a minimal firmware that boots, serves the page and takes an OTA update
3. Safe mode, remote logging, network fallback, recovery from a bad image
4. The bytecode evaluator in C++, checked against `web/test/vectors.json`
5. Live control: the page detects the device, pushes and stores effects
6. First cabled session — validate power and wiring with known-good firmware first
7. Effect design, remotely, from then on

[`docs/firmware-plan.md`](docs/firmware-plan.md) is what steps 2–6 have to get
right, including the handful of things a cable is needed to change.
