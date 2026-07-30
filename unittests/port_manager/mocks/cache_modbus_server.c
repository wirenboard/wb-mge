#include "cache_modbus_server.h"
#include <stdbool.h>

/* Simple stubs — cache_modbus_server functionality is not exercised by
 * port_manager lifecycle tests. */

int mock_cache_modbus_server_init_called = 0;

/* Drives the failure path of the real init: the reassembly mutex that would not allocate
 * (ESP_ERR_NO_MEM, cache_modbus_server.c:427), anything tcp_server_init() cannot allocate
 * (ESP_ERR_NO_MEM, tcp_server.c:604,612,622,643 — descriptor, event group, connection mutex,
 * acceptor task), or a listen() refused with ERR_USE because cache_modbus_port collides with
 * another local listener (ESP_FAIL, tcp_server.c:598). The mock reports ESP_FAIL for all three
 * because the caller only tests != ESP_OK (port_manager.c:557), so the specific code is
 * immaterial here. The collision covers the web port too: every listener on this device now
 * binds the same dual-stack address, so lwIP refuses the second listen() instead of allowing
 * two listeners on one port. The full reasoning is in port_manager_init(). */
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
