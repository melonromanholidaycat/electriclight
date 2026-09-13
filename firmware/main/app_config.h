#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// Settings in NVS. Anything tied to physical wiring that might need tuning
// later belongs here rather than in a compile-time constant, because the guitar
// is hard to reach once it is closed.

#define APP_CFG_STR_MAX 64

typedef struct {
    char name[APP_CFG_STR_MAX];      // mDNS hostname, without ".local"
    char sta_ssid[APP_CFG_STR_MAX];  // empty means "never provisioned"
    char sta_pass[APP_CFG_STR_MAX];
    bool radio_on_boot;              // see app_wifi.h for why this exists
} app_config_t;

esp_err_t app_config_init(void);
const app_config_t *app_config_get(void);
esp_err_t app_config_set_wifi(const char *ssid, const char *pass);
esp_err_t app_config_set_name(const char *name);
esp_err_t app_config_set_radio_on_boot(bool on);
