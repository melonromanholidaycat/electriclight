#pragma once

// The pin map from docs/firmware-plan.md. Provisional until the guitar is
// rewired: nothing is connected to these yet, and the firmware is built to
// behave sensibly when nothing is.
//
// The three analogue inputs must stay on ADC1 (GPIO1-10). ADC2 stops working
// the moment WiFi is up, and the resulting fault looks exactly like bad wiring.

#define PIN_LED_BASS    15
#define PIN_LED_TREBLE  16

#define PIN_POT          1   // ADC1_CH0
#define PIN_BATTERY      2   // ADC1_CH1

// One conductor per switch position, common to ground, read with internal
// pull-ups. Exactly one low at rest; none low while the switch is turning.
#define PIN_SWITCH_0     4
#define PIN_SWITCH_1     5
#define PIN_SWITCH_2     6
#define PIN_SWITCH_3     7
#define PIN_SWITCH_4     8

// Reserved for a microphone that may never be fitted: 17, 18, 21.
// Spare: 38.
