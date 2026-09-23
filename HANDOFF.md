# Handoff

**State as of 2026-09-23. Branch `claude/led-guitar-brief-wrjr81`, which is the
default branch. HEAD `a1802953`.**

This is the one file in the repository that is **rewritten rather than appended
to**. Everything durable has a permanent home elsewhere and this only says where
things stand today; when it disagrees with a linked document, the linked
document wins.

Read [`AGENTS.md`](AGENTS.md) first. It is the brief, and it is what makes the
rest of the decisions make sense.

---

## Who you are working with

In [`AGENTS.md`](AGENTS.md), but the parts that change how you should behave:

- **He directs; he does not write code.** Bring decisions and trade-offs, not
  diffs. Implementation is yours to own.
- **He wants reasoning, not just recommendations.** He is German and prefers
  English.
- **Push back when something is wrong, and do not leave superseded advice
  standing.** This has mattered repeatedly — see *Things that turned out wrong*
  below, all of which he was told about explicitly.
- **He has a phone and no computer.** Everything must be reachable from an
  iPhone. A borrowed laptop is possible but rare and unpredictable.

## Where the project actually is

**Steps 1–5 of 7 are done and verified on hardware.** Step 6 is assembly, which
is where he is now. Step 7 is effect design from then on.

| | |
|---|---|
| Three ESP32-S3 boards | flashed, running the image built from `b59b155f` |
| Firmware since then | **documentation only** — the boards are functionally current |
| Connected to a guitar | **no.** Nothing has been wired yet |
| Parts | Pololu S13V30F5 converter ordered; the rest of the list is not |

Verified on the actual hardware, not just CI: boots, brings up its own access
point, serves the control page, takes an update over the air, reproduces all 104
golden frames on the silicon, renders at 60 Hz, drives its own LED, and accepts
effects pushed from the phone.

**Measured, and the number this project spent a long time guessing at:** a frame
costs **6.28 ms of 16.7 ms, 37.7%**. Consequences — layer headroom, pixel
headroom — are in [`docs/decisions.md`](docs/decisions.md).

**Not yet verified on hardware:** that pushed effects survive a power cycle. The
code writes them to NVS and reloads at boot and nobody has watched it happen.
One power cycle settles it.

## What he is waiting on, and what is waiting on him

| | |
|---|---|
| Ordered | Pololu S13V30F5. Three things to check on arrival: [`docs/shopping-list.md`](docs/shopping-list.md) |
| Still to buy | the rest of [`docs/shopping-list.md`](docs/shopping-list.md) |
| To do | restore the battery holder to six-in-series, jumpers out — it is a short hazard as it stands |
| Then | Session B in [`docs/firmware-plan.md`](docs/firmware-plan.md): assemble and validate. Needs no computer |

## Open on the code side

- **Neck geometry cannot be pushed from the page.** Output settings can. Changing
  the pixel count means re-initialising the LED driver's RMT channels, which is
  not something to do mid-upload. `/api/status` reports what the device actually
  renders and the panel flags a mismatch, so it is visible rather than silent.
  This will need doing once the real neck is measured. Reasoning in
  [`docs/decisions.md`](docs/decisions.md).
- **Layering is reserved in the format and unused.** There is room for exactly
  one more layer inside the frame budget, measured.
- **Pickup noise.** The old build put audible noise into the pickups, and the
  design for avoiding it is written up but untested. He recalls the noise
  tracking the effect, which rules out the data lines and points at supply
  current. See *Keeping the LEDs out of the pickups* in
  [`docs/firmware-plan.md`](docs/firmware-plan.md).

## How to work in here

```sh
node web/build.js            # also generates the firmware's golden vectors,
                             # default effects, flasher manifests - run it first
node web/test/run.js         # language, engine, and the cross-checks
firmware/test_host/run.sh    # the firmware's C, natively
node web/test/smoke.mjs      # needs Chromium; CHROMIUM_PATH= if not installed
```

**All three must pass, and CI must be green before telling him anything is
done.** CI builds both flash layouts and publishes the page, the flasher and the
firmware binaries to Pages. The firmware cannot be compiled locally here — the
ESP-IDF build in CI is the only check on it, so push and watch rather than
assume.

**Do not pipe a test through `tail` and trust the exit code.** A commit went out
red that way.

### The discipline that is load-bearing

**Each fact has exactly one home.** He has enforced this twice. Documents link
rather than restate; where two disagree, the linked one wins. A number written
down twice will disagree with itself within a month.

**Guards, not comments.** A surprising amount of this repo is tests that compare
one part of it against another: the firmware's geometry against the simulator's,
the built-in arity table against `ops.js`, the documented format limits against
the firmware's constants, the flasher's offsets against the partition table, the
device id the page looks for against the one the firmware reports. **When you
add a guard, break the thing it guards and confirm it fails.** Several here
passed for days while protecting nothing.

## Things that turned out wrong

Listed because they cost time, and because a fresh session will otherwise
rediscover or repeat them.

- **Estimates of firmware behaviour were wrong three times** and only measurement
  settled it. "Well inside the frame budget" (never run), then 6.9 ms inferred
  from the self-test, then 6.28 ms measured. The self-test's torture case is
  barely heavier than real effects.
- **The page is embedded in the firmware, so any control added to it is
  unreachable on every board flashed before it.** This recurs. The escapes are in
  [`docs/firmware-plan.md`](docs/firmware-plan.md).
- **Three RMT channels at two memory blocks each do not fit** on an ESP32-S3,
  which has four. The render loop failed to start and reported only `frames: 0`,
  which said nothing about why. It now reports a reason.
- **OTA alternates slots**, so `ota_0` after two updates is correct and not a
  failure.
- **A dark board reads as a failed flash.** This has been got wrong twice: once
  by shipping no default effects, once by writing `app_leds_onboard()` and never
  calling it.
- **The wire format's magic spelled `EFLX`**, because it was written as a u32.
- **The golden vectors tested the evaluator and not the output chain**, so a
  build with the brightness ceiling on the wrong side of gamma passed.
- **The buck converter spec was wrong**: a flat pack under load falls below 5 V,
  so it wanted a buck-boost, and the current rating mattered far less than the
  firmware's current limiter.

## Live

| | |
|---|---|
| Simulator | https://melonromanholidaycat.github.io/electriclight/ |
| Flasher (needs desktop Chrome) | https://melonromanholidaycat.github.io/electriclight/flash.html |
| Firmware image | https://melonromanholidaycat.github.io/electriclight/firmware/4mb/electriclight.bin |
| The guitar | `http://192.168.4.1/` on its own AP, or `http://electriclight.local/` once it is on a real network |
| AP credentials | `electriclight` / `electric-light`, committed on purpose — [`docs/decisions.md`](docs/decisions.md) |
