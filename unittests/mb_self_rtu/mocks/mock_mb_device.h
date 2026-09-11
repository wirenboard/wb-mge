#pragma once

#include "modbus_helpers.h"   /* MODBUS_RTU_MAX_FRAME_LEN */

#include <stddef.h>
#include <stdint.h>

// Observation of the mocked unit-0xFF responder: what it was called with, and what it
// should answer. resp/resp_len are writable by the test (set resp_len to 0 to make the
// responder decline the request, as it does for a foreign unit id).
typedef struct {
    int     called;
    uint8_t last_req[MODBUS_RTU_MAX_FRAME_LEN];
    size_t  last_req_len;
    uint8_t resp[MODBUS_RTU_MAX_FRAME_LEN];
    size_t  resp_len;
} mock_mb_device_calls_t;

extern mock_mb_device_calls_t mock_mb_device_calls;

void mock_mb_device_reset(void);
