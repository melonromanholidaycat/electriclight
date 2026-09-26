#pragma once
#include "FreeRTOS.h"
typedef unsigned EventBits_t;
typedef void *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t e,EventBits_t b);
EventBits_t xEventGroupClearBits(EventGroupHandle_t e,EventBits_t b);
EventBits_t xEventGroupGetBits(EventGroupHandle_t e);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t e,EventBits_t b,int clear,int all,TickType_t wait);
