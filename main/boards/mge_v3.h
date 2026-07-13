#pragma once

/* WB-MGE (signature mge_v3): no PSRAM, GPIO9 free for RS485-1 RX.
 * Port 1 = RS485-1, Port 2 = RS485-2. */
#define SERIAL_INPUT_PIN_1   GPIO_NUM_9    /* RS485-1 RX */
#define SERIAL_OUTPUT_PIN_1  GPIO_NUM_10   /* RS485-1 TX */
#define SERIAL_IO_PIN_1      GPIO_NUM_4    /* RS485-1 direction (DE/RE) */

#define SERIAL_INPUT_PIN_2   GPIO_NUM_12   /* RS485-2 RX */
#define SERIAL_OUTPUT_PIN_2  GPIO_NUM_14   /* RS485-2 TX */
#define SERIAL_IO_PIN_2      GPIO_NUM_15   /* RS485-2 direction (DE/RE) */
