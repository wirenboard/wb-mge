#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hardware_mocks_qemu";

// Mock functions for hardware-specific functionality that's not available in QEMU.
// Most former mocks are now provided by the real hardware-logic modules running
// against the virtual IO bus (see virtual_io_qemu.c). Only voltage_monitor stays
// excluded and mocked here.

// System voltage mock
float voltage_monitor_get_sys_voltage(void)
{
    ESP_LOGD(TAG, "Mock voltage_monitor_get_sys_voltage() called");
    return 12.3f;
}
