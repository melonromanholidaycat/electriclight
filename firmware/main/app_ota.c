#include "app_ota.h"

#include <stdlib.h>
#include <string.h>

#include "app_mode.h"
#include "app_wifi.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

struct app_ota_session {
    esp_ota_handle_t handle;
    const esp_partition_t *target;
    size_t written;
};

bool app_ota_pending_verify(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) return false;
    return state == ESP_OTA_IMG_PENDING_VERIFY;
}

void app_ota_confirm_if_healthy(void)
{
    if (!app_ota_pending_verify()) return;

    // "Healthy" for this device means reachable, or deliberately silent. An
    // image that boots but cannot be reached when it was supposed to be is
    // exactly as useless as one that does not boot, and far more annoying,
    // because nothing rolls it back automatically.
    if (app_wifi_has_ip() || app_mode_current() == EL_RADIO_OFF) {
        ESP_LOGI(TAG, "new image is reachable; confirming it good");
        esp_ota_mark_app_valid_cancel_rollback();
    } else {
        ESP_LOGE(TAG, "new image cannot reach the network; rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

esp_err_t app_ota_begin(app_ota_session_t **out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        ESP_LOGE(TAG, "no spare app partition; check the partition table");
        return ESP_ERR_NOT_FOUND;
    }

    app_ota_session_t *s = calloc(1, sizeof(*s));
    if (!s) return ESP_ERR_NO_MEM;

    esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &s->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        free(s);
        return err;
    }
    s->target = target;
    ESP_LOGI(TAG, "receiving an image into %s", target->label);
    *out = s;
    return ESP_OK;
}

esp_err_t app_ota_write(app_ota_session_t *s, const void *data, size_t len)
{
    if (!s) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_ota_write(s->handle, data, len);
    if (err != ESP_OK) return err;
    s->written += len;
    return ESP_OK;
}

esp_err_t app_ota_finish(app_ota_session_t *s)
{
    if (!s) return ESP_ERR_INVALID_STATE;

    esp_err_t err = esp_ota_end(s->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image rejected after %u bytes: %s",
                 (unsigned)s->written, esp_err_to_name(err));
        free(s);
        return err;
    }

    err = esp_ota_set_boot_partition(s->target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "accepted %u bytes into %s; it boots next and must then "
                      "prove itself", (unsigned)s->written, s->target->label);
    }
    free(s);
    return err;
}

void app_ota_abort(app_ota_session_t *s)
{
    if (!s) return;
    esp_ota_abort(s->handle);
    ESP_LOGW(TAG, "upload abandoned after %u bytes", (unsigned)s->written);
    free(s);
}

const char *app_ota_running_slot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    return running ? running->label : "?";
}
