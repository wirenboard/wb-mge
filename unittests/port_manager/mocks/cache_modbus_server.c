#include "cache_modbus_server.h"

/* Simple stubs — cache_modbus_server functionality is not exercised by
 * port_manager lifecycle tests. */

int mock_cache_modbus_server_init_called = 0;

esp_err_t cache_modbus_server_init(int port)
{
    (void)port;
    mock_cache_modbus_server_init_called++;
    return ESP_OK;
}

esp_err_t cache_modbus_server_deinit(void)
{
    return ESP_OK;
}

int cache_modbus_server_get_port(void)
{
    return 0;
}

void mock_cache_modbus_server_reset(void)
{
    mock_cache_modbus_server_init_called = 0;
}
