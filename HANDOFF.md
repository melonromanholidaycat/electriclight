# Handoff

**2026-09-26. Branch `claude/led-guitar-brief-wrjr81` is the default branch.**
Read [`AGENTS.md`](AGENTS.md) for the brief and working relationship.

## Current work

The September 26 review corrections add validated/atomic effect and output
uploads, recovery boots that bypass stored effects, battery monitoring with an
explicit setup control, runtime WiFi fallback, and phone-operated wiring tests.
They also fix the physical switch restarting animations every frame.
Implementation and physical limitations:
[`docs/firmware-plan.md`](docs/firmware-plan.md#september-26-firmware-corrections).

Three boards were flashed previously. The owner reports successful WiFi firmware
uploads from the iPhone. **No desktop is available.** The new corrections still
need an OTA upload and physical checks; do not describe them as proven on the
guitar. Host tests and CI cover software, not solder joints or ADC accuracy.

## Waiting on the owner

- Install the new 4 MB image over WiFi; CI has built and published it, then reload
  the control page. Leave Always enable WiFi on until the physical controls work.
- Assemble and validate per Session B in the firmware plan. Its obsolete cable
  step has been replaced with the on-device wiring patterns.
- The owner reports the battery holder restored to six-in-series with the old
  jumpers removed. Check the two output lugs for correct polarity and pack
  voltage with a meter before connecting the Pololu converter. Parts remain as
  indexed by [`docs/shopping-list.md`](docs/shopping-list.md).
- **Battery monitoring stays disabled until the sensing circuit is fitted and
  meter-checked.** Sense-feed isolation with the ESP32 unpowered is still a
  physical design question; see the firmware plan before wiring GPIO2.
- Witness saved effects surviving a power cycle, physical safe-mode entry,
  every switch position animating, pickup noise, and OTA after assembly.

The known geometry limitation and possible layering work remain in
[`docs/decisions.md`](docs/decisions.md). Audio and orientation remain deferred.
