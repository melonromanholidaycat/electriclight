#pragma once
#include <stddef.h>

// A log that only exists on a wire does not exist: there is no serial monitor
// on the finished instrument. Everything ESP_LOG writes is teed into a RAM ring
// buffer that the HTTP server can hand to a phone.

void app_log_init(void);

// Copies up to `max` bytes of the buffer, oldest first, into `out`.
// Returns the number of bytes written. NUL-terminates when there is room.
size_t app_log_read(char *out, size_t max);

void app_log_clear(void);
