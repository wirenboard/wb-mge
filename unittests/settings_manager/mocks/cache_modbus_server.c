// Mock for cache_modbus_server used by settings_manager unit tests.

#include "cache_modbus_server.h"

// Current mock state
static int  mock_running_port = 0;

// Controllable return codes
esp_err_t mock_cache_modbus_server_init_error = ESP_OK;
esp_err_t mock_cache_modbus_server_deinit_error = ESP_OK;

// Call counters
int mock_cache_modbus_server_init_call_count = 0;
int mock_cache_modbus_server_deinit_call_count = 0;
int mock_cache_modbus_server_init_last_port = 0;

void mock_cache_modbus_server_reset(void)
{
    mock_running_port = 0;
    mock_cache_modbus_server_init_error = ESP_OK;
    mock_cache_modbus_server_deinit_error = ESP_OK;
    mock_cache_modbus_server_init_call_count = 0;
    mock_cache_modbus_server_deinit_call_count = 0;
    mock_cache_modbus_server_init_last_port = 0;
}

void mock_cache_modbus_server_set_running_port(int port)
{
    mock_running_port = port;
}

esp_err_t cache_modbus_server_init(int port)
{
    mock_cache_modbus_server_init_call_count++;
    mock_cache_modbus_server_init_last_port = port;
    if (mock_cache_modbus_server_init_error != ESP_OK) {
        return mock_cache_modbus_server_init_error;
    }
    mock_running_port = port;
    return ESP_OK;
}

esp_err_t cache_modbus_server_deinit(void)
{
    mock_cache_modbus_server_deinit_call_count++;
    if (mock_cache_modbus_server_deinit_error != ESP_OK) {
        return mock_cache_modbus_server_deinit_error;
    }
    mock_running_port = 0;
    return ESP_OK;
}

int cache_modbus_server_get_port(void)
{
    return mock_running_port;
}
