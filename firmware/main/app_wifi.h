#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "el_bootstate.h"
#include "esp_err.h"

// The brief requires the radio off unless deliberately enabled, and also
// requires the guitar to stay reachable. Those only coexist if there is a way
// to turn the radio on that does not depend on stored configuration being
// correct - which is what the gesture and the boot-loop rescue are for. The
// decision is made in app_mode; this file only carries it out.

typedef enum {
    APP_WIFI_DOWN = 0,
    APP_WIFI_STATION,   // joined a known network
    APP_WIFI_FALLBACK,  // serving its own access point
} app_wifi_mode_t;

// EL_RADIO_OFF does nothing. EL_RADIO_ON joins a stored network and falls back
// to our own access point. EL_RADIO_SAFE goes straight to the fallback,
// ignoring stored credentials entirely - because bad stored credentials are one
// of the things safe mode exists to escape.
esp_err_t app_wifi_start(el_radio_mode_t mode);
app_wifi_mode_t app_wifi_mode(void);
const char *app_wifi_mode_name(void);
bool app_wifi_has_ip(void);
void app_wifi_ip_string(char *out, size_t max);
