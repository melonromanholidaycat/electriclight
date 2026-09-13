#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// Updates over the air, and the half of OTA that matters more: surviving a bad
// one. A new image boots in PENDING_VERIFY. If it fails to prove itself the
// bootloader reverts to the previous slot on the next restart, with no help
// from a phone and no cable.

typedef struct app_ota_session app_ota_session_t;

// True when the running image has not yet been confirmed good.
bool app_ota_pending_verify(void);

// Runs the health check and confirms or rejects the running image. Called once,
// after the network and the HTTP server are up, since that is what "healthy"
// means for this device: reachable.
void app_ota_confirm_if_healthy(void);

esp_err_t app_ota_begin(app_ota_session_t **out);
esp_err_t app_ota_write(app_ota_session_t *s, const void *data, size_t len);
esp_err_t app_ota_finish(app_ota_session_t *s);
void app_ota_abort(app_ota_session_t *s);

const char *app_ota_running_slot(void);
