#include "timers.h"
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
}
