#pragma once

/* Pull in the common timers mock definitions */
#include_next "freertos/timers.h"

#include <stdint.h>

/* Additional timer stubs required by sniffer.c */

static inline int xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    (void)xTimer;
    (void)xTicksToWait;
    return 1; /* pdPASS */
}

static inline void *pvTimerGetTimerID(TimerHandle_t xTimer)
{
    (void)xTimer;
    return 0;
}
