# Hardware reference

Photographs of the guitar as it stands, before the ESP32-S3 rebuild.

| file | what it shows |
|---|---|
| `guitar-front.jpg` | the whole instrument, both strips running the length of the neck |
| `fretboard-strips.jpg` | close-up: tape, LED packages, solder pads, position across the board |

What the close-up settles:

- The strips are **adhesive tape on the face of the fretboard**, out near the
  edges and just inside the outer strings — about 30 mm apart centre to centre.
- The pads read **5V / GND / DI** with data-direction arrows, and the LEDs are
  5050 packages. So: a 5 V, three-wire, RGB addressable strip. Not WS2815
  (12 V, four wire), not RGBW. WS2812B or an externally identical equivalent.
- The tape is continuous and uncut, so the **LED pitch is fixed in millimetres**.
  It therefore cannot track the frets, whose spacing is geometric. This is why
  the simulator defaults to even spacing.

What it does not settle, and what to measure next:

- **How many LEDs on one strip.** With even spacing, that one number fixes the
  entire geometry.
- Where the first and last LED sit relative to the nut and the last fret.
- Whether the strips are one chain or two, and which end feeds each.
- Strip-to-strip distance with an actual ruler, to replace the estimate read off
  the photograph.
