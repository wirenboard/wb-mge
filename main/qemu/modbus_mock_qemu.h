#pragma once

#include "serial.h" // for serial_desc_t

#ifdef __cplusplus
extern "C" {
#endif

// Start the Modbus mock background task.
// serial_desc must already have sniff_handler set by sniffer_attach().
// The task polls until sniff_handler is non-NULL, so it is safe to call
// this function before sniffer_attach() completes.
void modbus_mock_qemu_start(serial_desc_t *serial_desc);

// Stop the Modbus mock background task and release its handle.
// Safe to call even if the task was never started or has already been stopped.
void modbus_mock_qemu_stop(void);

#ifdef __cplusplus
}
#endif
