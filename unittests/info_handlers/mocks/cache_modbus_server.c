/* cache_modbus_server mock for the info_handlers unit test.
 *
 * info_get_handler() reports the runtime listening port via
 * cache_modbus_server_get_port(). This mock exposes that port as settable state so a
 * test can stand up both a running server (non-zero port) and a stopped/failed one
 * (0), mirroring the real module: s_port is set after a successful init and cleared
 * after a successful deinit. */

#include "bridge/cache_modbus_server.h"

static int mock_running_port = 0;

void mock_cache_modbus_server_set_port(int port)
{
    mock_running_port = port;
}

void mock_cache_modbus_server_reset(void)
{
    mock_running_port = 0;
}

esp_err_t cache_modbus_server_init(int port)
{
    mock_running_port = port;
    return ESP_OK;
}

esp_err_t cache_modbus_server_deinit(void)
{
    mock_running_port = 0;
    return ESP_OK;
}

int cache_modbus_server_get_port(void)
{
    return mock_running_port;
}
