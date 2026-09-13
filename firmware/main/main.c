// electriclight firmware.
//
// Step 2 of the sequence in AGENTS.md: boot, serve the control page, and accept
// a firmware update over the air - including surviving a bad one. There is no
// LED output and no effect evaluator yet; those are steps 3 and 4.
//
// The order below is deliberate. Logging is captured before anything can fail,
// configuration is read before the radio needs it, and the running image is
// only confirmed good once it has proved it can be reached.

#include "app_config.h"
#include "app_http.h"
#include "app_identity.h"
#include "app_log.h"
#include "app_ota.h"
#include "app_wifi.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

void app_main(void)
{
    app_log_init();

    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "electriclight %s (build %s, idf %s) booting from %s",
             ELECTRICLIGHT_VERSION,
             desc ? desc->version : "?",
             desc ? desc->idf_ver : "?",
             app_ota_running_slot());

    if (app_ota_pending_verify()) {
        ESP_LOGW(TAG, "this image is on probation and will roll back unless it "
                      "can reach the network");
    }

    ESP_ERROR_CHECK(app_config_init());
    ESP_ERROR_CHECK(app_wifi_start());

    if (app_wifi_has_ip()) {
        ESP_ERROR_CHECK(app_http_start());
    } else {
        ESP_LOGW(TAG, "radio is off, so nothing is being served");
    }

    // Give the network a moment to settle before deciding whether this image
    // deserves to be kept.
    vTaskDelay(pdMS_TO_TICKS(5000));
    app_ota_confirm_if_healthy();

    ESP_LOGI(TAG, "ready");
}
