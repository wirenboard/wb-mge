#include "unity.h"

/* The include path provides the real main/bridge/bridge.h for types and
 * BRIDGES_COUNT (since -I $(PROJ_DIR)/main/bridge is in Makefile INC). */
#include "bridge.h"
#include "bridge_mock.h"
#include <string.h>

/* Mock tracking state definitions */
mock_bridge_calls_t mock_bridge_calls[BRIDGES_COUNT] = {0};
bool mock_bridge_port_init_should_fail = false;
bool mock_bridge_port_init_serial_only_should_fail = false;

serial_desc_t mock_serial_desc_instances[BRIDGES_COUNT];
serial_desc_t *mock_bridge_serial_desc[BRIDGES_COUNT];

esp_err_t bridge_port_init(unsigned index)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "bridge_port_init: invalid index");
    mock_bridge_calls[index].bridge_port_init_called++;
    if (mock_bridge_port_init_should_fail) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t bridge_port_deinit(unsigned index)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "bridge_port_deinit: invalid index");
    mock_bridge_calls[index].bridge_port_deinit_called++;
    return ESP_OK;
}

esp_err_t bridge_port_init_serial_only(unsigned index, serial_desc_t **out)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "bridge_port_init_serial_only: invalid index");
    TEST_ASSERT_NOT_NULL(out);
    mock_bridge_calls[index].bridge_port_init_serial_only_called++;
    if (mock_bridge_port_init_serial_only_should_fail) {
        return ESP_FAIL;
    }
    *out = &mock_serial_desc_instances[index];
    mock_bridge_serial_desc[index] = &mock_serial_desc_instances[index];
    return ESP_OK;
}

serial_desc_t *bridge_get_serial_desc(unsigned index)
{
    if (index >= BRIDGES_COUNT) {
        return NULL;
    }
    mock_bridge_calls[index].bridge_get_serial_desc_called++;
    return mock_bridge_serial_desc[index];
}

esp_err_t bridge_read_serial_config(unsigned index, serial_config_t *config)
{
    if ((index >= BRIDGES_COUNT) || (!config)) {
        return ESP_ERR_INVALID_ARG;
    }
    mock_bridge_calls[index].bridge_read_serial_config_called++;
    memset(config, 0, sizeof(*config));
    return ESP_OK;
}

bool bridge_port_check_settings_changed(unsigned index)
{
    (void)index;
    return false;
}

esp_err_t bridge_init(void)
{
    return ESP_OK;
}

int tcp_server_active_connections(tcp_server_num_t server_num)
{
    (void)server_num;
    return 0;
}

esp_err_t bridge_disable_port(unsigned index)
{
    (void)index;
    return ESP_OK;
}

esp_err_t bridge_enable_port(unsigned index)
{
    (void)index;
    return ESP_OK;
}

void mock_bridge_reset(void)
{
    memset(mock_bridge_calls, 0, sizeof(mock_bridge_calls));
    mock_bridge_port_init_should_fail = false;
    mock_bridge_port_init_serial_only_should_fail = false;
    memset(mock_serial_desc_instances, 0, sizeof(mock_serial_desc_instances));
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        mock_bridge_serial_desc[i] = &mock_serial_desc_instances[i];
    }
}
