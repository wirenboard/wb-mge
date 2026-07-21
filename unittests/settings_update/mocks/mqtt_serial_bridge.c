#include "mqtt_serial_bridge.h"

int mock_mqtt_serial_bridge_start_called = 0;
int mock_mqtt_serial_bridge_stop_called = 0;

esp_err_t mqtt_serial_bridge_start(void)
{
    mock_mqtt_serial_bridge_start_called++;
    return ESP_OK;
}

void mqtt_serial_bridge_stop(void)
{
    mock_mqtt_serial_bridge_stop_called++;
}

void mock_mqtt_serial_bridge_reset(void)
{
    mock_mqtt_serial_bridge_start_called = 0;
    mock_mqtt_serial_bridge_stop_called = 0;
}
