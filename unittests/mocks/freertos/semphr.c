#include "semphr.h"
#include <stdlib.h>
#include "call_sequence.h"

int mock_xSemaphoreCreateMutex_called = 0;
unsigned mock_xSemaphoreCreateMutex_call_seq = 0;
SemaphoreHandle_t mock_xSemaphoreCreateMutex_return_value = MOCK_SEMAPHORE_HANDLE_T;

int mock_xSemaphoreTake_called = 0;
unsigned mock_xSemaphoreTake_call_seq = 0;
SemaphoreHandle_t mock_xSemaphoreTake_Handle = NULL;
TickType_t mock_xSemaphoreTake_xTicksToWait = 0;
BaseType_t mock_xSemaphoreTake_return_value = pdPASS;

int mock_xSemaphore_held_count = 0;

int mock_xSemaphoreGive_called = 0;
unsigned mock_xSemaphoreGive_call_seq = 0;
SemaphoreHandle_t mock_xSemaphoreGive_Handle = NULL;

int mock_vSemaphoreDelete_called = 0;
unsigned mock_vSemaphoreDelete_call_seq = 0;
SemaphoreHandle_t mock_vSemaphoreDelete_Handle = NULL;

void (*mock_xSemaphoreTake_hook)(void) = NULL;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    mock_xSemaphoreCreateMutex_called++;
    mock_xSemaphoreCreateMutex_call_seq = call_sequence_get_call_id();
    return mock_xSemaphoreCreateMutex_return_value;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
    mock_xSemaphoreTake_called++;
    mock_xSemaphoreTake_call_seq = call_sequence_get_call_id();
    mock_xSemaphoreTake_Handle = xSemaphore;
    mock_xSemaphoreTake_xTicksToWait = xTicksToWait;
    if (mock_xSemaphoreTake_hook != NULL) {
        mock_xSemaphoreTake_hook();
    }
    if (mock_xSemaphoreTake_return_value == pdPASS) {
        mock_xSemaphore_held_count++;
    }
    return mock_xSemaphoreTake_return_value;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    mock_xSemaphoreGive_called++;
    mock_xSemaphoreGive_call_seq = call_sequence_get_call_id();
    mock_xSemaphoreGive_Handle = xSemaphore;
    if (mock_xSemaphore_held_count > 0) {
        mock_xSemaphore_held_count--;
    }
    return pdPASS;
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
    mock_vSemaphoreDelete_called++;
    mock_vSemaphoreDelete_call_seq = call_sequence_get_call_id();
    mock_vSemaphoreDelete_Handle = xSemaphore;
}

void mock_freertos_semaphore_reset(void)
{
    mock_xSemaphoreCreateMutex_called = 0;
    mock_xSemaphoreCreateMutex_call_seq = 0;
    mock_xSemaphoreCreateMutex_return_value = MOCK_SEMAPHORE_HANDLE_T;

    mock_xSemaphoreTake_called = 0;
    mock_xSemaphoreTake_call_seq = 0;
    mock_xSemaphoreTake_Handle = NULL;
    mock_xSemaphoreTake_xTicksToWait = 0;
    mock_xSemaphoreTake_return_value = pdPASS;

    mock_xSemaphore_held_count = 0;

    mock_xSemaphoreGive_called = 0;
    mock_xSemaphoreGive_call_seq = 0;
    mock_xSemaphoreGive_Handle = NULL;

    mock_vSemaphoreDelete_called = 0;
    mock_vSemaphoreDelete_call_seq = 0;
    mock_vSemaphoreDelete_Handle = NULL;

    mock_xSemaphoreTake_hook = NULL;
}
