#include "esp_io_expander_tca95xx_16bit.h"

esp_err_t mock_esp_io_expander_return = ESP_OK;
int mock_esp_io_expander_called = 0;
i2c_master_bus_handle_t mock_esp_io_expander_bus = NULL;
uint32_t mock_esp_io_expander_addr = 0;

esp_err_t esp_io_expander_new_i2c_tca95xx_16bit(
    i2c_master_bus_handle_t i2c_bus, uint32_t dev_addr, esp_io_expander_handle_t *handle_ret
)
{
    mock_esp_io_expander_called++;
    mock_esp_io_expander_bus = i2c_bus;
    mock_esp_io_expander_addr = dev_addr;

    if (mock_esp_io_expander_return != ESP_OK) {
        return mock_esp_io_expander_return;
    }

    if (handle_ret) {
        *handle_ret = MOCK_EXPANDER_HANDLE;
    }

    return ESP_OK;
}
