#pragma once

#include "FreeRTOS.h"

#define MOCK_TIMER_HANDLE                           ((TimerHandle_t)0xDDDDDDDD)

typedef void *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t xTimer);

extern int mock_xTimerCreate_called;
extern const char *mock_xTimerCreate_pcTimerName;
extern TickType_t mock_xTimerCreate_xTimerPeriod;
extern BaseType_t mock_xTimerCreate_xAutoReload;
extern void *mock_xTimerCreate_pvTimerID;
extern TimerCallbackFunction_t mock_xTimerCreate_pxCallbackFunction;
extern TimerHandle_t mock_xTimerCreate_return_value;

extern int mock_xTimerStart_called;
extern TimerHandle_t mock_xTimerStart_xTimer;
extern TickType_t mock_xTimerStart_xTicksToWait;

void mock_freertos_timers_reset(void);

TimerHandle_t xTimerCreate(const char * const pcTimerName,
                          const TickType_t xTimerPeriod,
                          const BaseType_t xAutoReload,
                          void * const pvTimerID,
                          TimerCallbackFunction_t pxCallbackFunction);

BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait);

