#pragma once
#include "esp_err.h"

/* Mock tracking data for serial_set_rx_timeout() calls.
 * Include this header AFTER sniffer.h (which transitively pulls in serial.h)
 * so that serial_desc_t is already defined — do not include serial.h here
 * to avoid redefinition errors. */

/* Forward declaration only — definition comes from serial.h via sniffer.h */
typedef struct serial_desc_t serial_desc_t;

typedef struct {
    int called;
    serial_desc_t *desc;
    uint8_t tout_symbols;
    esp_err_t result;  /* set to non-zero to simulate failure */
} mock_serial_set_rx_timeout_t;

extern mock_serial_set_rx_timeout_t mock_serial_set_rx_timeout_data;

void mock_serial_reset(void);
