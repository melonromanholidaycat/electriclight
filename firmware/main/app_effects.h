#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// What the guitar plays, and how it remembers it.
//
// The page compiles effects; the firmware never does. What arrives here is
// already bytecode, in the wire format web/src/lang/serialize.js writes, so the
// expression compiler stays in the browser where it can be changed without a
// firmware build. This module's whole job is to accept that, check it, hand it
// to the render loop, and put it somewhere it survives a power cycle.
//
//   POST /api/effects   { version, output, slots: [ {name, program, params} ] }
//   GET  /api/effects   the same document back
//
// `program` is base64 of an ELFX blob. All five slots must be present.
// Output settings are validated against the board policy and stored in full.

// Generous against a real payload of two or three kilobytes, and small enough
// that a malformed upload cannot exhaust the heap.
#define APP_EFFECTS_MAX_JSON 8192

// Applies a payload: decodes every program before changing anything, so a
// document with one bad slot in it leaves the guitar playing what it was.
// On success the payload is stored and will be reloaded at the next boot.
// `err_out` receives a short human-readable reason on failure.
esp_err_t app_effects_apply(const char *json, size_t len, char *err_out, size_t err_max);

// The stored payload, or NULL if none has ever been accepted. Owned by this
// module; valid until the next apply.
const char *app_effects_stored(size_t *len_out);

// Loads whatever was stored and applies it. Called once at boot. Returns
// ESP_ERR_NOT_FOUND when nothing has been stored, which is not an error - it
// means the board is still playing its built-in defaults.
esp_err_t app_effects_restore(void);
