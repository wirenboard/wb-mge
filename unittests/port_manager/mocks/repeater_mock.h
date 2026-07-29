#pragma once

/* Mock tracking state for repeater functions — include from tests to access
 * call counters and control variables. */

#include "bridge.h"   /* for BRIDGES_COUNT */
#include <stdbool.h>

typedef struct {
    int init_called;
    int deinit_called;
} mock_repeater_calls_t;

extern mock_repeater_calls_t mock_repeater_calls[BRIDGES_COUNT];
extern bool mock_repeater_init_should_fail;
/* repeater_init() — the global (non per-port) init that creates the repeater mutex. */
extern int mock_repeater_global_init_called;

void mock_repeater_reset(void);
