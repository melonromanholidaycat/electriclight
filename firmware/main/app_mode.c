#include "app_mode.h"

#include "app_config.h"
#include "app_inputs.h"
#include "el_gesture.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mode";

// Below this the knob counts as wound down, which turns the gesture into a
// request for safe mode. Generous, because a 500 kohm pot does not read a
// clean zero at the end of its travel.
#define BRIGHTNESS_LOW 0.05f

#define POLL_MS 20

static el_radio_mode_t s_radio;
static el_boot_mode_t s_boot;
static uint8_t s_count;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

el_radio_mode_t app_mode_decide(void)
{
    // Count this boot before anything that could crash. If the firmware never
    // reaches app_mode_mark_healthy(), the count survives to the next boot -
    // which is the whole mechanism.
    s_count = el_bootstate_next(app_config_get()->boot_count);
    app_config_set_boot_count(s_count);
    s_boot = el_bootstate_mode(s_count, EL_BOOTLOOP_THRESHOLD);

    if (s_boot == EL_BOOT_SAFE) {
        ESP_LOGW(TAG, "%u boots without reaching a healthy state; entering safe mode",
                 (unsigned)s_count);
        s_radio = el_radio_decide(s_boot, false, false, false);
        return s_radio;
    }

    const uint32_t window = app_config_get()->gesture_window_ms;
    app_inputs_t in;
    el_gesture_t gesture;
    el_gesture_begin(&gesture, now_ms(), window);

    // Sampled at the start rather than at the end: the knob is what the player
    // set before switching on, and it may well get nudged during the sweep.
    app_inputs_read(&in);
    bool brightness_low = in.brightness < BRIGHTNESS_LOW;
    if (in.switch_fault) {
        ESP_LOGW(TAG, "more than one five-way contact closed; check the wiring");
    }

    if (window > 0) {
        ESP_LOGI(TAG, "watching the five-way for %u ms", (unsigned)window);
        while (el_gesture_window_open(&gesture, now_ms())) {
            app_inputs_read(&in);
            el_gesture_update(&gesture, now_ms(), in.position);
            if (el_gesture_detected(&gesture)) break;
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        }
    }

    const bool seen = el_gesture_detected(&gesture);
    s_radio = el_radio_decide(s_boot, seen, brightness_low,
                              app_config_get()->radio_always_on);

    ESP_LOGI(TAG, "gesture %s, brightness %s, radio %s",
             seen ? "seen" : "not seen",
             brightness_low ? "down" : "up",
             el_radio_name(s_radio));
    return s_radio;
}

void app_mode_mark_healthy(void)
{
    if (s_count == 0) return;
    s_count = el_bootstate_on_healthy();
    app_config_set_boot_count(s_count);
    ESP_LOGI(TAG, "this image reached a healthy state");
}

el_radio_mode_t app_mode_current(void) { return s_radio; }
el_boot_mode_t app_mode_boot(void) { return s_boot; }
uint8_t app_mode_boot_count(void) { return s_count; }
