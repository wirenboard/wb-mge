#include "bridge.h"

int mock_bridge_port_init_called[BRIDGES_COUNT] = {0};
unsigned mock_bridge_port_init_index[BRIDGES_COUNT] = {0};
esp_err_t mock_bridge_port_init_return_value[BRIDGES_COUNT] = {ESP_OK, ESP_OK};

int mock_bridge_port_deinit_called[BRIDGES_COUNT] = {0};
unsigned mock_bridge_port_deinit_index[BRIDGES_COUNT] = {0};
esp_err_t mock_bridge_port_deinit_return_value[BRIDGES_COUNT] = {ESP_OK, ESP_OK};

int mock_bridge_port_check_settings_changed_called[BRIDGES_COUNT] = {0};
bool mock_bridge_port_check_settings_changed_return_value[BRIDGES_COUNT] = {false, false};

esp_err_t bridge_port_init(unsigned index)
{
    if (index < BRIDGES_COUNT) {
        mock_bridge_port_init_index[index] = index;
        mock_bridge_port_init_called[index]++;
        return mock_bridge_port_init_return_value[index];
    }
    return ESP_FAIL;
}

esp_err_t bridge_port_deinit(unsigned index)
{
    if (index < BRIDGES_COUNT) {
        mock_bridge_port_deinit_index[index] = index;
        mock_bridge_port_deinit_called[index]++;
        return mock_bridge_port_deinit_return_value[index];
    }
    return ESP_FAIL;
}

bool bridge_port_check_settings_changed(unsigned index)
{
    if (index < BRIDGES_COUNT) {
        mock_bridge_port_check_settings_changed_called[index]++;
        return mock_bridge_port_check_settings_changed_return_value[index];
    }
    return false;
}

void mock_bridge_reset(void)
{
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        mock_bridge_port_init_called[i] = 0;
        mock_bridge_port_init_index[i] = 0;
        mock_bridge_port_init_return_value[i] = ESP_OK;

        mock_bridge_port_deinit_called[i] = 0;
        mock_bridge_port_deinit_index[i] = 0;
        mock_bridge_port_deinit_return_value[i] = ESP_OK;

        mock_bridge_port_check_settings_changed_called[i] = 0;
        mock_bridge_port_check_settings_changed_return_value[i] = false;
    }
}
