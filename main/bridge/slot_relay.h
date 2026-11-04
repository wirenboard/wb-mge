#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Index of the port whose DE line (SERIAL_IO_PIN_2) drives the WB-MGU slot relay.
#define SLOT_RELAY_PORT_INDEX 1u

// Port manager hooks: the pin belongs to the relay only while the UART does not use it, that is
// while the port is closed or its TX is disabled.
// Acquire drives the pin LOW (there is no external pulldown), release hands it to the UART.
void slot_relay_acquire(void);
void slot_relay_release(void);

// ESP_ERR_NOT_SUPPORTED: no slot on this board.
// ESP_ERR_INVALID_STATE: the UART or the factory test owns the pin.
esp_err_t slot_relay_set(bool on);

// ESP_ERR_NOT_SUPPORTED: no slot on this board. Reads false while the UART owns the pin.
esp_err_t slot_relay_get(bool *on);
