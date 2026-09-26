#pragma once
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(void *task);
int xTaskCreatePinnedToCore(void (*task)(void *), const char *name, unsigned stack, void *arg, int priority, void *handle, int core);
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, int priority, void *handle);
