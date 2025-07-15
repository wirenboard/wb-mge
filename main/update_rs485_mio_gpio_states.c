#include "rs485_control.h"
#include "mio_control.h"
#include "setting_items.h"
#include "esp_log.h"
#include "esp_err.h"

#include <stdbool.h>

static const char *TAG = "update_rs485_mio_gpio_states";

// обновляет состояние подтяжек, питания и терминаторов RS485 в соответствии с текущими настройками
void update_rs485_control(void)
{
    ESP_LOGI(TAG, "%s", __func__);

    bool pullup_1_enabled = false;
    bool pullup_2_enabled = false;
    bool term_1_enabled = false;
    bool term_2_enabled = false;
    bool vout_enabled = false;

    setting_items_read_raw(KEY_485_FAIL_SAFE_1, &pullup_1_enabled, SETTING_ITEM_TYPE_BOOL);
    setting_items_read_raw(KEY_485_FAIL_SAFE_2, &pullup_2_enabled, SETTING_ITEM_TYPE_BOOL);
    setting_items_read_raw(KEY_485_TERM_1, &term_1_enabled, SETTING_ITEM_TYPE_BOOL);
    setting_items_read_raw(KEY_485_TERM_2, &term_2_enabled, SETTING_ITEM_TYPE_BOOL);
    setting_items_read_raw(KEY_485_VOUT, &vout_enabled, SETTING_ITEM_TYPE_BOOL);

    rs485_pupd_on_off(RS485_1, pullup_1_enabled);
    rs485_pupd_on_off(RS485_2, pullup_2_enabled);
    rs485_term_on_off(RS485_1, term_1_enabled);
    rs485_term_on_off(RS485_2, term_2_enabled);
    rs485_bus_vout_on_off(vout_enabled);

    ESP_LOGI(TAG, "RS485 control updated");
}

void update_io_bus_control(void)
{
    ESP_LOGI(TAG, "%s", __func__);

    bool io_bus_enabled = false;
    setting_items_read_raw(KEY_IO_BUS_ENABLED, &io_bus_enabled, SETTING_ITEM_TYPE_BOOL);

    mio_control_io_bus_onoff(io_bus_enabled);
    ESP_LOGI(TAG, "IO bus control updated");
}
