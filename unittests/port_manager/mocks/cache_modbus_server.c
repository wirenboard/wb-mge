#include "cache_modbus_server.h"
#include <stdbool.h>

/* Simple stubs — cache_modbus_server functionality is not exercised by
 * port_manager lifecycle tests. */

int mock_cache_modbus_server_init_called = 0;

/* Drives the failure path of the real init: the reassembly mutex that would not allocate
 * (ESP_ERR_NO_MEM, cache_modbus_server.c:427), anything tcp_server_init() cannot allocate
 * (ESP_ERR_NO_MEM, tcp_server.c:537,545,555,576 — descriptor, event group, connection mutex,
 * acceptor task), or a listen() refused with ERR_USE because cache_modbus_port collides with a
 * BRIDGE port (ESP_FAIL, tcp_server.c:531). The mock reports ESP_FAIL for all three because the
 * caller only tests != ESP_OK (port_manager.c:556), so the specific code is immaterial here.
 * NOT a collision with the web port — that one yields two listeners rather than an error; the
 * full reasoning is in port_manager_init(). */
bool mock_cache_modbus_server_init_should_fail = false;

esp_err_t cache_modbus_server_init(int port)
{
    (void)port;
    mock_cache_modbus_server_init_called++;
    return mock_cache_modbus_server_init_should_fail ? ESP_FAIL : ESP_OK;
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
    mock_cache_modbus_server_init_should_fail = false;
}
