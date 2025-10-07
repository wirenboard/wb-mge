#include "driver/i2c_master.h"

#include <string.h>

esp_err_t mock_i2c_new_master_bus_return = ESP_OK;
int mock_i2c_new_master_bus_called = 0;
i2c_master_bus_handle_t mock_i2c_bus_handle = NULL;
i2c_master_bus_config_t mock_i2c_bus_config = {0};

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config, i2c_master_bus_handle_t *ret_bus_handle)
{
    mock_i2c_new_master_bus_called++;

    if (bus_config) {
        memcpy(&mock_i2c_bus_config, bus_config, sizeof(i2c_master_bus_config_t));
    }

    if (mock_i2c_new_master_bus_return != ESP_OK) {
        return mock_i2c_new_master_bus_return;
    }

    if (ret_bus_handle) {
        *ret_bus_handle = (i2c_master_bus_handle_t)0x12345678;
        mock_i2c_bus_handle = *ret_bus_handle;
    }

    return ESP_OK;
}
