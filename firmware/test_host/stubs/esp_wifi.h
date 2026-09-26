#pragma once
#include "esp_err.h"
#define WIFI_MODE_AP 1
#define WIFI_MODE_STA 2
#define WIFI_IF_AP 1
#define WIFI_IF_STA 2
#define WIFI_AUTH_WPA2_PSK 1
#define WIFI_INIT_CONFIG_DEFAULT() {0}
typedef struct {int unused;} wifi_init_config_t;
typedef struct {
    struct {unsigned char ssid[32],password[64];int ssid_len,max_connection,authmode,channel;} ap;
    struct {unsigned char ssid[32],password[64];} sta;
} wifi_config_t;
esp_err_t esp_wifi_init(const wifi_init_config_t *c);
esp_err_t esp_wifi_set_mode(int m);
esp_err_t esp_wifi_set_config(int i,const wifi_config_t *c);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_connect(void);
