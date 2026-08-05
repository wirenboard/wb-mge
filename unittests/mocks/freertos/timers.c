#include "timers.h"
#include "unity.h"
#include <stdlib.h>

int mock_xTimerCreate_called = 0;
const char *mock_xTimerCreate_pcTimerName = NULL;
TickType_t mock_xTimerCreate_xTimerPeriod = 0;
BaseType_t mock_xTimerCreate_xAutoReload = 0;
void *mock_xTimerCreate_pvTimerID = NULL;
TimerCallbackFunction_t mock_xTimerCreate_pxCallbackFunction = NULL;
TimerHandle_t mock_xTimerCreate_return_value = MOCK_TIMER_HANDLE;

int mock_xTimerStart_called = 0;
TimerHandle_t mock_xTimerStart_xTimer = NULL;
TickType_t mock_xTimerStart_xTicksToWait = 0;
BaseType_t mock_xTimerStart_return_value = pdPASS;

int mock_xTimerStop_called = 0;
TimerHandle_t mock_xTimerStop_xTimer = NULL;
TickType_t mock_xTimerStop_xTicksToWait = 0;

int mock_xTimerDelete_called = 0;
TimerHandle_t mock_xTimerDelete_xTimer = NULL;

TimerHandle_t xTimerCreate(const char * const pcTimerName,
                          const TickType_t xTimerPeriod,
                          const BaseType_t xAutoReload,
                          void * const pvTimerID,
                          TimerCallbackFunction_t pxCallbackFunction)
{
    mock_xTimerCreate_pcTimerName = pcTimerName;
    mock_xTimerCreate_xTimerPeriod = xTimerPeriod;
    mock_xTimerCreate_xAutoReload = xAutoReload;
    mock_xTimerCreate_pvTimerID = pvTimerID;
    mock_xTimerCreate_pxCallbackFunction = pxCallbackFunction;
    mock_xTimerCreate_called++;
    return mock_xTimerCreate_return_value;
}

BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    mock_xTimerStart_xTimer = xTimer;
    mock_xTimerStart_xTicksToWait = xTicksToWait;
    mock_xTimerStart_called++;
    /* Overridable so a test can reproduce a full FreeRTOS timer command queue: with
     * xTicksToWait = 0 the real xTimerStart() returns pdFAIL and arms nothing. */
    return mock_xTimerStart_return_value;
}

BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    /* The real xTimerStop() opens with configASSERT(xTimer), and this firmware is
     * built with assertion level 2 — a NULL handle is a panic and a reboot on the
     * device, not a quiet no-op. Reproduce that verdict here instead of accepting
     * NULL, so a caller that runs before its owner's init (the boot window that
     * port_manager_init_subsystems() closes) fails the test on the host too. */
    TEST_ASSERT_NOT_NULL_MESSAGE(xTimer,
        "xTimerStop(NULL): FreeRTOS configASSERT panics the device on this handle");
    mock_xTimerStop_xTimer = xTimer;
    mock_xTimerStop_xTicksToWait = xTicksToWait;
    mock_xTimerStop_called++;
    return pdPASS;
}

BaseType_t xTimerDelete(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    mock_xTimerDelete_xTimer = xTimer;
    mock_xTimerDelete_called++;
    return pdPASS;
}

void mock_freertos_timers_reset(void)
{
    mock_xTimerCreate_called = 0;
    mock_xTimerCreate_pcTimerName = NULL;
    mock_xTimerCreate_xTimerPeriod = 0;
    mock_xTimerCreate_xAutoReload = 0;
    mock_xTimerCreate_pvTimerID = NULL;
    mock_xTimerCreate_pxCallbackFunction = NULL;
    mock_xTimerCreate_return_value = MOCK_TIMER_HANDLE;

    mock_xTimerStart_called = 0;
    mock_xTimerStart_xTimer = NULL;
    mock_xTimerStart_xTicksToWait = 0;
    mock_xTimerStart_return_value = pdPASS;

    mock_xTimerStop_called = 0;
    mock_xTimerStop_xTimer = NULL;
    mock_xTimerStop_xTicksToWait = 0;

    mock_xTimerDelete_called = 0;
    mock_xTimerDelete_xTimer = NULL;
}
