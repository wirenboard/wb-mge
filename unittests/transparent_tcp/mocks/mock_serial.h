#pragma once

#include "serial.h"
#include <stdbool.h>

typedef struct {
    int init_called;
    int deinit_called;
    bool init_should_fail;
} mock_serial_calls_t;

extern mock_serial_calls_t mock_serial_calls;

void mock_serial_reset(void);
