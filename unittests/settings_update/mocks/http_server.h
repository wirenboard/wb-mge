#pragma once

#include <esp_err.h>
#include <stdbool.h>

extern int mock_http_server_init_called;
extern esp_err_t mock_http_server_init_return_value;

extern int mock_http_server_deinit_called;
extern esp_err_t mock_http_server_deinit_return_value;

extern int mock_http_server_check_settings_changed_called;
extern bool mock_http_server_check_settings_changed_return_value;

// Global call id (call_sequence_get_call_id()) of the FIRST init() / deinit() since reset.
// Lets a test assert the two-phase order against port_manager and the cache Modbus server.
extern unsigned mock_http_server_init_call_seq;
extern unsigned mock_http_server_deinit_call_seq;

// How many vTaskDelay() calls had already happened when deinit() was called (-1 = deinit was not
// called). The web UI socket may only be torn down AFTER the delay that lets the response to the
// current POST /settings reach the client.
extern int mock_http_server_delays_before_deinit;

void mock_http_server_reset(void);

esp_err_t http_server_init(void);
esp_err_t http_server_deinit(void);
bool http_server_check_settings_changed(void);
