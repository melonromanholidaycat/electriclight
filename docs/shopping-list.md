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
| Multimeter | |
| Soldering iron, solder, flux | assumed, since the guitar has already been apart |

## Still to buy

### The one real decision

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

### Power

| item | qty | spec | why |
|---|---|---|---|
| AA NiMH cells | 6, plus spares | low self-discharge | [hardware](hardware/) — alkalines sag, and the old pack proved it |
| Fuse + inline holder | 1 | **5 A** — not 3 A | nothing limits fault current today, and a flat pack draws 2.95 A; [firmware-plan, Power](firmware-plan.md) |
| Electrolytic capacitor | 1–2 | 1000 µF, 10 V or better | bulk near the strips |
| Ceramic capacitors | ~10 | 100 nF | decoupling, plus one on the pot's ADC pin |

### Wiring

| item | qty | spec | why |
|---|---|---|---|
| Silicone wire | a few metres | **22 AWG** (20 if it fits) | 5 V and GND up the neck |
| Silicone wire | a few metres | 26–28 AWG | two data lines, carry no current |
| JST-XH connector, 6-way | 1 pair + crimps | | neck comes off with four bolts, not a soldering iron |
| Heat shrink | assorted | | |

### Small parts

| item | qty | spec | why |
|---|---|---|---|
| Resistors | 2 | ~330 Ω | series on each LED data line, damps reflections |
| Perfboard | 1 small piece | | the DIP-14 and its passives need somewhere to sit |
| USB-C cable | 1 | **must carry data** | the board has no serial-converter chip, so the cable is the programmer |
| USB-C extension or panel mount | 1 | | routed into the cavity. Insurance now, not workflow: updates go over WiFi, so this is for the day a board will not boot |
| Foam tape or standoffs | 1 | to mount the board | keep the antenna edge (the red part at the front) clear of metal and the pack — [firmware-plan](firmware-plan.md) |

## Deliberately not buying

| | why |
|---|---|
| A replacement potentiometer | the A500K push-pull is the instrument's power switch. One 100 nF capacitor fixes the impedance — [firmware-plan](firmware-plan.md) |
| A replacement battery holder | the existing one is restored to six-in-series. Only if rewiring proves impossible, and measure the routed cavity first |
| LED strip | all 52 pixels verified working before teardown |
| A microphone | deferred, and not committed to |
| A replacement IMU | deferred. The fitted GY-61 is the wrong part, but nothing is built on it — [firmware-plan](firmware-plan.md) |
