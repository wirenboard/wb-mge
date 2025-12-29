#include "driver/i2c_master.h"

#include <string.h>

mock_i2c_new_master_bus_t mock_i2c_new_master_bus_data = {0};
mock_i2c_del_master_bus_t mock_i2c_del_master_bus_data = {0};

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config, i2c_master_bus_handle_t *ret_bus_handle)
{
    mock_i2c_new_master_bus_data.called++;

    if (bus_config) {
        memcpy(&mock_i2c_new_master_bus_data.bus_config, bus_config, sizeof(i2c_master_bus_config_t));
    }

    if (mock_i2c_new_master_bus_data.should_fail) {
        return ESP_FAIL;
    }

    if (bus_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ret_bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *ret_bus_handle = MOCK_I2C_MASTER_BUS_HANDLE;

    return ESP_OK;
}

esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle)
{
    mock_i2c_del_master_bus_data.called++;
    mock_i2c_del_master_bus_data.bus_handle = bus_handle;

    if (mock_i2c_del_master_bus_data.should_fail) {
        return ESP_FAIL;
    }

    if (bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

void mock_i2c_master_reset(void)
{
    memset(&mock_i2c_new_master_bus_data, 0, sizeof(mock_i2c_new_master_bus_data));
    memset(&mock_i2c_del_master_bus_data, 0, sizeof(mock_i2c_del_master_bus_data));
}
