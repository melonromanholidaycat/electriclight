# electriclight

A Strat with two addressable LED strips down the neck, rebuilt around an
ESP32-S3 so effects can be designed, edited and deployed over WiFi. See
[`AGENTS.md`](AGENTS.md) for the project brief and the constraints behind it.

## Where this is

**Steps 1 to 5 of 7.** The simulator runs in a browser today. The firmware
boots, serves that same page, accepts an update over the air and survives a bad
one, and carries the survival features — the gesture that brings the radio up,
safe mode, boot-loop rescue, a log that outlives a crash. It now also renders
effects: the same evaluator as the browser, reproducing every golden frame byte
for byte, and carrying those vectors so the guitar can re-run the check on
itself over WiFi.

**It has run on hardware.** Boards are flashed from a web page; the firmware
boots, brings up its own access point, serves the control page, accepts an
update over the air, and reproduces all 104 golden frames on the device itself.
It renders at 60 Hz on its own core and drives WS2812 strips, so a bare board on
a desk lights its own LED without a guitar attached.

Nothing has been connected to a guitar yet.

```
web/src/lang/     the effect language: tokeniser, parser, compiler, bytecode evaluator
web/src/model/    neck geometry, render engine, effect library
web/src/ui/       canvas neck, knob, five-way switch, the device panel
web/build.js      inlines it all into one self-contained page
web/flasher/      the web flasher: installs firmware over USB from a browser tab
web/test/         unit tests, golden vectors, browser smoke test
firmware/         ESP-IDF project: boots, serves the page, takes an OTA update
firmware/components/core   the decision logic and the effect evaluator, plain C
firmware/test_host/        those tests, run natively — ./firmware/test_host/run.sh
docs/             specification, decisions, firmware plan, hardware photographs
```

## Live

| | |
|---|---|
| Simulator | https://melonromanholidaycat.github.io/electriclight/ |
| Flasher — needs desktop Chrome, Edge or Opera | https://melonromanholidaycat.github.io/electriclight/flash.html |
| Firmware image | https://melonromanholidaycat.github.io/electriclight/firmware/4mb/electriclight.bin |
| The guitar | `http://192.168.4.1/` on its own access point, or `http://electriclight.local/` once it has joined a network |

The access point is `electriclight`, password `electric-light`, committed on
purpose — [`docs/decisions.md`](docs/decisions.md).

## The documents

| | authoritative for |
|---|---|
| [`AGENTS.md`](AGENTS.md) | the brief: constraints that drive every decision. Read first. |
| [`HANDOFF.md`](HANDOFF.md) | where things stand today, and what is waiting on whom |
| [`docs/hardware/`](docs/hardware/) | photographs, and every measured number |
| [`docs/effect-format.md`](docs/effect-format.md) | what an effect is — the contract the firmware is held to |
| [`docs/decisions.md`](docs/decisions.md) | why it is that way, what was rejected, and what would reopen it |
| [`docs/firmware-plan.md`](docs/firmware-plan.md) | electrical consequences, and what the cabled session must get right |
| [`docs/shopping-list.md`](docs/shopping-list.md) | every part to buy, indexed back to the reasoning |

Each fact has exactly one home; the others link to it rather than restating it.
`CLAUDE.md` is a pointer to `AGENTS.md`, not a second document.

## Running it

```sh
node web/build.js      # -> dist/index.html, and the firmware's generated files
node web/test/run.js   # language, engine and golden-vector tests, no dependencies
firmware/test_host/run.sh   # the firmware's C, compiled and run on this machine
```

`firmware/test_host/run.sh` needs `node web/build.js` to have run first: the
golden vectors are generated into C there, and both the host tests and the
firmware compile them.

All three must pass before anything is called done, and **the exit code of a
pipeline is the last command's** — a commit once went out with a red test
because the run was piped through `tail`. The firmware cannot be compiled here;
the ESP-IDF build in CI is the only check on it.

The smoke test needs Chromium:

```sh
npm ci && npx playwright install chromium
node web/test/smoke.mjs
```

The firmware needs ESP-IDF v5.4 and the generated page:

```sh
node web/build.js                    # writes firmware/main/www/index.html.gz
cd firmware && idf.py set-target esp32s3 && idf.py build           # 4 MB module
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.16mb" build   # 16 MB module
```

CI builds both, so the day the firmware stops fitting a 4 MB module is the day
CI says so rather than the cabled session.

CI runs all three on every push. On the default branch it also publishes
`dist/index.html` to GitHub Pages, enabling Pages itself on the first run.

## Flashing

`flash.html` is published to Pages beside the simulator. It installs the firmware
onto a board over USB from Chrome, Edge or Opera on a desktop — no toolchain, no
driver, nothing installed on the machine. Safari, Firefox and every browser on
iOS lack Web Serial and cannot flash; the page says so rather than offering a
button that fails.

That is the only step in the whole project that needs a computer. Everything
after it happens over WiFi.

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

A Strat with two separate strips of ordinary addressable tape stuck to the face
of the fretboard, both fed from the body end — so LED 0 sits at the last fret,
not the nut. Being commercial tape, the LEDs are evenly spaced in millimetres
and do not line up with frets.

[`docs/hardware/`](docs/hardware/) holds the photographs, every measured number,
and the arithmetic that cross-checks them. It is the only place those numbers
live; the simulator's defaults are held to it by a test.

## Next

Firmware: boot, serve the page, take an update over the air, survive a bad one,
then the effect evaluator in C++ checked against `web/test/vectors.json`, then
live control from the page. Hardware last, because a cabled session is scarce
and everything before it can be proven green in CI without one.

The numbered sequence is in [`AGENTS.md`](AGENTS.md); what those steps have to
get right is [`docs/firmware-plan.md`](docs/firmware-plan.md).
