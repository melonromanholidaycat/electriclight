#include "app_log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// 8 KB holds a few minutes of ordinary logging, and comfortably more of the
// only kind that matters: whatever was printed just before something broke.
#define LOG_CAPACITY 8192

static char s_buf[LOG_CAPACITY];
static size_t s_head;      // next write position
static bool s_wrapped;
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
    s_chain = esp_log_set_vprintf(capture);
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
