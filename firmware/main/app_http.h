#pragma once
#include "esp_err.h"

// The control surface. Serves the same page the simulator publishes, embedded
// and pre-gzipped, plus the small API that page looks for to work out it is
// talking to a guitar rather than running standalone.

esp_err_t app_http_start(void);
