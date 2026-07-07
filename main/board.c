#include "board.h"

#include "esp_psram.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "board";

/*
 * RS485 UART pin sets per board, indexed by 0-based logical port.
 * Confirmed from the WB-MGE and WB-MGU schematics and the Wirenboard wiki.
 *
 * WB-MGU note: GPIO9 = PSRAM CE and GPIO10 = WBE2 TX. Routing GPIO9 to a UART
 * hijacks the PSRAM chip-enable and corrupts the (PSRAM-backed) heap, so the
 * RS485 is placed on GPIO14/12/15 (port 0) and the WBE2 bus on GPIO10/4/13.
 */
typedef struct {
    int tx;
    int rx;
    int rts;  /* RS485 direction (DE/RE) pin */
} rs485_pinset_t;

static const rs485_pinset_t PINS_MGE[2] = {
    { GPIO_NUM_10, GPIO_NUM_9,  GPIO_NUM_4  },   /* port 0: RS485-1 */
    { GPIO_NUM_14, GPIO_NUM_12, GPIO_NUM_15 },   /* port 1: RS485-2 */
};

static const rs485_pinset_t PINS_MGU[2] = {
    { GPIO_NUM_14, GPIO_NUM_12, GPIO_NUM_15 },   /* port 0: RS485 (GPIO9=PSRAM CE, kept off UART) */
    { GPIO_NUM_10, GPIO_NUM_4,  GPIO_NUM_13 },   /* port 1: WBE2 extension bus */
};

bool board_is_mgu(void)
{
    /* PSRAM presence is the board discriminator; it never changes at runtime. */
    static int cached = -1;
    if (cached < 0) {
        bool mgu = esp_psram_is_initialized();
        cached = mgu ? 1 : 0;
        ESP_LOGI(TAG, "Board detected: %s", mgu ? "WB-MGU (PSRAM)" : "WB-MGE (no PSRAM)");
    }
    return cached == 1;
}

void board_rs485_pins(unsigned port_index, int *tx, int *rx, int *rts)
{
    if (port_index > 1) {
        port_index = 0;  /* defensive: only two logical ports exist */
    }
    const rs485_pinset_t *set = board_is_mgu() ? &PINS_MGU[port_index]
                                               : &PINS_MGE[port_index];
    if (tx)  { *tx  = set->tx; }
    if (rx)  { *rx  = set->rx; }
    if (rts) { *rts = set->rts; }
}
