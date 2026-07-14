#include "port_manager.h"
#include "call_sequence.h"

int mock_port_manager_check_settings_changed_called[BRIDGES_COUNT] = {0};
bool mock_port_manager_check_settings_changed_return_value[BRIDGES_COUNT] = {false, false};

int mock_port_manager_apply_settings_called[BRIDGES_COUNT] = {0};
unsigned mock_port_manager_apply_settings_index[BRIDGES_COUNT] = {0};
esp_err_t mock_port_manager_apply_settings_return_value[BRIDGES_COUNT] = {ESP_OK, ESP_OK};
unsigned mock_port_manager_apply_settings_call_seq[BRIDGES_COUNT] = {0};

int mock_port_manager_release_called[BRIDGES_COUNT] = {0};
unsigned mock_port_manager_release_call_seq[BRIDGES_COUNT] = {0};
int mock_port_manager_release_skipped[BRIDGES_COUNT] = {0};
int mock_port_manager_apply_settings_skipped[BRIDGES_COUNT] = {0};

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

// Like the real one: an inner ports_frozen() check under the port lock. It lives HERE, not in the
// caller, so a test cannot pass by relying on an unlocked pre-check in settings_update — and the
// counter below only ever moves when a port really was torn down.
esp_err_t port_manager_release(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_port_manager_ports_frozen_return_value) {
        mock_port_manager_release_skipped[port_index]++;
        return ESP_OK;
    }
    mock_port_manager_release_called[port_index]++;
    mock_port_manager_release_call_seq[port_index] = call_sequence_get_call_id();
    return ESP_OK;
}

esp_err_t port_manager_apply_settings(unsigned port_index)
{
    // Same contract as the real entry point: a no-op under the port lock while the ports are frozen.
    if ((port_index < BRIDGES_COUNT) && mock_port_manager_ports_frozen_return_value) {
        mock_port_manager_apply_settings_skipped[port_index]++;
        return ESP_OK;
    }
    if (port_index < BRIDGES_COUNT) {
        mock_port_manager_apply_settings_index[port_index] = port_index;
        mock_port_manager_apply_settings_called[port_index]++;
        mock_port_manager_apply_settings_call_seq[port_index] = call_sequence_get_call_id();
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
        mock_port_manager_apply_settings_call_seq[i] = 0;
        mock_port_manager_apply_settings_return_value[i] = ESP_OK;
        mock_port_manager_apply_settings_skipped[i] = 0;

        mock_port_manager_release_called[i] = 0;
        mock_port_manager_release_call_seq[i] = 0;
        mock_port_manager_release_skipped[i] = 0;
    }
}
