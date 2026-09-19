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

**No separate voltage regulator could be found.** Two conductors run from the
battery cavity to the push-pull switch, and there is no regulator chip or module
visible on the loom. The likeliest explanation is that the strips are fed from
the Arduino Nano's own on-board linear regulator, which would explain the
brief's long-standing suspicion that the present setup caps achievable
brightness — a Nano's regulator can supply a few hundred milliamps at most, and
drops the whole difference from the pack as heat.

## Still to check

Open until the guitar is opened. Nothing should be designed around an assumption
where one of these is missing.

- **The voltage on a strip's 5 V conductor with the system powered on.** One
  measurement, and it settles whether anything regulates the pack down at all.
  Around 5 V means a regulator exists somewhere; around 9 V means the strips
  have been running well over their rated supply.
- **What the blue-taped component beside the Nano is** (`nano-and-loom.jpg`,
  top of frame). It is the only thing on the loom that could be a regulator.
- The gauge of the conductors running up the neck. They were sized for whatever
  the old setup could drive, which was probably a fraction of what the rebuild
  will.
- The exact strip part, if the reel or tape carries a marking. Taken as
  WS2812B-equivalent for now: 5 V, three-wire, individually addressable.
- Confirmation that the new board's own pinout matches the published one for the
  ESP32-S3 Super Mini. These generic boards vary between sellers and revisions.
