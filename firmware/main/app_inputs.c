#include "app_inputs.h"

#include "app_pins.h"
#include "driver/gpio.h"
#include "el_gesture.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "inputs";

static const int SWITCH_PINS[EL_SWITCH_POSITIONS] = {
    PIN_SWITCH_0, PIN_SWITCH_1, PIN_SWITCH_2, PIN_SWITCH_3, PIN_SWITCH_4,
};

static adc_oneshot_unit_handle_t s_adc;
static bool s_ready;

esp_err_t app_inputs_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < EL_SWITCH_POSITIONS; i++) mask |= 1ULL << SWITCH_PINS[i];

    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        // Pull-ups, switch common to ground. With nothing wired every pin reads
        // high, which is the same as "mid-sweep" - so an unwired board simply
        // never sees a gesture rather than misreading one.
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;

    adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1 };
    err = adc_oneshot_new_unit(&unit, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan = {
        .atten = ADC_ATTEN_DB_12,   // widest range; the top of the pot's travel clips slightly
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_CHANNEL_0, &chan)); // GPIO1, pot
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_CHANNEL_1, &chan)); // GPIO2, battery

    s_ready = true;
    ESP_LOGI(TAG, "pot on GPIO%d, five-way on GPIO%d-%d",
             PIN_POT, PIN_SWITCH_0, PIN_SWITCH_4);
    return ESP_OK;
}

// The pot is 500 kohm feeding a 100 nF capacitor. The capacitor is what makes
// the reading usable at all; multisampling takes care of what is left.
static float read_brightness(void)
{
    if (!s_ready) return 0.0f;
    int total = 0;
    int taken = 0;
    for (int i = 0; i < 16; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, ADC_CHANNEL_0, &raw) == ESP_OK) {
            total += raw;
            taken++;
        }
    }
    if (!taken) return 0.0f;
    float v = (float)total / (float)taken / 4095.0f;
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void app_inputs_read(app_inputs_t *out)
{
    if (!out) return;
    out->brightness = read_brightness();
    out->switch_fault = false;
    out->position = EL_SWITCH_IN_TRANSIT;

    int found = 0;
    for (int i = 0; i < EL_SWITCH_POSITIONS; i++) {
        if (gpio_get_level(SWITCH_PINS[i]) == 0) {
            out->position = i;
            found++;
        }
    }

    // Exactly one contact closed is a position. None is the switch in transit,
    // or nothing wired. Two at once cannot happen on a working rotary switch,
    // so it is a wiring fault and must not be reported as a position.
    if (found > 1) {
        out->position = EL_SWITCH_IN_TRANSIT;
        out->switch_fault = true;
    }
}
