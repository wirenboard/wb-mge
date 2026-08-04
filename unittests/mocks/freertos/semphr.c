#include "semphr.h"
#include <stdlib.h>
#include "call_sequence.h"

int mock_xSemaphoreCreateMutex_called = 0;
unsigned mock_xSemaphoreCreateMutex_call_seq = 0;
SemaphoreHandle_t mock_xSemaphoreCreateMutex_return_value = MOCK_SEMAPHORE_HANDLE_T;

int mock_xSemaphoreCreateMutexStatic_called = 0;
unsigned mock_xSemaphoreCreateMutexStatic_call_seq = 0;
StaticSemaphore_t *mock_xSemaphoreCreateMutexStatic_buffers[MOCK_STATIC_MUTEX_MAX] = { NULL };

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

/* The real one returns the caller's buffer cast to a handle and cannot fail for a non-NULL
 * buffer (FreeRTOS xQueueGenericCreateStatic(): `pxNewQueue = (Queue_t *) pxStaticQueue`).
 * Returning the buffer address rather than a fixed sentinel reproduces the property tests
 * care about — each mutex gets a distinct handle — so a test can still tell two of them
 * apart in mock_xSemaphoreTake_Handle. */
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *pxMutexBuffer)
{
    if (mock_xSemaphoreCreateMutexStatic_called < MOCK_STATIC_MUTEX_MAX) {
        mock_xSemaphoreCreateMutexStatic_buffers[mock_xSemaphoreCreateMutexStatic_called] = pxMutexBuffer;
    }
    mock_xSemaphoreCreateMutexStatic_called++;
    mock_xSemaphoreCreateMutexStatic_call_seq = call_sequence_get_call_id();
    return (SemaphoreHandle_t)pxMutexBuffer;
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

    mock_xSemaphoreCreateMutexStatic_called = 0;
    mock_xSemaphoreCreateMutexStatic_call_seq = 0;
    for (int i = 0; i < MOCK_STATIC_MUTEX_MAX; i++) {
        mock_xSemaphoreCreateMutexStatic_buffers[i] = NULL;
    }

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
