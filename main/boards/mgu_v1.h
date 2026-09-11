#pragma once

/* WB-MGU (signature mgu_v1): external PSRAM. GPIO9 = PSRAM CE and GPIO10 = WBE2
 * TX, so RS485 is kept off GPIO9. Port 1 = RS485 on 14/12/15; Port 2 = WBE2 bus
 * on 10/4/13. Pins confirmed from the WB-MGU schematic. */
#define SERIAL_INPUT_PIN_1   GPIO_NUM_12   /* RS485 RX */
#define SERIAL_OUTPUT_PIN_1  GPIO_NUM_14   /* RS485 TX */
#define SERIAL_IO_PIN_1      GPIO_NUM_15   /* RS485 direction (DE/RE) */

#define SERIAL_INPUT_PIN_2   GPIO_NUM_4    /* WBE2 RX */
#define SERIAL_OUTPUT_PIN_2  GPIO_NUM_10   /* WBE2 TX */
#define SERIAL_IO_PIN_2      GPIO_NUM_13   /* wired, but unused: see SERIAL_MODE_2 */

/* UART driver mode per port. Port 1 drives an RS-485 transceiver and needs the
 * direction pin. Port 2 is the WBE2 bus to the Z-Wave board: full duplex, no
 * transceiver, so there is nothing for direction control to switch — half-duplex
 * mode would only gate TX on a pin the design does not use. */
#define SERIAL_MODE_1        UART_MODE_RS485_HALF_DUPLEX
#define SERIAL_MODE_2        UART_MODE_UART

/* Port index (0-based) whose RTU traffic is inspected for requests addressed to
 * this gateway's own unit id — the WBE2 bus, where the Z-Wave board is the Modbus
 * master. Defined on this board only: where it is absent the repeater relays every
 * byte, which is what every other board wants. */
#define MB_SELF_RTU_PORT_INDEX  1
