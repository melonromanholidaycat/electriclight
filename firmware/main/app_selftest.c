#include "app_selftest.h"

#include <inttypes.h>
#include <string.h>

#include "el_selftest.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "selftest";
static const char *NS = "electric";
static const char *KEY = "st_build";

// Enough of the ELF hash to identify a build. A collision would mean skipping
// one self-test on one board, which is not worth 32 bytes of NVS to prevent.
#define BUILD_TAG_BYTES 8

static app_selftest_t s_result = {
    .ran = false,
    .ok = false,
    .ms = 0,
    .summary = "not run",
};

// Twenty seconds of one core is long enough to starve the idle task and trip
// the task watchdog, which is exactly what the first run on real hardware did.
// A 1 ms delay every sixteen frames costs well under a second in total.
static void yield_to_rtos(void *ctx)
{
    (void)ctx;
    vTaskDelay(1);
}

static bool build_tag(uint8_t *out)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    if (!desc) return false;
    memcpy(out, desc->app_elf_sha256, BUILD_TAG_BYTES);
    return true;
}

bool app_selftest_is_new_build(void)
{
    uint8_t now[BUILD_TAG_BYTES];
    if (!build_tag(now)) return true; // cannot tell, so check

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return true;

    uint8_t seen[BUILD_TAG_BYTES];
    size_t len = sizeof seen;
    esp_err_t err = nvs_get_blob(h, KEY, seen, &len);
    nvs_close(h);

    if (err != ESP_OK || len != sizeof seen) return true;
    return memcmp(now, seen, sizeof seen) != 0;
}

static void remember_build(void)
{
    uint8_t now[BUILD_TAG_BYTES];
    if (!build_tag(now)) return;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, KEY, now, sizeof now) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

const app_selftest_t *app_selftest_run(void)
{
    el_selftest_result_t r;
    const int64_t t0 = esp_timer_get_time();
    const bool ok = el_selftest(&r, yield_to_rtos, NULL);
    const int64_t us = esp_timer_get_time() - t0;

    s_result.ran = true;
    s_result.ok = ok;
    s_result.ms = (uint32_t)(us / 1000);
    el_selftest_describe(&r, s_result.summary, sizeof(s_result.summary));

    if (ok) {
        ESP_LOGI(TAG, "evaluator matches the simulator: %s (%" PRIu32 " ms)",
                 s_result.summary, s_result.ms);
        // Only a pass is remembered. A build that failed gets re-checked on the
        // next boot rather than being quietly written off.
        remember_build();
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
