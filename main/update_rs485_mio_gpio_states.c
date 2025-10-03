#include "rs485_control.h"
#include "mio_control.h"
#include "setting_items.h"
#include "esp_log.h"
#include "esp_err.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "update_rs485_mio_gpio_states";

// обновляет состояние подтяжек, питания и терминаторов RS485 в соответствии с текущими настройками
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

void update_io_bus_control(void)
{
    bool io_bus_enabled = setting_items_read_bool(KEY_IO_BUS_ENABLED);

    mio_control_io_bus_onoff(io_bus_enabled);
    ESP_LOGI(TAG, "IO bus control updated: %s", io_bus_enabled ? "enabled" : "disabled");
}
