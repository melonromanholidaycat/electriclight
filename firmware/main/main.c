// electriclight firmware.
//
// Steps 2 and 3 of the sequence in AGENTS.md: boot, decide on what terms the
// radio comes up, serve the control page, accept an update over the air, and
// survive a bad one.
//
// The order below is deliberate and most of it is load-bearing. Logging is
// captured before anything can fail. The boot is counted before anything can
// crash, so that a firmware which cannot stay up still reaches a state someone
// can talk to. The running image is confirmed good only once it has proved it
// can be reached.

#include "app_config.h"
#include "app_http.h"
#include "app_identity.h"
#include "app_inputs.h"
#include "app_log.h"
#include "app_mode.h"
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
    ESP_ERROR_CHECK(app_inputs_init());

    // Counts this boot as unhealthy, then watches the five-way. Blocks for the
    // gesture window at most.
    const el_radio_mode_t radio = app_mode_decide();

    ESP_ERROR_CHECK(app_wifi_start(radio));

    if (app_wifi_has_ip()) {
        ESP_ERROR_CHECK(app_http_start());
    } else {
        ESP_LOGI(TAG, "radio is off, so nothing is being served");
    }

    // Give the network a moment to settle before deciding whether this image
    // deserves to be kept, and whether this boot counts as healthy.
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Reachable when it was meant to be. With the radio deliberately off there
    // is nothing to be reachable on, and refusing to call that healthy would
    // send a perfectly good instrument into safe mode for being discreet.
    const bool healthy = (radio == EL_RADIO_OFF) || app_wifi_has_ip();
    if (healthy) {
        app_mode_mark_healthy();
        app_ota_confirm_if_healthy();
    } else {
        ESP_LOGE(TAG, "the radio was meant to be up and is not; leaving this "
                      "boot counted against the rescue threshold");
    }

    ESP_LOGI(TAG, "ready");
}
