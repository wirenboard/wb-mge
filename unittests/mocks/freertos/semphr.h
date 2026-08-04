#pragma once

#include "FreeRTOS.h"
#include <stdbool.h>

#define MOCK_SEMAPHORE_HANDLE_T             ((SemaphoreHandle_t)0xBAADF00D)

typedef void *SemaphoreHandle_t;

extern int mock_xSemaphoreCreateMutex_called;
extern unsigned mock_xSemaphoreCreateMutex_call_seq;
extern SemaphoreHandle_t mock_xSemaphoreCreateMutex_return_value;

/* Static creation is counted separately from the dynamic one above, and has no
 * *_return_value knob on purpose: the real xSemaphoreCreateMutexStatic() returns the buffer
 * it was handed, so it cannot fail for a non-NULL buffer, and a mock that could fail would
 * let a test assert behaviour the firmware can never meet. */
extern int mock_xSemaphoreCreateMutexStatic_called;
extern unsigned mock_xSemaphoreCreateMutexStatic_call_seq;
/* Every buffer handed to it, in call order: sharing is a relation between two of them, which a
 * "last buffer" field could not show. Calls past the cap are counted but not recorded. */
#define MOCK_STATIC_MUTEX_MAX 8
extern StaticSemaphore_t *mock_xSemaphoreCreateMutexStatic_buffers[MOCK_STATIC_MUTEX_MAX];

extern int mock_xSemaphoreTake_called;
extern unsigned mock_xSemaphoreTake_call_seq;
extern SemaphoreHandle_t mock_xSemaphoreTake_Handle;
extern TickType_t mock_xSemaphoreTake_xTicksToWait;
/* Value xSemaphoreTake() returns. pdPASS by default (restored by
 * mock_freertos_semaphore_reset()); set to pdFAIL to exercise a lock-timeout path. */
extern BaseType_t mock_xSemaphoreTake_return_value;

extern int mock_xSemaphoreGive_called;
extern unsigned mock_xSemaphoreGive_call_seq;
extern SemaphoreHandle_t mock_xSemaphoreGive_Handle;

extern int mock_vSemaphoreDelete_called;
extern unsigned mock_vSemaphoreDelete_call_seq;
extern SemaphoreHandle_t mock_vSemaphoreDelete_Handle;

/* Called from inside xSemaphoreTake(), before it returns — i.e. in the window where
 * the caller is "waiting for the lock". Lets a single-threaded test inject the event
 * that a competing task would have caused there (see the port-freeze race tests).
 * NULL (the default, restored by mock_freertos_semaphore_reset()) means no hook. */
extern void (*mock_xSemaphoreTake_hook)(void);

/* Number of takes that have not been given back yet: a single-threaded test can read it
 * to tell whether the code under test currently holds the lock. */
extern int mock_xSemaphore_held_count;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *pxMutexBuffer);
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);
void vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

void mock_freertos_semaphore_reset(void);
