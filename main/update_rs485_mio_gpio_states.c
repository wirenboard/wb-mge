#include "setting_items.h"
#include "bridge/port_manager.h"
#include "esp_log.h"

#include <stdbool.h>

#if !QEMU_BUILD
    #include "rs485_control.h"
    #include "mio_control.h"
#endif

#if !QEMU_BUILD
static const char *TAG = "update_rs485_mio_gpio_states";

// Updates the state of pull-ups, power and RS485 terminators according to current settings.
// Requires GPIO expander (TCA9535) — hardware only, not available in QEMU.
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

    ESP_LOGI(TAG, "RS485 control updated");
}

// Updates the IO bus (MIO) enable state according to current settings.
// Requires GPIO expander (TCA9535) — hardware only, not available in QEMU.
void update_io_bus_control(void)
{
    bool io_bus_enabled = setting_items_read_bool(KEY_IO_BUS_ENABLED);

    mio_control_io_bus_onoff(io_bus_enabled);
    ESP_LOGI(TAG, "IO bus control updated: %s", io_bus_enabled ? "enabled" : "disabled");
}
#endif // !QEMU_BUILD

// Applies the tx_disabled setting to the running serial ports for both RS-485 ports.
// Unlike update_rs485_control(), this is a pure software flag with no GPIO expander
// dependency, so it must run in QEMU builds as well.
void update_serial_tx_disabled(void)
{
    port_manager_set_tx_disabled(0, setting_items_read_bool(KEY_485_TX_DISABLED_1));
    port_manager_set_tx_disabled(1, setting_items_read_bool(KEY_485_TX_DISABLED_2));
}
