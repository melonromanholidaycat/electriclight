#pragma once
#include <stdbool.h>
#include "esp_err.h"

// The pot and the five-way. Both are read here and nowhere else, so the rest of
// the firmware never has to know that one is an ADC channel with a capacitor
// hanging off it and the other is five pins with pull-ups.

typedef struct {
    int position;      // 0..4, or EL_SWITCH_IN_TRANSIT while turning or unwired
    float brightness;  // 0..1
    bool switch_fault; // more than one contact closed: wiring, not a sweep
} app_inputs_t;

esp_err_t app_inputs_init(void);
void app_inputs_read(app_inputs_t *out);
