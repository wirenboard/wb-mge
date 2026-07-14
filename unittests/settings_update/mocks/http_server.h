#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

// Mirrors the real http_server.h: the port the web UI falls back to when NVS holds no usable
// web_port, and the last-resort port the settings update task tries when neither the configured nor
// the previously served port will bind.
#define HTTP_SERVER_DEFAULT_PORT 80

// Ordered log of every port passed to http_server_init()/http_server_init_port() since reset.
#define MOCK_HTTP_INIT_PORT_LOG_MAX 8

// init() and init_port() share these counters: both start the server, they only differ in where the
// port comes from (NVS vs the explicit fallback port).
extern int mock_http_server_init_called;
extern esp_err_t mock_http_server_init_return_value;

extern int mock_http_server_deinit_called;
extern esp_err_t mock_http_server_deinit_return_value;

extern int mock_http_server_check_settings_changed_called;
extern bool mock_http_server_check_settings_changed_return_value;

// The port NVS asks for: what http_server_init() tries to bind.
extern uint16_t mock_http_server_configured_port;

// Per-port failure injection (0 = off), mirroring the cache server mock: init() fails for this port
// only, so a test can make the NEW port fail while the rollback to the OLD port still succeeds.
extern uint16_t mock_http_server_init_fail_port;
extern esp_err_t mock_http_server_init_fail_error;

// The mirror image of the above (0 = off): init() succeeds for this port and fails for every other
// one. Lets a test kill both the new and the old port while leaving the last-resort default port
// bindable.
extern uint16_t mock_http_server_init_ok_port;

extern uint16_t mock_http_server_init_ports[MOCK_HTTP_INIT_PORT_LOG_MAX];
extern uint16_t mock_http_server_init_last_port;

// Global call id (call_sequence_get_call_id()) of the FIRST init() / deinit() since reset.
// Lets a test assert the two-phase order against port_manager and the cache Modbus server.
extern unsigned mock_http_server_init_call_seq;
extern unsigned mock_http_server_deinit_call_seq;

// How many vTaskDelay() calls had already happened when deinit() was called (-1 = deinit was not
// called). The web UI socket may only be torn down AFTER the delay that lets the response to the
// current POST /settings reach the client.
extern int mock_http_server_delays_before_deinit;

void mock_http_server_reset(void);
void mock_http_server_set_running_port(uint16_t port);

esp_err_t http_server_init(void);
esp_err_t http_server_init_port(uint16_t port);
esp_err_t http_server_deinit(void);
uint16_t http_server_get_port(void);
bool http_server_check_settings_changed(void);
