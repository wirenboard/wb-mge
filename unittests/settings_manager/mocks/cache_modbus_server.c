// Mock for cache_modbus_server used by settings_manager unit tests.

#include "cache_modbus_server.h"

// Current mock state
static int  mock_running_port = 0;

// Controllable return codes
esp_err_t mock_cache_modbus_server_init_error = ESP_OK;
esp_err_t mock_cache_modbus_server_deinit_error = ESP_OK;

// Per-port failure injection: when armed (port != 0), cache_modbus_server_init()
// returns mock_cache_modbus_server_init_fail_error for that specific port only,
// while all other ports succeed. This lets a test make init() fail on a new port
// but succeed on the rollback (old) port — needed to cover the port-change
// rollback branch in settings_manager. A value of 0 disables per-port injection.
int       mock_cache_modbus_server_init_fail_port  = 0;
esp_err_t mock_cache_modbus_server_init_fail_error = ESP_FAIL;

// Call counters
int mock_cache_modbus_server_init_call_count = 0;
int mock_cache_modbus_server_deinit_call_count = 0;
int mock_cache_modbus_server_init_last_port = 0;

// Ordered record of every port passed to cache_modbus_server_init() since reset.
// Lets a test assert both the failed init(new_port) and the rollback init(old_port).
#define MOCK_INIT_PORT_LOG_MAX 8
int mock_cache_modbus_server_init_ports[MOCK_INIT_PORT_LOG_MAX] = { 0 };

void mock_cache_modbus_server_reset(void)
{
    mock_running_port = 0;
    mock_cache_modbus_server_init_error = ESP_OK;
    mock_cache_modbus_server_deinit_error = ESP_OK;
    mock_cache_modbus_server_init_fail_port = 0;
    mock_cache_modbus_server_init_fail_error = ESP_FAIL;
    mock_cache_modbus_server_init_call_count = 0;
    mock_cache_modbus_server_deinit_call_count = 0;
    mock_cache_modbus_server_init_last_port = 0;
    for (int i = 0; i < MOCK_INIT_PORT_LOG_MAX; i++) {
        mock_cache_modbus_server_init_ports[i] = 0;
    }
}

void mock_cache_modbus_server_set_running_port(int port)
{
    mock_running_port = port;
}

esp_err_t cache_modbus_server_init(int port)
{
    if (mock_cache_modbus_server_init_call_count < MOCK_INIT_PORT_LOG_MAX) {
        mock_cache_modbus_server_init_ports[mock_cache_modbus_server_init_call_count] = port;
    }
    mock_cache_modbus_server_init_call_count++;
    mock_cache_modbus_server_init_last_port = port;
    // Per-port failure injection takes precedence over the global error.
    if ((mock_cache_modbus_server_init_fail_port != 0) &&
        (port == mock_cache_modbus_server_init_fail_port)) {
        return mock_cache_modbus_server_init_fail_error;
    }
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
