#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "el_safety.h"

// Settings in NVS. Anything tied to physical wiring that might need tuning
// later belongs here rather than in a compile-time constant, because the guitar
// is hard to reach once it is closed.

#define APP_CFG_STR_MAX 64

typedef struct {
    char name[APP_CFG_STR_MAX];      // mDNS hostname, without ".local"
    char sta_ssid[APP_CFG_STR_MAX];  // empty means "never provisioned"
    char sta_pass[APP_CFG_STR_MAX];

    // Skips the gesture requirement entirely. Defaults on because the controls
    // are not wired yet and a bare board has no way to perform one. Turn it off
    // once the guitar can answer for itself - that is when the brief's "radio
    // off unless deliberately enabled" actually starts holding.
    bool radio_always_on;
    el_battery_config_t battery;

    // How long after start-up the five-way is watched. 0 disables the gesture.
    uint32_t gesture_window_ms;

    // Consecutive boots that never reached a healthy state. Persisted, because
    // the whole point is that it survives a firmware that cannot stay up.
    uint8_t boot_count;
} app_config_t;

esp_err_t app_config_init(void);
const app_config_t *app_config_get(void);
esp_err_t app_config_set_wifi(const char *ssid, const char *pass);
esp_err_t app_config_set_name(const char *name);
esp_err_t app_config_set_radio_always_on(bool on);
esp_err_t app_config_set_boot_count(uint8_t count);

esp_err_t app_config_set_battery(const el_battery_config_t *config);
