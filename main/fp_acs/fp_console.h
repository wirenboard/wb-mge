#pragma once

/*
 * The device console over HTTP (GET /fp/console): everything the ESP log prints from app_main()
 * on is also kept in a RAM ring of FP_CONSOLE_SIZE bytes in PSRAM, so a device away from any
 * laptop can still be diagnosed. The UART console is unchanged: every line still goes to the
 * vprintf that was installed before.
 *
 * Not captured: the bootloader and the startup code before app_main() (ESP_EARLY_LOG, which
 * bypasses the log's vprintf), logging from an ISR, a critical section or before the scheduler
 * runs, and a line that arrives while the ring is busy for longer than a short wait. A line longer
 * than 255 bytes is kept cut, ending in "...". Nothing survives a reboot.
 */

#include "fp_acs.h"

#if FP_ACS_ENABLED

#include <stdbool.h>
#include <stddef.h>

#define FP_CONSOLE_SIZE             (32 * 1024)

// Installs the capture. Call once, as early in app_main() as possible: what is logged before it
// is not kept. Without PSRAM it logs why and leaves the log as it was.
void fp_console_start(void);

// Copies the ring into out (at least FP_CONSOLE_SIZE bytes), oldest line first; once the ring has
// wrapped, from the first whole line on. False when the capture is not running or out is too
// small. Safe to call from any task.
bool fp_console_copy(char *out, size_t out_size, size_t *len);

#endif // FP_ACS_ENABLED
