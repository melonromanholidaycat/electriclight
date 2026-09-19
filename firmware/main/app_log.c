#include "app_log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// The buffer lives in RTC memory and is not zeroed at start-up, so it survives
// a restart. That is the entire point: on a device with no serial port, the
// interesting log is the one written just before the firmware fell over, and an
// ordinary buffer loses exactly that.
//
// A power cycle still clears it - the push-pull cuts the supply - but a crash,
// a watchdog or a rollback does not, and those are the cases worth seeing.
// 4 KB rather than 8, because RTC slow memory is small and shared.
#define LOG_CAPACITY 4096
#define LOG_MAGIC 0x4C474F4Cu  // "LOGL"

RTC_NOINIT_ATTR static char s_buf[LOG_CAPACITY];
RTC_NOINIT_ATTR static uint32_t s_magic;
RTC_NOINIT_ATTR static size_t s_head;
RTC_NOINIT_ATTR static bool s_wrapped;
static SemaphoreHandle_t s_lock;
static vprintf_like_t s_chain;

static void append(const char *data, size_t len)
{
    if (len >= LOG_CAPACITY) {
        data += len - LOG_CAPACITY + 1;
        len = LOG_CAPACITY - 1;
    }
    size_t first = LOG_CAPACITY - s_head;
    if (first > len) first = len;
    memcpy(s_buf + s_head, data, first);
    if (len > first) {
        memcpy(s_buf, data + first, len - first);
        s_wrapped = true;
    }
    s_head = (s_head + len) % LOG_CAPACITY;
    if (s_head == 0) s_wrapped = true;
}

static int capture(const char *fmt, va_list args)
{
    char line[256];
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);

    if (n > 0 && s_lock && xSemaphoreTake(s_lock, 0) == pdTRUE) {
        append(line, (size_t)n < sizeof(line) - 1 ? (size_t)n : sizeof(line) - 1);
        xSemaphoreGive(s_lock);
    }

    // Still print to the console when one happens to be attached - during the
    // cabled session it is the fastest loop available.
    return s_chain ? s_chain(fmt, args) : 0;
}

void app_log_init(void)
{
    if (s_lock) return;
    s_lock = xSemaphoreCreateMutex();

    // Trust the surviving contents only if the magic and the head index are
    // both credible. Uninitialised RTC memory is arbitrary, and treating
    // arbitrary bytes as a log would be worse than having none.
    const bool survived = (s_magic == LOG_MAGIC && s_head < LOG_CAPACITY);
    if (!survived) {
        memset(s_buf, 0, sizeof(s_buf));
        s_head = 0;
        s_wrapped = false;
        s_magic = LOG_MAGIC;
    }

    s_chain = esp_log_set_vprintf(capture);

    if (survived) {
        // Everything above this line is from before the restart.
        ESP_LOGW("log", "---- restarted; the log above is from the previous run ----");
    }
}

size_t app_log_read(char *out, size_t max)
{
    if (!out || max == 0) return 0;
    if (!s_lock) { out[0] = '\0'; return 0; }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t len = 0;
    if (s_wrapped) {
        size_t tail = LOG_CAPACITY - s_head;
        size_t take = tail < max ? tail : max;
        memcpy(out, s_buf + s_head, take);
        len = take;
    }
    size_t room = max - len;
    size_t take = s_head < room ? s_head : room;
    memcpy(out + len, s_buf, take);
    len += take;
    xSemaphoreGive(s_lock);

    if (len < max) out[len] = '\0';
    return len;
}

void app_log_clear(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_head = 0;
    s_wrapped = false;
    xSemaphoreGive(s_lock);
}
