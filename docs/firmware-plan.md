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

Both layouts are written and both are built on every push:
[`4mb.csv`](../firmware/partitions/4mb.csv) (the default — two 1.5 MB app slots,
896 KB storage) and [`16mb.csv`](../firmware/partitions/16mb.csv) (two 3 MB
slots, 9.9 MB storage).

**4 MB is enough — measured, not estimated.** The earlier advice here was to
insist on 16 MB modules. That was written before there was a firmware to weigh,
and it was wrong in a way worth recording: it optimised for never needing a
cable again without weighing whether the board fits in a guitar.

Built for size, the step 2 image is **801 KB**. Against a 4 MB module's 1.5 MB
app slot that leaves **48% free — 753 KB** — before the evaluator, the LED
driver or effect storage go in, and those are tens of kilobytes, not hundreds.
On a 16 MB module the same image leaves 74% free.

CI builds both layouts on every push, so the app outgrowing a 1.5 MB slot would
show up as a red build months before it showed up at the cabled session.

4 MB is the default the build produces, because that is what the boards in hand
are. The remaining cost of 4 MB is honest but small: less room for something
unforeseen, and repartitioning needs a cable.

The layout needs, at minimum:

| partition | why |
|---|---|
| two OTA app slots | rollback after a bad image is non-negotiable |
| `otadata` | which slot booted, and whether it is confirmed good |
| NVS | settings, WiFi credentials, geometry, slot assignments |
| a filesystem | effect definitions, presets, the log ring buffer |

Size every partition with generous headroom. Unused flash costs nothing;
repartitioning costs a cable and a borrowed computer.

The bootloader uses 36% of its own space. The embedded page is 25 KB of the app
image, and `node web/build.js` prints the current figure.

Optimising for size rather than for debugging saved 8% — less than the 20–30%
that is usual, because most of the image is Espressif's own WiFi and networking
libraries, which are already built for size. Worth having, not a lever to pull
again.

## Pin budget

Both strips are separate runs, so:

| use | count | notes |
|---|---|---|
| LED data | 2 | one per strip, not daisy-chained |
| potentiometer | 1 | analog |
| five-way switch | 1 | analog, if it is a resistor ladder — otherwise up to 5 digital |
| battery sense | 1 | analog, via a divider |
| microphone (reserved) | 3 | I²S needs BCLK, WS and DATA. Not 2 — PDM needs two but locks you into worse parts |

### It fits the small board, with room over

An ESP32-S3 Super Mini (`ESP32-S3FH4R2`, 22.5 × 18 mm) breaks out thirteen GPIO
with no boot or system involvement: **1, 2, 4, 5, 6, 7, 8, 15, 16, 17, 18, 21,
38**. Seven of those (1–8) are ADC1, which is where the three analog inputs have
to live. Eight pins needed, thirteen available, and the ADC1 requirement is met
twice over.

A proposed map, provisional until the five-way's wiring is known:

| pin | use | why |
|---|---|---|
| GPIO15 | LED data, bass strip | safe, digital |
| GPIO16 | LED data, treble strip | safe, digital |
| GPIO1 | potentiometer | ADC1_CH0 |
| GPIO2 | five-way | ADC1_CH1 |
| GPIO4 | battery sense | ADC1_CH3 |
| GPIO17, 18, 21 | reserved for the microphone | I²S BCLK / WS / DATA |
| GPIO5, 6, 7, 8, 38 | spare | three still on ADC1 |

**GPIO48 carries an on-board WS2812.** That is worth more than it looks: the
effect evaluator and the LED driver can both be brought up and checked against
the golden vectors on a bare board, one pixel at a time, with no guitar, no
strips and no cable session. It turns a chunk of step 4 from something that has
to wait for hardware into something that does not.

Native USB, no serial-converter chip, so flashing needs nothing but a USB-C
cable — which also makes the pigtail-into-the-cavity insurance below cheaper: it
is a USB-C extension, not a programming header.

**The constraint that bites: ADC2 does not work while WiFi is active.** All
three analog inputs must therefore be on **ADC1, which is GPIO1–GPIO10** on the
ESP32-S3. That is ten pins for three jobs, so it is not tight, but putting the
pot on an ADC2 pin would produce an intermittent fault that looks like a wiring
problem and is not.

Pins to keep clear:

- **Strapping pins: GPIO0, GPIO3, GPIO45, GPIO46.** A pull-up or pull-down on
  these changes boot behaviour.
- **GPIO19 and GPIO20** are USB D− and D+ if native USB is used.
- **On modules with octal PSRAM (the `R8` suffix), GPIO35–37 are consumed.** The
  small `ESP32-S3FH4R2`-based boards carry *quad* PSRAM instead, which leaves
  those three pins free — one of the few ways the smaller board is the better
  one here.

Verify all of this against the pinout that came with the actual board.
Documentation for these generic boards is inconsistent between sellers, and pin
diagrams, memory claims and LED wiring all differ between revisions — the
listing for these ones advertises "WiFi 6", which the ESP32-S3 does not have, so
its other claims deserve checking too. The five-way's electrical arrangement is
also still unknown and needs checking when the guitar is open.

Two more things to check on a small board, neither fatal and both worth knowing
before the guitar is closed:

- **The antenna is a PCB trace with no external connector.** WiFi is the only
  transport there is, and it will be working from inside a wooden cavity next to
  a battery pack. Wood is not much of an obstacle, but this is worth confirming
  at the cabled session rather than at a rehearsal.
- **The on-board 3.3 V regulator is small.** The S3 pulls several hundred
  milliamps in bursts while transmitting, which is exactly when a marginal
  supply browns out. The decoupling already planned matters more here.

The S3's RMT peripheral has four TX channels, so two LED strips are comfortable.

## Level shifting

**Two channels, not one.** WS2812B at 5 V wants a logic high around 0.7 × VDD =
3.5 V; the S3 drives 3.3 V. It often works and it is not dependable, especially
as the data run up the neck is long.

**Chosen: SN74AHCT125N**, quad bus buffer, DIP-14. Both strips fit one chip.

The family matters more than the part. **AHCT**, not AHC: the T means
TTL-compatible input thresholds, so at a 5 V supply a logic high starts around
2 V and a 3.3 V signal is read cleanly. Plain AHC uses CMOS thresholds — about
3.5 V at a 5 V supply — and a 3.3 V signal sits right on the edge of them. The
two parts are otherwise interchangeable and look identical in a parts drawer.

Rejected: running the strips at ~4.3 V so their threshold falls under the S3's
output, which saves the part at the cost of brightness; and the sacrificial
first-LED trick, which works but turns a dead first pixel into a dead strip.
With a real shifter in place the buck stays at a full 5 V.

### How to wire it

DIP-14 pinout, as standard for a 74x125: pin 1 `1OE`, 2 `1A`, 3 `1Y`, 4 `2OE`,
5 `2A`, 6 `2Y`, 7 `GND`, 8 `3Y`, 9 `3A`, 10 `3OE`, 11 `4Y`, 12 `4A`, 13 `4OE`,
14 `VCC`.

- **Power it from the 5 V strip rail**, not from 3.3 V. Its output swinging to
  5 V is the entire point.
- `OE` is **active low**. Tie pins 1 and 4 to GND so the two used channels stay
  enabled.
- Feed GPIO15 into pin 2 and GPIO16 into pin 5; take strip data from pins 3
  and 6.
- **Do not leave the unused inputs floating.** Tie pins 9 and 12 to GND, and
  pins 10 and 13 to VCC so those outputs stay disabled. A floating CMOS input
  drifts to mid-rail and oscillates, which wastes current and injects noise into
  a board that is already sharing a supply with several amps of LEDs.
- 100 nF ceramic directly across pins 14 and 7, as close to the chip as it will
  sit.
- ~330 Ω in series with each output before it reaches the strip, to damp
  reflections on the run up the neck.

DIP-14 needs something to sit on — perfboard or a small proto board — and takes
about 19 × 7 mm plus that. Not a problem in a Strat control cavity, but it is
not a part that can be free-wired tidily.

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
