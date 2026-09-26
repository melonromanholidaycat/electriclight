#pragma once
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_ERR_INVALID_STATE 5
#define ESP_ERR_NOT_SUPPORTED 6
#define ESP_ERROR_CHECK(x) assert((x) == ESP_OK)
const char *esp_err_to_name(esp_err_t err);
