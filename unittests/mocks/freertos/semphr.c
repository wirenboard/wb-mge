#include "semphr.h"
#include <stdlib.h>

int mock_xSemaphoreCreateMutex_called = 0;
SemaphoreHandle_t mock_xSemaphoreCreateMutex_return_value = MOCK_SEMAPHORE_HANDLE_T;

SemaphoreHandle_t mock_xSemaphoreTake_Handle = NULL;
TickType_t mock_xSemaphoreTake_xTicksToWait = 0;
int mock_xSemaphoreTake_called = 0;

SemaphoreHandle_t mock_xSemaphoreGive_Handle = NULL;
int mock_xSemaphoreGive_called = 0;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    mock_xSemaphoreCreateMutex_called++;
    return mock_xSemaphoreCreateMutex_return_value;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
    mock_xSemaphoreTake_Handle = xSemaphore;
    mock_xSemaphoreTake_xTicksToWait = xTicksToWait;
    mock_xSemaphoreTake_called++;
    return pdPASS;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    mock_xSemaphoreGive_Handle = xSemaphore;
    mock_xSemaphoreGive_called++;
    return pdPASS;
}

void mock_freertos_semaphore_reset(void)
{
    mock_xSemaphoreCreateMutex_called = 0;
    mock_xSemaphoreCreateMutex_return_value = MOCK_SEMAPHORE_HANDLE_T;

    mock_xSemaphoreTake_Handle = NULL;
    mock_xSemaphoreTake_xTicksToWait = 0;
    mock_xSemaphoreTake_called = 0;

    mock_xSemaphoreGive_Handle = NULL;
    mock_xSemaphoreGive_called = 0;
}
