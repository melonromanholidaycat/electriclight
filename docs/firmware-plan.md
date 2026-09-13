# Firmware plan

What has to be true before and during the cabled session, written down while it
is fresh, because the session is scarce and the cost of arriving with the wrong
module is a delay measured in weeks.

**Built so far (step 2):** an ESP-IDF project in [`firmware/`](../firmware/)
that boots, joins a known network or falls back to its own access point,
announces itself over mDNS, serves the embedded control page, exposes
`/api/status`, `/api/log` and `/api/ota`, and takes a firmware image over the
air with automatic rollback if the new one cannot be reached. No LED output and
no effect evaluator yet — those are steps 3 and 4.

Two things in it are deliberately provisional and must change before the guitar
is closed. The radio follows a stored setting that defaults **on**, because
there is no hardware yet to read a boot gesture from; and the health check that
confirms a new image counts "reachable" as healthy, which will want to include
"the LEDs actually lit" once there are any.

The guiding rule from the brief: **anything that cannot be changed over WiFi is
effectively permanent.** Most of this document is that rule applied.

---

## What actually needs a cable

Short list, and it is worth keeping short:

1. **The first flash.** Obviously.
2. **The partition table.** Its *contents* update over the air; its *layout*
   does not. Getting this wrong is the most expensive mistake available.
3. **Anything physically miswired.**

Everything else — effects, settings, geometry, brightness, WiFi credentials,
the web UI itself — must be reachable over the air, or it is a design bug.

## Flash and partitions

The layout is written: [`firmware/partitions/16mb.csv`](../firmware/partitions/16mb.csv),
with [`4mb.csv`](../firmware/partitions/4mb.csv) as the fallback if the boards
turn out to be smaller. Two 3 MB app slots, a 24 KB NVS, and the remainder as
storage.

**Buy or confirm 16 MB modules.** A 4 MB ESP32-S3 has to fit two OTA app slots,
the embedded web UI and stored effects, and it will be tight enough to force bad
choices later. 16 MB removes the question. If the boards on hand are 4 MB, that
is worth knowing *before* the cabled session, not during it.

The layout needs, at minimum:

| partition | why |
|---|---|
| two OTA app slots | rollback after a bad image is non-negotiable |
| `otadata` | which slot booted, and whether it is confirmed good |
| NVS | settings, WiFi credentials, geometry, slot assignments |
| a filesystem | effect definitions, presets, the log ring buffer |

Size every partition with generous headroom. Unused flash costs nothing;
repartitioning costs a cable and a borrowed computer.

As built, the step 2 image is 871 KB against a 3 MB slot — **72% of the app
partition is still free**, before there is any evaluator, LED driver or effect
storage in it. The bootloader uses 36% of its own space. The embedded page is
25 KB of that image, and `node web/build.js` prints the current figure. All of
it grows; none of it is close.

## Pin budget

Both strips are separate runs, so:

| use | count | notes |
|---|---|---|
| LED data | 2 | one per strip, not daisy-chained |
| potentiometer | 1 | analog |
| five-way switch | 1 | analog, if it is a resistor ladder — otherwise up to 5 digital |
| battery sense | 1 | analog, via a divider |
| microphone (reserved) | 3 | I²S needs BCLK, WS and DATA. Not 2 — PDM needs two but locks you into worse parts |

**The constraint that bites: ADC2 does not work while WiFi is active.** All
three analog inputs must therefore be on **ADC1, which is GPIO1–GPIO10** on the
ESP32-S3. That is ten pins for three jobs, so it is not tight, but putting the
pot on an ADC2 pin would produce an intermittent fault that looks like a wiring
problem and is not.

Pins to keep clear:

- **Strapping pins: GPIO0, GPIO3, GPIO45, GPIO46.** A pull-up or pull-down on
  these changes boot behaviour.
- **GPIO19 and GPIO20** are USB D− and D+ if native USB is used.
- **On modules with octal PSRAM (the `R8` suffix), GPIO35–37 are consumed.**

Verify all of this against the datasheet for the exact module before wiring
anything. The five-way's electrical arrangement is still unknown and needs
checking when the guitar is open.

The S3's RMT peripheral has four TX channels, so two LED strips are comfortable.

## Level shifting

**Two channels, not one.** WS2812B at 5 V wants a logic high around 0.7 × VDD =
3.5 V; the S3 drives 3.3 V. It often works and it is not dependable, especially
as the data run up the neck is long.

Options, roughly in order of preference:

1. A proper shifter — a 74AHCT125 covers four channels, and both strips fit in
   one chip.
2. Run the strip at ~4.3 V instead of 5 V, which brings its threshold under the
   S3's output. Costs a little brightness, saves a part.
3. A sacrificial first LED as a shifter. Works, widely used, ugly, and it means
   a dead first pixel is now a dead strip.

## Power

Worst case is **every LED at full white: 3.1 A at 5 V**, about 15.6 W, which
through a buck converter is roughly **2.5 A from a 7.2 V pack**. (LED counts and
every other measurement: [`hardware/`](hardware/).)

- Size the buck for the worst case with headroom. A 3 A module is not enough;
  many cheap ones cannot hold 3 A in practice. A synchronous 5 A part is the
  comfortable choice.
- **AA holder spring contacts are a real series resistance at 2.5 A.** So are
  thin wires up the neck. Cheap holders sag noticeably under load, and a sagging
  supply is indistinguishable from a firmware fault when you cannot see a serial
  port.
- NiMH rather than alkaline — alkalines sag badly under this kind of load.
- Bulk capacitance close to the strips, and proper decoupling at the board.
  Radio transmission spikes can brown out an ESP32 that looks adequately
  supplied at DC.

The brightness ceiling is a remotely editable setting, so it should not end up
load-bearing for safety — which is the point of sizing the hardware for the
worst case rather than for the expected one. The automatic dim the brief
requires needs the battery divider on an ADC1 pin, per the budget above.

## Reachability, which is the thing that can strand the project

The brief requires the radio off unless deliberately enabled. That saves power
and stops anyone connecting mid-set, and it creates one hazard: if the only path
to turning it on is stored configuration, then bad stored configuration makes
the guitar unreachable from a phone.

So:

- **Safe mode must force the radio on**, ignoring stored config, with hardcoded
  fallback AP credentials.
- **Those credentials are not permanent — they ship in the image and an OTA can
  change them — but they are the last way back.** Which makes changing them the
  riskiest edit in the firmware: get safe mode wrong in a build that also breaks
  normal operation and there is no path left except a cable. Treat a change to
  safe mode as a change that has to be verified before it is relied on, not as
  an ordinary edit.
- Safe mode must be reachable by a **physical gesture at boot** — the five-way
  and the pot are the only inputs available, so some combination of them held at
  power-on. Design it so it cannot be hit by accident on stage.

On a known network, mDNS works from iOS Safari, so `something.local` is the
friendly path. On the SoftAP fallback, iOS will complain about no internet and
may try captive-portal detection; that is survivable but worth handling
deliberately rather than discovering it in a car park.

## Remote logging

There is no serial monitor, so a log that only exists on a wire does not exist.
A ring buffer in RAM served over HTTP is enough, and it must survive the thing
it is diagnosing — which means it needs to be readable in safe mode too.

## OTA

- Two app slots, boot the new one, and **confirm it good only after a health
  check passes**. An image that boots and then wedges must roll back on its own.
- Manual firmware upload from the phone's Files app works in iOS Safari, so keep
  that path available as a fallback when the normal one is broken.
- Effect bytecode carries function indices, and firmware must refuse an effect
  using one it does not know rather than run it anyway. The rule and its reason
  are in [`effect-format.md`](effect-format.md#bytecode); it matters here because
  an effect carried over from the Pages simulator is always from a newer build
  than the device.

## Cheap insurance before the guitar is closed

**Route a USB-C pigtail or a small programming header into the rear cavity.** It
does not help the phone, but it turns "borrow a laptop *and* disassemble the
guitar" into "borrow a laptop". Given how much of this project's design bends
around scarce cable access, it is the best few euros available.

## Cabled session checklist

In order, and the order matters:

1. **Validate the hardware with known-good third-party firmware first** — WLED
   or similar. Power, wiring, strip type, LED count and direction all get
   confirmed before any custom code is in the picture. Debugging your own
   firmware against unverified wiring is the slowest possible loop.
2. Check supply voltage **under load**, not at idle, at the far end of the neck.
3. Confirm both strips independently, and confirm the data direction — index 0
   is expected at the *last fret*, not the nut.
4. Flash the real firmware and **verify OTA works on the bench, before closing
   the guitar**. An OTA path that has never been exercised is not a path.
5. Verify safe mode entry by its physical gesture, also before closing.
6. Flash all three boards, so a brick is a swap rather than another session.

## Host-testable core

The effect evaluator (step 4) has to reproduce `web/test/vectors.json` exactly,
and proving that should not need a device. It will live in a plain C++ component
with no ESP-IDF dependencies, built twice: into the firmware, and natively on
the CI runner against the golden vectors. Nothing of it exists yet — the step 2
firmware is all hardware-facing code, and an empty abstraction would be worse
than none.

## Known issues to handle later

- **Stored settings win over new defaults.** That is correct — a firmware update
  must never silently rewrite someone's library — but it means corrected
  hardware facts need an explicit reset. When the device stores the library
  (step 5), this needs a real answer: probably a versioned hardware-facts block
  that is separate from user content and may be updated by the firmware, while
  effects and presets are never touched.
- **Appending an opcode is backward compatible for decoding but not for
  execution.** See the refusal requirement above.
