#pragma once

#include <stdbool.h>

/*
 * Runtime board-variant detection and board-dependent pin routing.
 *
 * The same firmware binary runs on two hardware variants that share the ESP32
 * but differ in RS485 UART pin assignment:
 *   - WB-MGE: no PSRAM.  RS485-1 = GPIO10/9/4, RS485-2 = GPIO14/12/15.
 *   - WB-MGU: external PSRAM.  GPIO9 is the PSRAM chip-enable (CE) and GPIO10 is
 *     the WBE2 TX, so they MUST NOT be routed to a UART. The single RS485 lives
 *     on GPIO14/12/15 (logical port 0) and the WBE2 bus on GPIO10/4/13 (port 1).
 *
 * Board is discriminated by PSRAM presence (esp_psram_is_initialized()).
 */

/* True on WB-MGU (PSRAM present), false on WB-MGE. Result is fixed at runtime
 * and cached on first call. */
bool board_is_mgu(void);

/* Return the TX / RX / RTS(direction) GPIO numbers for the given 0-based RS485
 * logical port index (0 or 1), selecting the correct set for the detected board.
 * Any of the out-pointers may be NULL. */
void board_rs485_pins(unsigned port_index, int *tx, int *rx, int *rts);
