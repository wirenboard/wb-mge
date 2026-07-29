#pragma once

/* Pull in the common timers mock definitions */
#include_next "freertos/timers.h"

#include <stdint.h>

/* Additional timer stubs required by sniffer.c.
 * xTimerStop() used to live here as a do-nothing inline; it now comes from the
 * shared mock, which records its argument and rejects a NULL handle the way the
 * real configASSERT() does. */

static inline void *pvTimerGetTimerID(TimerHandle_t xTimer)
{
    (void)xTimer;
    return 0;
}
