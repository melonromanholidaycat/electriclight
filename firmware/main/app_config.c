#include "app_config.h"

#include <string.h>

#include "app_identity.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config";
static const char *NS = "electric";

// Long enough to reach the switch and sweep it, short enough that the
// instrument is playable almost immediately. Remotely adjustable.
#define DEFAULT_GESTURE_WINDOW_MS 5000

static app_config_t s_cfg;

static void load_str(nvs_handle_t h, const char *key, char *dst, const char *fallback)
{
    size_t len = APP_CFG_STR_MAX;
    if (nvs_get_str(h, key, dst, &len) != ESP_OK) {
        strlcpy(dst, fallback, APP_CFG_STR_MAX);
    }
}

esp_err_t app_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unusable (%s), erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        load_str(h, "name", s_cfg.name, ELECTRICLIGHT_DEVICE_ID);
        load_str(h, "ssid", s_cfg.sta_ssid, "");
        load_str(h, "pass", s_cfg.sta_pass, "");
        uint8_t radio = 1;
        nvs_get_u8(h, "radio", &radio);
        s_cfg.radio_always_on = radio != 0;

        uint8_t boots = 0;
        nvs_get_u8(h, "boots", &boots);
        s_cfg.boot_count = boots;

        uint32_t window = DEFAULT_GESTURE_WINDOW_MS;
        nvs_get_u32(h, "gwindow", &window);
        s_cfg.gesture_window_ms = window;
        nvs_close(h);
    } else {
        strlcpy(s_cfg.name, ELECTRICLIGHT_DEVICE_ID, APP_CFG_STR_MAX);
        s_cfg.sta_ssid[0] = '\0';
        s_cfg.sta_pass[0] = '\0';
        s_cfg.radio_always_on = true;
        s_cfg.boot_count = 0;
        s_cfg.gesture_window_ms = DEFAULT_GESTURE_WINDOW_MS;
    }

    ESP_LOGI(TAG, "name=%s ssid=%s always_on=%d boots=%u window=%ums",
             s_cfg.name, s_cfg.sta_ssid[0] ? s_cfg.sta_ssid : "(none)",
             s_cfg.radio_always_on, (unsigned)s_cfg.boot_count,
             (unsigned)s_cfg.gesture_window_ms);
    return ESP_OK;
}

const app_config_t *app_config_get(void) { return &s_cfg; }

static esp_err_t store(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t app_config_set_wifi(const char *ssid, const char *pass)
{
    if (!ssid || !pass) return ESP_ERR_INVALID_ARG;
    esp_err_t err = store("ssid", ssid);
    if (err == ESP_OK) err = store("pass", pass);
    if (err != ESP_OK) return err;
    strlcpy(s_cfg.sta_ssid, ssid, APP_CFG_STR_MAX);
    strlcpy(s_cfg.sta_pass, pass, APP_CFG_STR_MAX);
    ESP_LOGI(TAG, "wifi credentials stored for %s", ssid);
    return ESP_OK;
}

esp_err_t app_config_set_name(const char *name)
{
    if (!name || !name[0]) return ESP_ERR_INVALID_ARG;
    esp_err_t err = store("name", name);
    if (err != ESP_OK) return err;
    strlcpy(s_cfg.name, name, APP_CFG_STR_MAX);
    return ESP_OK;
}

static esp_err_t store_u8(const char *key, uint8_t value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t app_config_set_radio_always_on(bool on)
{
    esp_err_t err = store_u8("radio", on ? 1 : 0);
    if (err == ESP_OK) s_cfg.radio_always_on = on;
    return err;
}

esp_err_t app_config_set_boot_count(uint8_t count)
{
    esp_err_t err = store_u8("boots", count);
    if (err == ESP_OK) s_cfg.boot_count = count;
    return err;
}
