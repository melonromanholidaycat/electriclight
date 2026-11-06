# Shopping list

Every part the rebuild needs, in one place.

**This file is an index, not a source of truth.** Each entry carries the minimum
needed to buy the right thing and a pointer to where the reasoning lives. Where
this file and a linked section disagree, **the linked section wins** — it is the
one that explains itself, and an unexplained number is the one more likely to be
stale.

---

## Have already

| | |
|---|---|
| 3 × ESP32-S3 Super Mini | `ESP32-S3FH4R2`, 4 MB. Pinout and why it fits: [firmware-plan](firmware-plan.md) |
| 10 × SN74AHCT125N, DIP-14 | Level shifter. **AHCT, not AHC** — [firmware-plan](firmware-plan.md) |
| AA NiMH cells | 6; low self-discharge |
| USB-C data cable | For board recovery |
| Foam tape / standoffs | To mount the board |
| Heat shrink | Assorted |
| Multimeter | |
| Soldering iron and flux | On hand or available for assembly; solder wire listed below to buy |
| Guitar audio volume potentiometer | B250K; received. Separate from the A500K LED pot |
| Electrolytic capacitor | Received; verify 1000 µF and at least 10 V before assembly |
| Ceramic capacitors | Received; verify two 100 nF (0.1 µF), at least 10 V, before assembly |
| Resistors | Received; verify two 330 Ω, 0.25 W before assembly |
| JST connectors | Received; optional neck disconnect. Verify six-way mating pair, contact size, and 22 AWG fit before use |

## Still to buy

### 5 V converter (selected, still to buy)

**A 5 V buck-boost converter**, and everything else on this list is cheap by
comparison.

| requirement | value |
|---|---|
| topology | **buck-boost**, not a plain buck |
| input | must cover **5 V and below** — a flat pack under load sags to about 4 V |
| output | 5.0 V |
| current | **3 A is ample** |
| **enable / shutdown pin** | **required** — the push-pull switch drives this rather than carrying the load |

This reverses an earlier entry that asked for a synchronous 5 A buck. That was
sized for every LED at full white, which the firmware's current limiter means
the instrument never reaches — and a plain buck browns out on a flat pack long
before it reaches its rating anyway, because the input falls below its own
output. The arithmetic is in [firmware-plan, Power](firmware-plan.md).

Check the physical size against the control cavity before buying. If the only
module you can find has no enable pin, say so before ordering — the workaround
is one MOSFET and two resistors, not a different converter.

**Selected, not yet ordered: Pololu S13V30F5.** Fixed 5 V, buck-boost, with a shutdown pin.

Three things to read off its product page rather than take from here, because
they decide how it gets wired and this file has been wrong about this part of
the design once already:

- **Which way the enable pin goes.** Pololu's regulators generally pull ENABLE
  up internally and shut down when it is driven low, which would mean the
  push-pull switch has to *close to ground* to turn the instrument off. Confirm
  it before soldering; getting it backwards makes the switch an on-switch.
- **Output current against input voltage.** A buck-boost delivers less at low
  input, and low input is exactly our worst case — a flat pack under load. The
  figure that matters is what it gives at around 5.5 V in, not its headline.
- **Quiescent draw in shutdown.** Switching on the enable pin means the pack
  stays connected to the converter's input permanently, so "off" is a sleep
  current rather than a disconnection. It should be tens of microamps, which is
  far below NiMH self-discharge and therefore irrelevant — but it is worth
  knowing that the fuse is now the only thing between a charged pack and a
  fault, at all times, including in the case.

### Power

| item | qty | spec | why |
|---|---|---|---|
| Fuse + inline holder | 1 each | **5 A MINI automotive blade fuse, 32 V DC**; matching insulated inline MINI holder rated at least 5 A DC, with 22 AWG or thicker leads | Install in the pack-positive wire close to the holder; [firmware-plan, Power](firmware-plan.md) |

### Wiring

| item | qty | spec | why |
|---|---|---|---|
| 22 AWG silicone wire | already owned | Use for 5 V, GND **and both data lines**, if it fits the neck channel | No additional 26–28 AWG wire is needed |

### Small parts

| item | qty | spec | why |
|---|---|---|---|
| Electronics solder wire | 1 small spool | 0.6–0.8 mm, flux-cored, for electronics (e.g. lead-free SAC305); no acid/plumbing solder | For the remaining joints; confirm you have adequate flux separately |
| Perfboard | 1 small piece | **2.54 mm (0.1 in) hole spacing**, large enough for a DIP-14 and wiring | Mount the SN74AHCT125N and its 100 nF bypass capacitor; a suitable DIP-14 breakout board also works |
| USB-C extension or panel mount | 1 | | routed into the cavity. Insurance now, not workflow: updates go over WiFi, so this is for the day a board will not boot |

## Deliberately not buying

| | why |
|---|---|
| A replacement LED brightness/power potentiometer | the existing A500K push-pull stays; one 100 nF capacitor fixes its ADC impedance — [firmware-plan](firmware-plan.md) |
| A replacement battery holder | the existing one is restored to six-in-series. Only if rewiring proves impossible, and measure the routed cavity first |
| LED strip | all 52 pixels verified working before teardown |
| A microphone | deferred, and not committed to |
| A replacement IMU | deferred. The fitted GY-61 is the wrong part, but nothing is built on it — [firmware-plan](firmware-plan.md) |
