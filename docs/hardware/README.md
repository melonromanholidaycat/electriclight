# Hardware reference

Photographs of the guitar as it stands, before the ESP32-S3 rebuild.

| file | what it shows |
|---|---|
| `guitar-front.jpg` | the whole instrument, both strips running the length of the neck |
| `fretboard-strips.jpg` | close-up: tape, LED packages, solder pads, position across the board |

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

| | |
|---|---|
| LEDs per strip | 26 (52 total) |
| Strips | two separate runs, not daisy-chained |
| Signal direction | body to nut, so electrical index 0 is at the last fret |
| Nut to first LED | 20 mm |
| Last LED to last fret | 20 mm |
| Strip spacing | 27 mm centre to centre at the twelfth fret |
| Frets | 21, counted |
| Scale length | 648 mm (25.5"), assumed from the Strat body |

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

## Still to check

- The exact strip part, if the reel or tape carries a marking.
- What currently regulates the battery voltage down.
- Wiring, connectors, and the gauge of the run up the neck.
- How the pot and five-way are wired, and the five-way's resistor ladder.
