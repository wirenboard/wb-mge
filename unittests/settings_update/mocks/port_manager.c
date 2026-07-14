#include "port_manager.h"

int mock_port_manager_check_settings_changed_called[BRIDGES_COUNT] = {0};
bool mock_port_manager_check_settings_changed_return_value[BRIDGES_COUNT] = {false, false};

int mock_port_manager_apply_settings_called[BRIDGES_COUNT] = {0};
unsigned mock_port_manager_apply_settings_index[BRIDGES_COUNT] = {0};
esp_err_t mock_port_manager_apply_settings_return_value[BRIDGES_COUNT] = {ESP_OK, ESP_OK};

int mock_port_manager_ports_frozen_called = 0;
bool mock_port_manager_ports_frozen_return_value = false;

bool port_manager_check_settings_changed(unsigned port_index)
{
    if (port_index < BRIDGES_COUNT) {
        mock_port_manager_check_settings_changed_called[port_index]++;
        return mock_port_manager_check_settings_changed_return_value[port_index];
    }
    return false;
}

esp_err_t port_manager_apply_settings(unsigned port_index)
{
    if (port_index < BRIDGES_COUNT) {
        mock_port_manager_apply_settings_index[port_index] = port_index;
        mock_port_manager_apply_settings_called[port_index]++;
        return mock_port_manager_apply_settings_return_value[port_index];
    }
    return ESP_FAIL;
}

bool port_manager_ports_frozen(void)
{
    mock_port_manager_ports_frozen_called++;
    return mock_port_manager_ports_frozen_return_value;
}

void mock_port_manager_reset(void)
{
    mock_port_manager_ports_frozen_called = 0;
    mock_port_manager_ports_frozen_return_value = false;

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        mock_port_manager_check_settings_changed_called[i] = 0;
        mock_port_manager_check_settings_changed_return_value[i] = false;

        mock_port_manager_apply_settings_called[i] = 0;
        mock_port_manager_apply_settings_index[i] = 0;
        mock_port_manager_apply_settings_return_value[i] = ESP_OK;
    }
}
