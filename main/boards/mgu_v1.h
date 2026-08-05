#pragma once

/* WB-MGU (signature mgu_v1): external PSRAM. GPIO9 = PSRAM CE and GPIO10 = WBE2
 * TX, so RS485 is kept off GPIO9. Port 1 = RS485 on 14/12/15; Port 2 = WBE2 bus
 * on 10/4/13. Pins confirmed from the WB-MGU schematic. */
#define SERIAL_INPUT_PIN_1   GPIO_NUM_12   /* RS485 RX */
#define SERIAL_OUTPUT_PIN_1  GPIO_NUM_14   /* RS485 TX */
#define SERIAL_IO_PIN_1      GPIO_NUM_15   /* RS485 direction (DE/RE) */

#define SERIAL_INPUT_PIN_2   GPIO_NUM_4    /* WBE2 RX */
#define SERIAL_OUTPUT_PIN_2  GPIO_NUM_10   /* WBE2 TX */
#define SERIAL_IO_PIN_2      GPIO_NUM_13   /* WBE2 direction (DE/RE) */
