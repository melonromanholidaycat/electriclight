# Handoff

**2026-09-23. Branch `claude/led-guitar-brief-wrjr81`, which is also the default
branch.**

The only file here that is **rewritten rather than appended to**. It carries
nothing durable — just where things stand and what is waiting on whom. Anything
that would still be true in a month belongs in one of the documents
[`README.md`](README.md) lists, and where this disagrees with one of those, that
one wins.

**Read [`AGENTS.md`](AGENTS.md) first.** It is the brief and the working
relationship, and nothing below makes sense without it.

---

## Where it stands

**Steps 1–5 of 7 done and verified on hardware.** Step 6 is assembly, which is
where the owner is now. What each step covered: [`AGENTS.md`](AGENTS.md). What
the hardware sessions established and what they did not:
[`docs/firmware-plan.md`](docs/firmware-plan.md), *The two sessions*.

| | |
|---|---|
| Three ESP32-S3 boards | flashed, running the image built from `b59b155f` |
| Everything committed since | **documentation only** — the boards are functionally current |
| Wired to a guitar | no. Nothing has been connected yet |

## Waiting on the owner

| | |
|---|---|
| Ordered | Pololu S13V30F5 converter. Three things to check when it arrives: [`docs/shopping-list.md`](docs/shopping-list.md) |
| To buy | the rest of [`docs/shopping-list.md`](docs/shopping-list.md) |
| To do | restore the battery holder to six-in-series — as it stands it is a short hazard, [`docs/hardware/`](docs/hardware/) |
| Then | Session B in [`docs/firmware-plan.md`](docs/firmware-plan.md). Needs parts and an iron, and no computer |

## Open, and where the reasoning is

- Neck geometry cannot be pushed from the page; output settings can — [`docs/decisions.md`](docs/decisions.md)
- Layering has room for one more layer inside the frame budget — [`docs/decisions.md`](docs/decisions.md)
- Pushed effects surviving a power cycle is still unwitnessed — [`docs/firmware-plan.md`](docs/firmware-plan.md)
- The pickup-noise design is written and untested — [`docs/firmware-plan.md`](docs/firmware-plan.md), *Keeping the LEDs out of the pickups*

## Read these before assuming anything

A reading list, not a second home for any of it. Each cost real time, and a
fresh session will otherwise rediscover them or repeat the advice that caused
them.

- Estimates of firmware behaviour were wrong three times before measurement
  settled it — [`docs/decisions.md`](docs/decisions.md), the literal-port and
  frame-cost entries
- The page is embedded in the firmware, so a control added to it is unreachable
  on every board flashed before it — [`docs/firmware-plan.md`](docs/firmware-plan.md),
  *The update button cannot install itself*
- A dark board reads as a failed flash, got wrong twice — the default effects and
  `app_leds_onboard` exist because of it
- The golden vectors tested the evaluator and not the output chain, so a build
  with the brightness ceiling on the wrong side of gamma passed —
  [`docs/decisions.md`](docs/decisions.md), *Golden vectors are the contract*
- The buck converter spec was wrong in both topology and rating —
  [`docs/firmware-plan.md`](docs/firmware-plan.md), *Power*
- Several guards passed for days while protecting nothing. **Break the thing a
  guard guards and watch it fail, every time.**
