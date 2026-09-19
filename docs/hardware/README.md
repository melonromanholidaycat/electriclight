# Hardware reference

Photographs of the guitar as it stands, before the ESP32-S3 rebuild.

**This file is the only home for measured numbers.** Every other document links
here rather than restating them, because a number written down twice is a number
that will disagree with itself within a month. The measurement table below is
parsed by `web/test/run.js` and checked against the simulator's defaults in
`web/src/model/geometry.js`, so the code cannot drift from it either: change a
figure here and the build fails until the code agrees.

| file | what it shows |
|---|---|
| `guitar-front.jpg` | the whole instrument, both strips running the length of the neck |
| `fretboard-strips.jpg` | close-up: tape, LED packages, solder pads, position across the board |
| `cavity-overview.jpg` | the pickguard lifted: Nano, loom, both control groups |
| `nano-and-loom.jpg` | the Arduino Nano and everything wired to it |
| `controls-rotary-switch.jpg` | the dedicated rotary switch for the LEDs |
| `controls-rotary-switch-detail.jpg` | the same from the side |
| `neck-pocket-wiring.jpg` | where the strips enter the body: three conductors per strip |
| `gy-61-accelerometer.jpg` | the sensor beside the Nano, silkscreen legible |
| `battery-holder-loaded.jpg` | six AA cells in the flush-mount holder |
| `battery-holder-empty.jpg` | the slots and their moulded polarity marks |
| `battery-holder-contacts.jpg` | the underside: contact tabs and the links between them |
| `board-front.jpg` | the ESP32-S3 Super Mini, header labels legible |
| `board-back.jpg` | its underside: board revision, pin list, B+/B- pads |
| `board-datasheet-callouts.jpg` | the seller's numbered feature diagram, front and back |
| `board-datasheet-pinout.jpg` | the seller's pinout, both rows, with per-pin peripherals |
| `board-datasheet-specs.jpg` | the seller's stated specification |
| `board-datasheet-power.jpg` | the seller's power options, and the warning about them |

The four `board-datasheet-*` images are **the seller's material, not measurements.**
They are kept because they are the only per-pin reference that matches this exact
revision, and because two of them say things nothing else here says. They do not
outrank anything measured on the board: where they disagree with it, they are
noted as unconfirmed below.

What the close-up settles:

- The strips are **adhesive tape on the face of the fretboard**, out near the
  edges and just inside the outer strings. Estimating the spacing off the
  photograph gave about 30 mm; measuring it gave 27 mm, which is the figure in
  use. Photographs establish arrangement, not dimensions.
- The pads read **5V / GND / DI** with data-direction arrows, and the LEDs are
  5050 packages. So: a 5 V, three-wire, RGB addressable strip. Not WS2815
  (12 V, four wire), not RGBW. WS2812B or an externally identical equivalent.
- The tape is continuous and uncut, so the **LED pitch is fixed in millimetres**.
  It therefore cannot track the frets, whose spacing is geometric. This is why
  the simulator defaults to even spacing.

## Measured

The rows with a `key` are read by the test. Keep the key, and put the number
first in the value.

| measurement | value | key |
|---|---|---|
| LEDs per strip | 26 (52 in total) | `ledsPerStrip` |
| Frets | 21, counted | `frets` |
| Scale length | 648 mm (25.5"), assumed from the Strat body | `scaleLength` |
| Nut to first LED | 20 mm | `nutToFirstLed` |
| Last LED to last fret | 20 mm | `lastLedToLastFret` |
| Strip spacing, centre to centre at the twelfth fret | 27 mm | `stripSpacing` |
| Strips | two separate runs, not daisy-chained | |
| Signal direction | body to nut, so electrical index 0 is at the last fret | |

### What that arithmetic confirms

On a 25.5" scale the 21st fret sits 455.3 mm from the nut. The LEDs therefore
span 20 mm to 435.3 mm, which over 25 gaps is a **16.61 mm pitch — 60.2 LEDs per
metre**. A standard 60/m tape is 16.67 mm, so the measurements agree with a real
commercial part to within 0.4%, and they only do so for a 21-fret neck. A 22-fret
neck would imply 58.7/m, which no one makes.

Two independent facts falling out of one set of measurements is good evidence
the numbers are right. The simulator shows the implied density on its Setup page
for exactly this reason: if a future measurement makes it land somewhere strange,
that is the measurement being wrong, not the tape.

Both were confirmed: the neck has 21 frets, counted.

## The controller board

**HW-747 V0.0.2**, an ESP32-S3 Super Mini. The header rows carry **GPIO1–13**
plus RX/TX, 3V3, GND and 5V, and nothing else — confirmed against the board
itself, not against a published pinout. The espboards.dev reference for this
name describes a revision with far more pins broken out, and following it would
have put LED data on GPIO15 and 16, which this board does not have.

It is definitely an S3 and not a C3: the underside silkscreen lists GPIO33–48,
and a C3 stops at 21.

**The red `C3` on the front edge is the 2.4 GHz ceramic antenna**, item 5 in the
seller's callout diagram. Earlier notes here and in conversation called it a
component designator, which was wrong twice over — it is a part, and it is the
part the whole wireless design depends on. Its position has a consequence that
belongs in [`../firmware-plan.md`](../firmware-plan.md): it sits at the board
edge, and a Strat control cavity is very often lined with conductive shielding
paint.

**Both buttons are present: BOOT and RESET**, items 6 and 7. The seller gives the
download-mode procedure as *hold BOOT, then press RESET*. The flasher page had
been hedging about whether a RESET button existed; it no longer needs to.

**The `3V3` pin is labelled `3V3(OUT)`** in the seller's pinout — an output of
the on-board regulator. Independent agreement with the 3.3 V measured on the pin
whose silkscreen reads `CV3`.

**2 MB PSRAM.** The part is an `ESP32-S3FH4R2`: 4 MB flash, 2 MB PSRAM, 512 KB
SRAM. The firmware does not enable PSRAM and does not need it — the whole render
engine addresses 52 pixels — but it is there if something later does.

### The underside pad row

**Confirmed against the board in hand.** There is a second row of pads on the
underside carrying **GPIO14–18, 21 and 33–48**, in addition to the outer header
row of GPIO1–13 plus RX/TX, 3V3, GND and 5V. An earlier version of this section
said the board did not bring these out; that was read off the front of the board
only.

Two details that corroborate the seller's diagram rather than contradicting it:
GPIO19 and 20 are **absent** from the row, which is correct — they are the
native USB data lines this board flashes through. And GPIO33–37 are present and
usable here because the module is an `ESP32-S3FH4R2`, which has *quad* PSRAM; on
an octal-PSRAM part those pins are consumed by the memory bus. This firmware
does not enable PSRAM at all, so the question does not arise.

Which of the new pins are actually free to use:

| pins | usable | note |
|---|---|---|
| 14–18, 21, 33–38, 47 | yes | plain digital |
| 39–42 | yes | these are the JTAG pins; nothing here uses JTAG |
| 45, 46 | avoid | strapping pins, sampled at reset |
| 48 | taken | the on-board RGB LED |
| 19, 20 | not present | native USB |

**None of them is ADC1.** GPIO14–18 are ADC2, which stops working once WiFi is
up, and 21 and 33–48 have no ADC at all. So the row is a large amount of
*digital* room and not one more analogue channel — which is the distinction that
decides what it does and does not unlock. See the pin budget in
[`../firmware-plan.md`](../firmware-plan.md).

It changes nothing about the pin map in use. GPIO12 and 13 carry LED data
because they are ADC2-only and therefore useless for analogue once WiFi is up,
not because nothing else was free.

**The pin between GPIO13 and GND reads `CV3` rather than `3V3`. Measured at
3.3 V against GND over USB — it is the 3.3 V rail, and the silkscreen is a
typo.** Its position said as much, between the GPIO run and ground, which is
where every variant of this board puts it. It was measured rather than assumed
because the potentiometer's top end connects to it, and the ESP32's analogue
inputs are not 5 V tolerant.

## The controls, as found

Confirmed by opening the guitar. **All 52 LEDs were verified working on the
original Nano before anything was disconnected**, so any dead pixel found later
is something the rebuild did.

**The five-way is a dedicated rotary switch, not the guitar's pickup selector.**
It is wired with five separate conductors back to the controller — one per
position — rather than as a resistor ladder. So it costs five digital pins, not
one analog pin. The pin budget still closes; see
[`../firmware-plan.md`](../firmware-plan.md).

**The potentiometer is an A500K push-pull, and its switch section is the system
power switch.** Two consequences, both in the firmware plan: 500 kΩ is a far
higher source impedance than the ESP32's ADC likes, and the `A` means a
logarithmic taper, which interacts with the gamma already in the output chain.
The push-pull is worth keeping — it is the only power switch the instrument has.

**The blue-taped module beside the Nano is a GY-61**, not a regulator. Its
silkscreen reads `VCC X_OUT Y_OUT Z_OUT GND`, which identifies it as an
**ADXL335: a three-axis analogue accelerometer**. Not a gyroscope, despite the
name it has been going by, and that distinction is the whole story of why the
orientation effects built on it only half worked.

An accelerometer alone cannot separate tilt from movement. Both arrive as
acceleration on the same three axes, so gravity — the thing you want, because it
tells you which way the guitar is pointing — is inseparable from strumming,
walking and every knock against a strap. No amount of firmware fixes that; the
information is not in the signal.

It is deferred in the same way audio-reactive lighting is: not to be built on
now. See [`../firmware-plan.md`](../firmware-plan.md) for what it costs in pins
and why a different part is the answer when orientation work eventually
happens.

**There was no regulator because the old build did not need one.** Two
conductors ran from the battery cavity to the push-pull switch with no regulator
chip or module anywhere on the loom, and **the pack measures 4.63 V** — already
inside what a WS2812B will run on. The strips were fed from the cells directly.

That also corrects an earlier guess in this file, that the Nano's own on-board
regulator was feeding them. It was not; nothing was.

The holder takes six AA cells and is **wired as two parallel banks of three**,
confirmed by inspection. The way it achieves that is the part worth knowing:
**three of the cells go in backwards**, against the polarity moulded into the
holder, with added jumper wires making the two banks. That is why the pack reads
4.63 V — three cells in series at 1.54 V each, which is an old alkaline with its
open-circuit voltage intact and its capacity gone.

### This holder is a booby trap, and it has already sprung once

Insert the cells the way the holder's own markings tell you to, and the jumpers
put the two banks in **anti-parallel** — 9 V driving a closed loop through six
cells, with nothing but their own internal resistance to limit it.

| cells | Ω per cell | loop | current | heat, inside the pack |
|---|---|---|---|---|
| old alkaline | 0.30 | 1.80 Ω | 5 A | 45 W |
| fresh alkaline | 0.15 | 0.90 Ω | 10 A | 90 W |
| **NiMH** | 0.04 | 0.24 Ω | **37 A** | **338 W** |

The owner did exactly this while investigating, caught it by the heat, and got
the cells out in time. With the NiMH cells the rebuild calls for, the same
mistake would be roughly seven times worse — and NiMH will happily deliver that
current rather than sagging out of it the way a tired alkaline does.

**Restore the holder to a plain six-in-series string**: remove the jumpers, put
the original links back, and let the cells go in the way the moulding says. That
is less work than maintaining the hack, it is what the converter wants anyway,
and it removes a trap that currently punishes anyone who inserts batteries
correctly.

The old alkalines should be discarded rather than reused — they have been
through a partial short, and the rebuild moves to NiMH regardless.

And it finally explains the capped brightness properly. Alkaline cells have an
internal resistance around 0.15–0.3 Ω each, rising as they deplete. Three in
series lose about 1.3 V inside the cells at 2 A and 2 V at 3 A — so the harder
an effect drove the strips, the further the supply collapsed. The ceiling was
never the regulator. It was the chemistry.

It does not matter. **The rebuild feeds the strips 5 V because that is what they
are specified for**, not because of anything the old build did. Reverse-
engineering a previous owner's guess was only ever archaeology.

One thing to carry forward, though: if the old setup *was* unregulated, the
strips spent their life above their rated supply. All 52 were verified working
immediately before teardown, so any damage is not yet visible — but if pixels
start misbehaving once everything else is known good, "the tape is tired" is a
reasonable hypothesis rather than an absurd one.

**The original wiring is gone.** What replaces it is specified in
[`../firmware-plan.md`](../firmware-plan.md) rather than inherited, which is an
improvement: the old run was sized for whatever the Nano's regulator could
deliver, and the rebuild asks for considerably more.

## Still to check

Nothing should be designed around an assumption where one of these is missing.

- The exact strip part, if the reel or tape carries a marking. Taken as
  WS2812B-equivalent for now: 5 V, three-wire, individually addressable.
