#include "mqtt_manager.h"

int mock_mqtt_manager_init_called = 0;
int mock_mqtt_manager_restart_called = 0;

esp_err_t mqtt_manager_init(void)
{
    mock_mqtt_manager_init_called++;
    return ESP_OK;
}

void mqtt_manager_restart(void)
{
    mock_mqtt_manager_restart_called++;
}

void mock_mqtt_manager_reset(void)
{
    mock_mqtt_manager_init_called = 0;
    mock_mqtt_manager_restart_called = 0;
}
