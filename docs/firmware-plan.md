# Firmware plan

What has to be true before and during the cabled session, written down while it
is fresh, because the session is scarce and the cost of arriving with the wrong
module is a delay measured in weeks.

**Built so far (steps 2 and 3):** an ESP-IDF project in
[`firmware/`](../firmware/) that boots, decides on what terms the radio comes
up, joins a known network or falls back to its own access point, announces
itself over mDNS, serves the embedded control page, exposes `/api/status`,
`/api/log`, `/api/ota`, `/api/wifi`, `/api/radio` and `/api/selftest`, and takes a firmware image
over the air with automatic rollback if the new one cannot be reached.

The survival features are in: the sweep gesture, safe mode on the same gesture
with the brightness down, boot-loop rescue after three boots that never reach a
healthy state, and a log in RTC memory that survives a crash. The decisions are
plain C in [`../firmware/components/core`](../firmware/components/core) with no
ESP-IDF in them, and CI compiles and runs them natively on every push.

No LED output and no effect evaluator yet — that is step 4. **832 KB**, which
leaves 47% of a 4 MB module's app slot free.

Three things are deliberately provisional and must change before the guitar is
closed:

- **`radio_always_on` defaults on**, because a bare board has no way to perform
  a gesture. `POST /api/radio {"alwaysOn": false}` arms the gesture, and that is
  the moment the brief's "radio off unless deliberately enabled" starts holding.
  Until then this firmware is more reachable and less discreet than the finished
  instrument.
- **The health check counts "reachable" as healthy.** It should eventually also
  mean the LEDs lit, once there are any to light.
- **Nothing is wired to the input pins.** The firmware handles that — with
  pull-ups and no wiring, every switch pin reads high, which is
  indistinguishable from mid-sweep, so it never sees a gesture rather than
  inventing one — but no gesture has yet been read from a real switch.

The guiding rule from the brief: **anything that cannot be changed over WiFi is
effectively permanent.** Most of this document is that rule applied.

---

## What actually needs a cable

Short list, and it is worth keeping short:

1. **The first flash.** Obviously — but it costs a browser tab, not a toolchain.
   See [`decisions.md`](decisions.md) for the web flasher and why it was built
   before the step it delayed.
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
| five-way switch | 5 | **digital.** It is a rotary switch with one conductor per position, not a resistor ladder |
| battery sense | 1 | analog, via a divider |
| microphone (reserved) | 3 | I²S needs BCLK, WS and DATA. Not 2 — PDM needs two but locks you into worse parts |

### Digital pins are plentiful; analogue ones are not

Which pins this board has, which of them are free, and which are strapping or
already spoken for: [`hardware/`](hardware/). What matters here is what that
list is shaped like.

**The constraint is not how many pins there are.** It is that **ADC1 is GPIO1–10
and nothing else** — the only converter that still works once WiFi is up.
Everything on the underside pad row is ADC2 or has no ADC at all.

So that row is a great deal of digital room and **not one extra analogue
channel.** Anything digital is effectively free; anything analogue still competes
for ten pins, seven of which are already taken.

| pin | use | why |
|---|---|---|
| GPIO12, 13 | LED data, bass and treble | ADC2-only, so nothing analogue is given up |
| GPIO1 | potentiometer | ADC1_CH0 |
| GPIO2 | battery sense | ADC1_CH1 |
| GPIO4–8 | five-way, one pin per position | digital in, internal pull-ups, common to GND |
| GPIO9, 10 | spare, and the only spare ADC1 | the scarce resource |
| GPIO11 | spare, ADC2 | digital only |
| GPIO3 | avoid | strapping |
| GPIO14–18, 21, 33–42, 47 | spare, digital | underside pads — **last resort**, fiddly by hand |
| GPIO45, 46 | avoid | strapping |

**On the outer header, where the easy joints are: three spare pins, two of them
ADC1.** The underside row adds about twenty more, but the owner's standing
preference is to stay off it, so the real budget is the smaller number. Both
facts matter to the next section.

**Do not use the B+/B− pads on the underside** — they are a single-cell LiPo
charger input, not somewhere to attach the pack. The converter's 5 V goes to the
5V pin.

**GPIO48 carries an on-board RGB WS2812.** It appears on the underside row, so
it is reachable, but it already has a job. That LED is worth more than it looks:
the effect evaluator and the LED driver can both be brought up and checked
against the golden vectors on a bare board, one pixel at a time, with no guitar,
no strips and no cabled session. It turns a chunk of step 4 from something that
has to wait for hardware into something that does not.

(GPIO43 and 44 are the UART0 pins, and are the `TX` and `RX` on the front header
rather than part of the underside row.)

Native USB, no serial-converter chip, so flashing needs nothing but a USB-C
cable — which also makes the pigtail-into-the-cavity insurance below cheaper: it
is a USB-C extension, not a programming header.

### The two deferred sensors, and where the budget runs out

There is a **GY-61 already in the guitar**, taped beside the Nano. It is an
ADXL335 — a three-axis *analogue accelerometer*, not the gyroscope it has been
called. It is deferred and not to be built on now.

It is an expensive part to keep, and the underside pad row does not make it any
cheaper. Its three outputs are **analogue**, and every pin that row added is
ADC2 or no-ADC. The scarce resource is untouched: **ADC1 is ten pins, the pot
and battery sense take two, and the five-way currently occupies five more.**

Counting only what each option actually competes for:

| configuration | ADC1 used, of 10 | digital used | verdict |
|---|---|---|---|
| base — strips, pot, battery, five-way | 7 | 2 | fits easily |
| plus a microphone (I²S, three digital) | 7 | 5 | fits easily |
| plus a 6-axis I²C IMU (two digital) | 7 | 7 | fits easily |
| plus the GY-61 (three analogue) | **10** | 2 | full, and GPIO3 is strapping |
| the GY-61 with the five-way moved to the underside row | 5 | 7 | comfortable |

The row's effect is narrow and worth stating plainly: **every digital option
became free, and no analogue one did.** A microphone and an IMU together, which
this plan previously called "over by three", now cost nothing anyone has to
think about.

**Do not wire the GY-61 during the rebuild.** An earlier revision of this file
said to connect it on the grounds that it was cheap — that was written believing
it was an I²C part costing two digital pins. Three ADC1 channels for a sensor
that cannot do the job is not cheap, and leaving it unwired costs nothing: it
stays physically mounted, and whoever revisits orientation effects will have the
guitar open to fit a better part anyway.

When that happens, a 6-axis I²C IMU — MPU-6050, LSM6DS3, ICM-42688 — is the
answer, and it wins twice over: a gyroscope lets sensor fusion separate gravity
from movement, which is the thing the ADXL335 physically cannot do, and it
occupies two digital pins instead of three analogue ones. Same size, same money.

### If the budget ever does bind, in the order to try things

The owner's standing preference is to stay off the underside pads: they take a
fiddly hand-solder, and the wiring this rebuild replaces already failed at its
joints. That ranks the options, and it un-ranks one I had put first.

Counting only the outer header, and leaving GPIO3 alone, there are **twelve
usable pins and nine are taken.** GPIO9, 10 and 11 are free — three digital, two
of them ADC1.

1. **Fit the right sensor and the crunch never arrives.** A 6-axis I²C IMU costs
   two digital pins. GPIO9 and 10 take it, and GPIO11 is still spare. This is
   already the recommendation above for reasons that have nothing to do with
   pins.
2. **One sensor of any kind fits.** An I²S microphone costs three digital pins,
   which is exactly what is free.
3. **Both sensors is where it binds** — five pins wanted, three free. Then the
   **resistor ladder comes back**: four resistors at the switch collapse the
   five-way onto a single ADC pin and hand back four. It also cuts the wires
   running from the switch to the board from six to two, which is fewer delicate
   joints, not more. The costs are real but small: ADC sampling, and thresholds
   that have to separate five levels reliably.
4. **The underside pads, last.** Moving the five-way there frees the same four
   pins with no resistors and no thresholds, but it is five hand-soldered pads.
   Worth it only if the ladder proves unreliable.

An earlier revision of this section called the ladder retired and put the
underside pads first, on the grounds that they were electrically cleaner. That
was true and beside the point: a joint that might not hold is worse than a
threshold that might need tuning, and tuning happens over WiFi.

None of this needs doing speculatively. The crunch only arrives if a second
sensor is fitted, and fitting one means the guitar is open that same afternoon.

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

- **The antenna is a ceramic chip at the board's front edge**, with no external
  connector — the red part marked `C3`, identified from the seller's callout
  diagram in [`hardware/`](hardware/). WiFi is the only transport there is.
  Wood is not much of an obstacle; **conductive shielding paint is**, and a Strat
  control cavity is very often lined with it. That is a Faraday cage with the
  only radio in the instrument inside it.

  **Checked, and this cavity is not shielded** — no paint, no foil. The concern
  is closed. It is written down because the fix was cheap only while the guitar
  was open: if the cavity is ever shielded later, the board wants its antenna
  edge pointing at the pickguard opening rather than buried against a painted
  wall, or a window scraped in the paint behind it.

  Still worth keeping the antenna edge clear of the battery pack and of any
  wiring run, which costs nothing to do while mounting.
- **The on-board 3.3 V regulator is small.** The S3 pulls several hundred
  milliamps in bursts while transmitting, which is exactly when a marginal
  supply browns out. The decoupling already planned matters more here.

The S3's RMT peripheral has four TX channels, so two LED strips are comfortable.

## The controls, and what becomes of them

Three functions exist on two physical parts, and all three survive the rebuild.
Nothing the instrument can do today becomes phone-only.

| control | today | after |
|---|---|---|
| push-pull, pulled | power on/off, carrying the whole LED current | power on/off, driving the converter's enable pin — milliamps, not amps |
| pot, rotated | brightness | master brightness by default; a preset may rebind it to any one named parameter |
| rotary five-way | effect selector | selects one of five stored slots directly |

What an effect actually sees of the last two is in
[`effect-format.md`](effect-format.md); this section is about the parts.

**Cutting power mid-write is safe by construction.** The push-pull kills the
converter and therefore the board, with no warning and no shutdown. Settings
live in NVS, which is built to survive losing power during a write, and an OTA
caught half-written simply fails to become valid — the previous slot still
boots. Neither needed extra work; both are worth knowing before someone asks.

### The strips are the only status display

Once the guitar is closed there is no other output. No serial port, no screen,
and the board's own LED is sealed inside the cavity. So anything the instrument
needs to tell someone standing in front of it — radio on, safe mode, a flat
pack, an update applied — has to be said with the 52 LEDs it already has.

That is a firmware design constraint rather than a hardware one, but it belongs
here because it decides what the instrument can communicate at all.

### Switching the radio on without a phone

The brief requires the radio off unless deliberately enabled, and the only
inputs available for "deliberately" are the pot and the five-way. The chosen
trigger is **a gesture**, because it is the only candidate that cannot be
performed by accident; see [`decisions.md`](decisions.md) for what was rejected.

**Sweep the five-way from one end position to the other, within a few seconds of
switching on.** Direction does not matter. Both end positions have to be visited,
which is a four-notch travel — far too large and too deliberate to happen while
someone reaches for the guitar.

The mechanics that make it work:

- **The window starts when the firmware starts, not at power-on**, and the
  bootloader takes a few hundred milliseconds before that. Default the window to
  5 seconds — a person takes about one to move a hand to the switch — and make
  it a remotely adjustable setting like everything else here.
- **Break-before-make means no input is low while the switch is in transit.**
  That state is normal during a sweep and a fault at rest; the firmware must not
  confuse the two. It is also what makes a sweep legible rather than a jump.
- **No gesture means the radio stays off** and the stored slot plays as usual.
  That is the ordinary path, on stage and everywhere else.
- **Turning it off again is a power cycle without the gesture.** Nothing to
  remember.

**Safe mode is the same gesture with the brightness at minimum.** One physical
vocabulary rather than two, and minimum brightness is a position the knob has to
be deliberately put in. Safe mode then does what it always did: ignore stored
configuration, force the radio up on the compiled-in fallback access point.
Boot-loop detection still reaches it with no gesture at all, for when nobody is
there to perform one.

**The strips confirm it**, because they are the only output the instrument has
once it is closed. A single sweep of colour up the neck on success, distinct
from anything an effect does at startup. Optionally one dim LED at the nut while
the window is open, so it is possible to tell the gesture was seen at all rather
than guessing.

None of this is built yet — it is step 3.

## The potentiometer is 500 kΩ, and that needs handling

It is an **A500K push-pull**, and its switch section is the instrument's power
switch, so replacing it is not free: a 10 kΩ linear push-pull is an unusual part
and the push-pull function is worth more than the convenience.

**500 kΩ is far above what the ESP32's ADC wants**, which is nearer 10 kΩ. The
converter charges a small sampling capacitor from the source, and through half a
megohm it cannot do that quickly enough — readings come out noisy and slow to
settle, and it looks exactly like a firmware bug. It worked acceptably on the
Nano because the ATmega's input is more forgiving; do not read that as evidence
it will work here.

**Wire it between the board's 3.3 V rail and ground, with the wiper to GPIO1 —
never from 5 V.** The pot is a divider, so its top end sets the wiper's maximum,
and the ESP32's analogue inputs are not 5 V tolerant: a pot fed from 5 V would
put up to 5 V on an input rated for 3.3, and would do it the first time the knob
was turned all the way up. Taking the reference from the same rail the converter
uses also makes the reading ratiometric, so supply droop moves both and the
brightness does not wander.

On this board that rail is the pin whose silkscreen reads `CV3` — measured at
3.3 V, so the label is a typo and the pin is the one to use. See
[`hardware/`](hardware/).

The other fix is one component: **100 nF from the ADC pin to GND**, plus
multisampling in firmware. The capacitor becomes the charge reservoir the converter samples
from, and the pot only has to keep it topped up. Worst case is mid-rotation at
roughly 125 kΩ, giving a time constant near 13 ms — imperceptible on a knob, and
far better than the alternative.

**The `A` means a logarithmic taper**, which is right for a brightness control —
perception is roughly logarithmic too. But the output chain already applies
gamma, so a log pot feeding a gamma curve compensates twice and will feel dead
across most of its travel. The knob response therefore needs to be a **remotely
adjustable curve** rather than a compiled-in assumption, like everything else
tied to physical wiring.

## The power switch must not carry the load

The push-pull switch currently sits in the battery line and carries the whole
LED current. That was survivable when the strips were fed from a Nano's
regulator and drew a few hundred milliamps. It is not survivable at the 2.5 A
the rebuild can pull from the pack: the switch on a guitar pot is a small-signal
part, and welding its contacts closed would be a failure with no obvious cause
and no way back except opening the guitar.

**Use it as a signal, not as a conductor.** Almost every buck converter module
has an enable pin; run the push-pull to that instead, and the switch carries
milliamps while the converter carries the amps. The control behaves identically
and the part is no longer operating outside its rating.

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
- Feed GPIO12 into pin 2 and GPIO13 into pin 5; take strip data from pins 3
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

## The run up the neck

The original wiring did not survive disassembly, so this is a specification
rather than something inherited — which is the better position to be in, since
the old run was sized for whatever a Nano's regulator could deliver.

**Four conductors, not six.** Both strips share one 5 V and one ground, with a
data line each. The ground has to be common anyway, because WS2812 data is
referenced to it, and sharing the positive costs nothing: for a given amount of
copper, one conductor of twice the area carrying twice the current drops exactly
the same voltage as two separate ones. Two fewer wires to route through the
channel, and the pair can be thicker.

| conductor | gauge | why |
|---|---|---|
| 5 V | 22 AWG, 20 AWG if it fits | 3.1 A worst case over ~0.5 m is 0.08 V at 22 AWG, half that at 20 |
| GND | same as 5 V | carries the same current back |
| data ×2 | 26–28 AWG | carries no current worth the name |

Silicone insulation rather than PVC: thinner wall for the same conductor, and far
more flexible in a routed channel.

**Put a connector in the neck pocket.** The neck comes off with four bolts; it
should not also require a soldering iron. A 6-way JST-XH with the 5 V and ground
doubled up across two pins each keeps every contact inside its rating without
relying on the software brightness ceiling to stay there.

**Strain-relieve both ends.** The joints that failed here failed because nothing
held the wire except the solder, and solder is not a mechanical fixing. A cable
tie anchored to something solid, or a blob of hot glue over the joint, costs
nothing at build time and is the difference between this happening again and not.

## Power

Worst case is **every LED at full white: 3.1 A at 5 V**, about 15.6 W, which
through a buck converter is roughly **2.5 A from a 7.2 V pack**. (LED counts and
every other measurement: [`hardware/`](hardware/).)

- **Fit a 5 A fuse in the pack's positive line.** Not 3 A, which an earlier
  revision of this file called for: the converter draws its *most* current from
  the pack when the pack is *flattest*, because it takes the same power from a
  lower voltage. At 6.0 V that is 2.95 A — close enough to a 3 A fuse to blow it
  eventually, and it would blow on stage at high brightness on a tired pack,
  which is both the worst moment and the hardest fault to diagnose.

  | pack | drawn from the pack |
  |---|---|
  | 8.4 V charged | 2.11 A |
  | 7.2 V nominal | 2.46 A |
  | 6.0 V flat | **2.95 A** |

  5 A leaves real headroom over normal operation and still clears a short in
  milliseconds — a fault through six NiMH cells is tens of amps, not five. Six
  NiMH cells in series will deliver that without complaint;
  they are a low-impedance source in a wooden box with wiring running the length
  of a neck. This was worth doing anyway, and the near-short described in
  [`hardware/`](hardware/) makes the case concrete: nothing in the instrument
  currently limits fault current at all. It costs well under a euro and it is
  the only part here whose entire job is to fail.
- **Wire the pack as six cells in series.** The holder is currently two parallel
  banks of three, achieved by inserting three cells backwards — see
  [`hardware/`](hardware/) for why that arrangement has to go regardless. Both arrangements store the same 14.4 Wh with NiMH in them, so this is
  not about capacity — it is about current:

  | pack | nominal | converter | current drawn from the pack |
  |---|---|---|---|
  | six in series | 7.2 V | buck | 2.5 A |
  | three in series, twice | 3.6 V | boost | 4.9 A |

  The series pack moves the same energy at half the current, which is a quarter
  of the resistive loss in the cells, the contacts and the wiring — and those
  contacts were already flagged as the weak point. It also lets a buck do the
  work, which is simpler and more efficient than boosting, and it keeps the
  input comfortably above 5 V at every state of charge: 8.4 V charged, 6.0 V
  flat.

  There is a second reason with NiMH specifically. Parallel banks of cells
  cross-charge each other when they drift apart in state of charge, which
  matters far more for rechargeables than for alkalines. A single series string
  cannot do that.

  If the holder proves impossible to rewire, a replacement 6×AA holder in the
  same footprint is the fallback — but check the footprint before buying, since
  this one is flush-mounted into a routed cavity.
- Size the buck for the worst case with headroom. A 3 A module is not enough;
  many cheap ones cannot hold 3 A in practice. A synchronous 5 A part is the
  comfortable choice.
- **AA holder spring contacts are a real series resistance at 2.5 A.** So are
  thin wires up the neck. Cheap holders sag noticeably under load, and a sagging
  supply is indistinguishable from a firmware fault when you cannot see a serial
  port.
- NiMH rather than alkaline, and this is now measured rather than asserted: at
  0.15–0.3 Ω per cell, three alkalines in series lose around 2 V inside the cells
  at 3 A. NiMH sits nearer 0.03–0.05 Ω per cell, an order of magnitude better,
  and it is why the old build's brightness collapsed exactly when an effect asked
  for the most light.
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

### The update button cannot install itself

The page is embedded in the firmware. So is the button that installs firmware.
A board running an image from before that button existed serves a page without
it, and the only way to get the button is the update it would have performed.

This is not a design flaw so much as a fact about self-hosted control surfaces,
and it recurs: **any control added to the page is unreachable on every board
flashed before it.** The escape is always one update performed some other way.

Three ways out, cheapest first:

1. **iOS Shortcuts.** `Get Contents of URL`, method `POST`, request body set to
   the firmware file from Files, pointed at `http://192.168.4.1/api/ota`. No
   computer, and it works against any firmware from step 2 onwards because the
   endpoint is older than the button.
2. **`curl` from any computer**, which is what the first over-the-air update was
   done with.
3. **Reflash over USB** from the web flasher, which needs a computer and a cable
   but is immune to whatever state the board is in.

**After one such update the loop is broken for good**, because the board is then
serving a page that can install the next one.

The same reasoning is why `/api/wifi` mattered more than it looked: until the
page could call it, the only way to reach the instrument was its own access
point, and a phone on that access point cannot see the internet. Downloading an
image and installing it were therefore two different networks. Putting the
guitar on a real network once collapses that back into one.

### Switch the guitar off before plugging that cable in

The seller's own documentation says it outright: **do not connect USB power and
external power at the same time.** See `board-datasheet-power.jpg` in
[`hardware/`](hardware/). With a pigtail routed into the cavity, that stops
being a bench warning and becomes an operating rule for the finished instrument,
because both supplies are permanently wired.

The design already makes this safe to obey, which is worth noting because it was
not designed for this. The push-pull switch drives the converter's **enable**
pin rather than carrying the load, so switching the guitar off shuts the
converter down and takes 5 V off the rail entirely — leaving USB as the only
source. The rule is therefore just: **pull the knob out before the cable goes
in.** The strips stay dark, which is correct: USB cannot feed them anyway.

This is also a reason not to accept a converter without an enable pin. Without
one, the switch has to break the 5 V rail itself, and then "off" depends on a
switch rated for several amps doing its job rather than on a logic pin.

## The two sessions

These were written as one session for a long time, and they are not. Separating
them is what makes the schedule survive an owner with no computer.

### Session A — flash the boards

**Needs:** a computer with Chrome, a USB-C cable that carries data, a desk.
**Does not need:** the guitar, the strips, the parts, a soldering iron, or any
software installed on that computer.

1. Open the published `flash.html`, click Install, pick the port.
2. Choose **Erase device** on a board that has never run this firmware.
3. **Do all three boards.** A spare is only a spare if it is ready to swap in.
4. Before the computer goes back: join the `electriclight` AP from the phone and
   confirm the control page loads. A failed flash is cheap to redo while the
   machine is still in the room and expensive afterwards.

Twenty minutes, and it can happen opportunistically the next time any laptop is
nearby. Everything the firmware does after this — effects, settings, geometry,
WiFi credentials, the web UI itself — arrives over the air.

**Done.** What it established, in the order it mattered:

- The board flashes from the web page, and comes up as its own access point with
  no credentials stored. It marks itself healthy in that state, so a board on a
  bench does not walk into safe mode after three power cycles.
- **The over-the-air path works**, tested by pushing the same image back at the
  device over its own AP. That is the assumption the whole project rests on and
  it had never been exercised.
- **The evaluator reproduces all 104 golden frames on the silicon**, with
  newlib's libm rather than CI's. Step 4's contract, proven where it counts.
- **It renders, and an effect pushed from the phone appears on the LED.** A
  frame costs 6.28 ms of the 16.7 ms available; the figure and what it implies
  are in [`decisions.md`](decisions.md).

Still unconfirmed on hardware: that pushed effects survive a power cycle. The
code stores them in NVS and reloads them at boot, and nothing has yet watched
that happen. It costs one power cycle to find out, and assembly involves rather
a lot of those.

Three things that only a real boot could have told us, all now fixed: the
self-test never ran on a cable-flashed image, it starved the idle task for
twenty seconds and tripped the task watchdog, and the page's instructions
skipped the RESET that leaves the ROM loader after flashing.

### Session B — assemble and validate the guitar

**Needs:** the parts, a soldering iron, a multimeter, and the guitar open.
**Does not need a computer at all**, provided Session A already happened: a
flashed board is reachable from the phone over its own access point.

In order, and the order matters:

1. **Validate the hardware with known-good third-party firmware first** — WLED
   or similar. Power, wiring, strip type, LED count and direction all get
   confirmed before any custom code is in the picture. Debugging your own
   firmware against unverified wiring is the slowest possible loop. This is the
   one step that puts a cable back in the picture; do it on a board that is not
   one of the three, or reflash afterwards from the same web page.
2. Check supply voltage **under load**, not at idle, at the far end of the neck.
3. Confirm both strips independently, and confirm the data direction — index 0
   is expected at the *last fret*, not the nut.
4. **Verify OTA works on the bench, before closing the guitar.** An OTA path
   that has never been exercised is not a path.
5. Verify safe mode entry by its physical gesture, also before closing.
6. Run `/api/selftest` from the phone once, so the evaluator is known to agree
   with the simulator on this silicon before the instrument is trusted on stage.

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
