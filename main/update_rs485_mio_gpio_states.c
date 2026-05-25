#include "rs485_control.h"
#include "mio_control.h"
#include "setting_items.h"
#include "bridge/port_manager.h"
#include "esp_log.h"

#include <stdbool.h>

static const char *TAG = "update_rs485_mio_gpio_states";

// Updates the state of pull-ups, power and RS485 terminators according to current settings
void update_rs485_control(void)
{
    bool pullup_1_enabled = setting_items_read_bool(KEY_485_FAIL_SAFE_1);
    bool pullup_2_enabled = setting_items_read_bool(KEY_485_FAIL_SAFE_2);
    bool term_1_enabled = setting_items_read_bool(KEY_485_TERM_1);
    bool term_2_enabled = setting_items_read_bool(KEY_485_TERM_2);
    bool vout_enabled = setting_items_read_bool(KEY_485_VOUT);

    rs485_pupd_on_off(RS485_1, pullup_1_enabled);
    rs485_pupd_on_off(RS485_2, pullup_2_enabled);
    rs485_term_on_off(RS485_1, term_1_enabled);
    rs485_term_on_off(RS485_2, term_2_enabled);
    rs485_bus_vout_on_off(vout_enabled);

    // Apply TX-disabled flag to running serial ports immediately
    port_manager_set_tx_disabled(0, setting_items_read_bool(KEY_485_TX_DISABLED_1));
    port_manager_set_tx_disabled(1, setting_items_read_bool(KEY_485_TX_DISABLED_2));

    ESP_LOGI(TAG, "RS485 control updated");
}

void update_io_bus_control(void)
{
    bool io_bus_enabled = setting_items_read_bool(KEY_IO_BUS_ENABLED);

    mio_control_io_bus_onoff(io_bus_enabled);
    ESP_LOGI(TAG, "IO bus control updated: %s", io_bus_enabled ? "enabled" : "disabled");
}
