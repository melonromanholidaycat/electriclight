#include "app_wifi.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_identity.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mdns.h"

static const char *TAG = "wifi";

#define GOT_IP    BIT0
#define STA_FAILED BIT1
#define STA_ATTEMPTS 5

static EventGroupHandle_t s_events;
static app_wifi_mode_t s_mode;
static int s_retries;
static esp_netif_ip_info_t s_ip;
static esp_netif_t *s_sta_netif;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries < STA_ATTEMPTS) {
            s_retries++;
            ESP_LOGW(TAG, "station disconnected, retry %d/%d", s_retries, STA_ATTEMPTS);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, STA_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *got = (ip_event_got_ip_t *)data;
        s_ip = got->ip_info;
        s_retries = 0;
        xEventGroupSetBits(s_events, GOT_IP);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "a device joined the fallback access point");
    }
}

static void start_mdns(void)
{
    const app_config_t *cfg = app_config_get();
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return;
    }
    mdns_hostname_set(cfg->name);
    mdns_instance_name_set("electriclight guitar");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "reachable at http://%s.local/", cfg->name);
}

static esp_err_t start_fallback_ap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, ELECTRICLIGHT_AP_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(ELECTRICLIGHT_AP_SSID);
    strlcpy((char *)ap.ap.password, ELECTRICLIGHT_AP_PASSWORD, sizeof(ap.ap.password));
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.channel = 6;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_mode = APP_WIFI_FALLBACK;
    ESP_LOGI(TAG, "fallback access point \"%s\" is up", ELECTRICLIGHT_AP_SSID);
    return ESP_OK;
}

esp_err_t app_wifi_start(el_radio_mode_t mode)
{
    const app_config_t *cfg = app_config_get();

    if (mode == EL_RADIO_OFF) {
        ESP_LOGI(TAG, "radio off; no gesture was performed");
        s_mode = APP_WIFI_DOWN;
        return ESP_OK;
    }

    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL));

    // Safe mode skips this entirely. Stored credentials are exactly the kind of
    // thing that strands a closed guitar, so the rescue path must not depend on
    // them being right.
    if (mode == EL_RADIO_SAFE && cfg->sta_ssid[0]) {
        ESP_LOGW(TAG, "safe mode: ignoring stored credentials for %s", cfg->sta_ssid);
    }

    if (mode != EL_RADIO_SAFE && cfg->sta_ssid[0]) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        wifi_config_t sta = {0};
        strlcpy((char *)sta.sta.ssid, cfg->sta_ssid, sizeof(sta.sta.ssid));
        strlcpy((char *)sta.sta.password, cfg->sta_pass, sizeof(sta.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "joining %s", cfg->sta_ssid);
        EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP | STA_FAILED, pdFALSE, pdFALSE,
                                               pdMS_TO_TICKS(20000));
        if (bits & GOT_IP) {
            s_mode = APP_WIFI_STATION;
            ESP_LOGI(TAG, "joined %s as " IPSTR, cfg->sta_ssid, IP2STR(&s_ip.ip));
            start_mdns();
            return ESP_OK;
        }

        // Never leave the guitar unreachable because a network moved or a
        // password changed. Fall back rather than sit there retrying.
        ESP_LOGW(TAG, "could not join %s, falling back to our own access point", cfg->sta_ssid);
        ESP_ERROR_CHECK(esp_wifi_stop());
        if (s_sta_netif) {
            esp_netif_destroy_default_wifi(s_sta_netif);
            s_sta_netif = NULL;
        }
    }

    esp_err_t err = start_fallback_ap();
    if (err == ESP_OK) start_mdns();
    return err;
}

app_wifi_mode_t app_wifi_mode(void) { return s_mode; }

const char *app_wifi_mode_name(void)
{
    switch (s_mode) {
        case APP_WIFI_STATION:  return "station";
        case APP_WIFI_FALLBACK: return "fallback";
        default:                return "off";
    }
}

bool app_wifi_has_ip(void) { return s_mode != APP_WIFI_DOWN; }

void app_wifi_ip_string(char *out, size_t max)
{
    if (s_mode == APP_WIFI_STATION) {
        snprintf(out, max, IPSTR, IP2STR(&s_ip.ip));
    } else if (s_mode == APP_WIFI_FALLBACK) {
        snprintf(out, max, "192.168.4.1");
    } else if (max > 0) {
        out[0] = '\0';
    }
}
