#pragma once

#include "serial.h"
#include <stdbool.h>

typedef struct {
    int init_called;
    int deinit_called;
    // Global call ordinal of the serial_deinit() call, so a test can pin that the UART
    // event task is joined BEFORE the TCP descriptor it produces into is freed.
    unsigned deinit_call_seq;
    bool init_should_fail;
    // Observation of serial_send (used to verify TCP -> serial relay)
    int send_called;
    uint8_t *send_last_data;
    size_t send_last_len;
} mock_serial_calls_t;

extern mock_serial_calls_t mock_serial_calls;

// Handler registered by serial_init (the transparent_tcp process_data_from_serial callback).
extern serial_receive_handler_t mock_serial_registered_handler;
// Descriptor handed back from serial_init, so a test can invoke the captured handler with it.
serial_desc_t *mock_serial_get_desc(void);

void mock_serial_reset(void);
