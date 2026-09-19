#include "app_selftest.h"

#include <inttypes.h>
#include <string.h>

#include "el_selftest.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "selftest";

static app_selftest_t s_result = {
    .ran = false,
    .ok = false,
    .ms = 0,
    .summary = "not run",
};

const app_selftest_t *app_selftest_run(void)
{
    el_selftest_result_t r;
    const int64_t t0 = esp_timer_get_time();
    const bool ok = el_selftest(&r);
    const int64_t us = esp_timer_get_time() - t0;

    s_result.ran = true;
    s_result.ok = ok;
    s_result.ms = (uint32_t)(us / 1000);
    el_selftest_describe(&r, s_result.summary, sizeof(s_result.summary));

    if (ok) {
        ESP_LOGI(TAG, "evaluator matches the simulator: %s (%" PRIu32 " ms)",
                 s_result.summary, s_result.ms);
    } else {
        // Worth shouting about. Every effect the owner designs in the browser
        // is trusted to look the same here; if that stops being true, the
        // simulator has quietly become a liar and the log is the only place
        // that will say so.
        ESP_LOGE(TAG, "evaluator DISAGREES with the simulator: %s", s_result.summary);
    }
    return &s_result;
}

const app_selftest_t *app_selftest_get(void) { return &s_result; }
