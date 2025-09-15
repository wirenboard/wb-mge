#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hardware_mocks_qemu";

// Mock functions for hardware-specific functionality that's not available in QEMU

// System voltage mock
float system_voltage_read(void)
{
    ESP_LOGD(TAG, "Mock system_voltage_read() called");
    return 12.0f; // Return a mock voltage value
}

// Config button mock
uint32_t config_button_get_press_count(void)
{
    ESP_LOGD(TAG, "Mock config_button_get_press_count() called");
    return 0; // No button presses in QEMU
}

// RS485 control mock
void update_rs485_control(void)
{
    ESP_LOGD(TAG, "Mock update_rs485_control() called");
    // No-op in QEMU
}

// IO bus control mock
void update_io_bus_control(void)
{
    ESP_LOGD(TAG, "Mock update_io_bus_control() called");
    // No-op in QEMU
}

// MIO control mock
void mio_control_init(void)
{
    ESP_LOGD(TAG, "Mock mio_control_init() called");
    // No-op in QEMU
}

// System voltage init mock
void system_voltage_init(void)
{
    ESP_LOGD(TAG, "Mock system_voltage_init() called");
    // No-op in QEMU
}

// Config button init mock
void config_button_init(void)
{
    ESP_LOGD(TAG, "Mock config_button_init() called");
    // No-op in QEMU
}

// RS485 control init mock
void rs485_control_init(void)
{
    ESP_LOGD(TAG, "Mock rs485_control_init() called");
    // No-op in QEMU
}

// Update RS485/MIO GPIO states mock
void update_rs485_mio_gpio_states(void)
{
    ESP_LOGD(TAG, "Mock update_rs485_mio_gpio_states() called");
    // No-op in QEMU
}
