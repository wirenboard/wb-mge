#pragma once

#include "FreeRTOS.h"
#include <stdbool.h>

#define MOCK_SEMAPHORE_HANDLE_T             ((SemaphoreHandle_t)0xBAADF00D)

typedef void *SemaphoreHandle_t;

extern int mock_xSemaphoreCreateMutex_called;
extern unsigned mock_xSemaphoreCreateMutex_call_seq;
extern SemaphoreHandle_t mock_xSemaphoreCreateMutex_return_value;

extern int mock_xSemaphoreTake_called;
extern unsigned mock_xSemaphoreTake_call_seq;
extern SemaphoreHandle_t mock_xSemaphoreTake_Handle;
extern TickType_t mock_xSemaphoreTake_xTicksToWait;

extern int mock_xSemaphoreGive_called;
extern unsigned mock_xSemaphoreGive_call_seq;
extern SemaphoreHandle_t mock_xSemaphoreGive_Handle;

extern int mock_vSemaphoreDelete_called;
extern unsigned mock_vSemaphoreDelete_call_seq;
extern SemaphoreHandle_t mock_vSemaphoreDelete_Handle;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);
void vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

void mock_freertos_semaphore_reset(void);
