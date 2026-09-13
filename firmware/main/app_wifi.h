#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// The brief requires the radio off unless deliberately enabled, and also
// requires the guitar to stay reachable. Those only coexist if there is a way
// to turn the radio on that does not depend on stored configuration.
//
// Step 2 has no hardware to read a gesture from, so the radio follows the
// stored `radio_on_boot` setting, which defaults on. The physical boot gesture
// arrives in step 3, once the pot and five-way wiring is known. Until then this
// firmware is deliberately more reachable and less discreet than the finished
// instrument will be.

typedef enum {
    APP_WIFI_DOWN = 0,
    APP_WIFI_STATION,   // joined a known network
    APP_WIFI_FALLBACK,  // serving its own access point
} app_wifi_mode_t;

esp_err_t app_wifi_start(void);
app_wifi_mode_t app_wifi_mode(void);
const char *app_wifi_mode_name(void);
bool app_wifi_has_ip(void);
void app_wifi_ip_string(char *out, size_t max);
