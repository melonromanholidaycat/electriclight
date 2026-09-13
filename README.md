# electriclight

A Strat with two addressable LED strips down the neck, rebuilt around an
ESP32-S3 so effects can be designed, edited and deployed over WiFi. See
[`CLAUDE.md`](CLAUDE.md) for the project brief and the constraints behind it.

## Where this is

**Step 1 of 7: the simulator.** Everything here runs in a browser today. There
is no firmware yet and no hardware has been opened.

```
web/src/lang/     the effect language: tokeniser, parser, compiler, bytecode evaluator
web/src/model/    neck geometry, render engine, effect library
web/src/ui/       canvas neck, knob, five-way switch
web/test/         unit tests, golden vectors, browser smoke test
docs/             the effect format specification
```

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
`dist/index.html` to GitHub Pages.

**One manual step, once:** in the repository settings, under Pages, set the
source to *GitHub Actions*. Nothing publishes until that is done.

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
position, physical position, time and the previous frame. It compiles in the
browser to 70–110 bytes of bytecode; the firmware only ever runs the bytecode.

Definitions are maths. Presets are named sets of slider values, grouped under
the definition they came from. The five-way switch picks one of five slots; the
knob is master brightness unless a preset says otherwise. The guitar stays fully
playable with no phone present.

[`docs/effect-format.md`](docs/effect-format.md) is the specification the
firmware will be written against.

## Next

2. CI plus a minimal firmware that boots, serves the page and takes an OTA update
3. Safe mode, remote logging, network fallback, recovery from a bad image
4. The bytecode evaluator in C++, checked against `web/test/vectors.json`
5. Live control: the page detects the device, pushes and stores effects
6. First cabled session — validate power and wiring with known-good firmware first
7. Effect design, remotely, from then on
