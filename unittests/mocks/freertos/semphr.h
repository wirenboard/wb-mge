#pragma once

#include "FreeRTOS.h"
#include <stdbool.h>

typedef void *SemaphoreHandle_t;

extern bool mock_xSemaphoreCreateMutex_should_fail;
extern int mock_xSemaphoreCreateMutex_called;
extern int mock_xSemaphoreTake_called;
extern int mock_xSemaphoreGive_called;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);

void mock_xSemaphore_reset(void);
