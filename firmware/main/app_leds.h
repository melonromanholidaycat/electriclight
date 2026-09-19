#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// The WS2812 output. Two strips on the neck, driven independently because they
// are separate runs and not daisy-chained, plus the board's own RGB LED.
//
// Written against the RMT peripheral directly rather than pulling in a managed
// component. It is about forty lines: a bytes encoder with the WS2812 bit
// timings, and one channel per strip. That is less code than the dependency's
// configuration would be, and it keeps the timing where it can be read.

typedef enum {
    EL_STRIP_BASS = 0,   // side 0, GPIO12
    EL_STRIP_TREBLE = 1, // side 1, GPIO13
    EL_STRIP_ONBOARD = 2,// the single RGB on the board itself, GPIO48
    EL_STRIP_COUNT,
} el_strip_t;

// leds_per_strip applies to the two neck strips; the on-board one is always 1.
esp_err_t app_leds_init(int leds_per_strip);

// rgb is 3 bytes per LED in the engine's order: side 0 first, then side 1, each
// in its strip's electrical order. Blocks until both strips have been written.
esp_err_t app_leds_write(const uint8_t *rgb, int total_leds);

// The board's own LED, for saying something on a bare board with no strips
// attached. Takes the same 0-255 range and the same gamma-corrected values the
// strips get, so it is dim enough to look at.
esp_err_t app_leds_onboard(uint8_t r, uint8_t g, uint8_t b);

// Everything off, and left off. Called before a reboot so the neck does not
// hold the last frame while the board restarts.
void app_leds_blank(void);
