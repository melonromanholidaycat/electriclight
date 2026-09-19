#pragma once

// The pin map, read off the board rather than off a published reference.
// Photographs in docs/hardware/. Nothing is connected to these yet, and the
// firmware is built to behave sensibly when nothing is.
//
// This board (HW-747 V0.0.2 "Super Mini") brings out GPIO1-13 on its headers,
// plus RX/TX, 3V3, GND and 5V. It does NOT bring out 14-21 or 33-48, which an
// earlier version of this file assumed. GPIO3 is a strapping pin and is left
// alone, leaving twelve usable.
//
// GPIO1-10 are ADC1, the only converter that keeps working once WiFi is up.
// GPIO11-13 are ADC2 only, which makes them useless for analogue here - so
// they take the digital jobs, and the scarce ADC1 pins stay free.

#define PIN_LED_BASS    12   // ADC2-only, so nothing analogue is given up
#define PIN_LED_TREBLE  13

#define PIN_POT          1   // ADC1_CH0
#define PIN_BATTERY      2   // ADC1_CH1

// One conductor per switch position, common to ground, read with internal
// pull-ups. Exactly one low at rest; none low while the switch is turning.
#define PIN_SWITCH_0     4
#define PIN_SWITCH_1     5
#define PIN_SWITCH_2     6
#define PIN_SWITCH_3     7
#define PIN_SWITCH_4     8

// Spare: 9, 10 (both ADC1) and 11 (ADC2 only).
// Avoid: 3, strapping.
// Do not use the B+/B- pads on the underside: those are a single-cell LiPo
// charger input, not a place to attach the pack. 5 V goes to the 5V pin.

// The on-board WS2812, not on any header. Worth more than it looks: the effect
// evaluator and the LED driver can be brought up against the golden vectors on
// a bare board, one pixel at a time, with no guitar and no cabled session.
#define PIN_ONBOARD_LED 48
