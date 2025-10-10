#pragma once

#include "driver/i2c_master.h"
#include "esp_io_expander.h"

#define MOCK_EXPANDER_HANDLE        ((esp_io_expander_handle_t)0x87654321)

enum esp_io_expander_tca_95xx_16bit_address {
    ESP_IO_EXPANDER_I2C_TCA9539_ADDRESS_00 = 0b1110100,
    ESP_IO_EXPANDER_I2C_TCA9539_ADDRESS_01 = 0b1110101,
    ESP_IO_EXPANDER_I2C_TCA9539_ADDRESS_10 = 0b1110110,
    ESP_IO_EXPANDER_I2C_TCA9539_ADDRESS_11 = 0b1110111,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000 = 0b0100000,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_001 = 0b0100001,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_010 = 0b0100010,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_011 = 0b0100011,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_100 = 0b0100100,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_101 = 0b0100101,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_110 = 0b0100110,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_111 = 0b0100111,
};

extern esp_err_t mock_esp_io_expander_return;
extern int mock_esp_io_expander_called;
extern i2c_master_bus_handle_t mock_esp_io_expander_bus;
extern uint32_t mock_esp_io_expander_addr;

esp_err_t esp_io_expander_new_i2c_tca95xx_16bit(i2c_master_bus_handle_t i2c_bus, uint32_t dev_addr, esp_io_expander_handle_t *handle_ret);
