#pragma once

// What the guitar calls itself, and how to get back into it when everything
// else has failed.
//
// ELECTRICLIGHT_AP_PASSWORD is committed deliberately. It is a deterrent, not a
// secret: this repository is public, so anyone can read it. The reasoning is in
// docs/decisions.md - losing access to a closed guitar is a far worse outcome
// than a stranger in radio range changing the lights, and a password that only
// exists in someone's memory is a password that will be lost.
#define ELECTRICLIGHT_DEVICE_ID   "electriclight"
#define ELECTRICLIGHT_AP_SSID     "electriclight"
#define ELECTRICLIGHT_AP_PASSWORD "electric-light"

// Bumped by hand. Shown in /api/status so a phone can tell which image is
// actually running, which is the only way to confirm an OTA landed.
#define ELECTRICLIGHT_VERSION "0.1.0"
