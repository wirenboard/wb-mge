#include "semphr.h"
#include <stdlib.h>
#include <stdbool.h>

SemaphoreHandle_t mock_xSemaphoreCreateMutex_return = NULL;
bool mock_xSemaphoreCreateMutex_should_fail = false;
int mock_xSemaphoreCreateMutex_called = 0;
int mock_xSemaphoreTake_called = 0;
int mock_xSemaphoreGive_called = 0;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    mock_xSemaphoreCreateMutex_called++;

    if (mock_xSemaphoreCreateMutex_should_fail) {
        return NULL;
    }

    if (mock_xSemaphoreCreateMutex_return != NULL) {
        return mock_xSemaphoreCreateMutex_return;
    }

    return (SemaphoreHandle_t)0xDEADBEEF;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
    (void)xSemaphore;
    (void)xTicksToWait;
    mock_xSemaphoreTake_called++;
    return pdPASS;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    (void)xSemaphore;
    mock_xSemaphoreGive_called++;
    return pdPASS;
}
