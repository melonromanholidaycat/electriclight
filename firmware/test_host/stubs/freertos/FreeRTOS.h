#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY UINT32_MAX
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdPASS 1
#define pdFALSE 0
#define pdTRUE 1
#define configTICK_RATE_HZ 1000
#define pdMS_TO_TICKS(ms) (ms)
#define BIT0 1
#define BIT1 2
#define BIT2 4
#define BIT3 8
