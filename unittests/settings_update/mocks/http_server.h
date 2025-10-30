#pragma once

#include <esp_err.h>
#include <stdbool.h>

extern int mock_http_server_init_called;
extern esp_err_t mock_http_server_init_return_value;

extern int mock_http_server_deinit_called;
extern esp_err_t mock_http_server_deinit_return_value;

extern int mock_http_server_check_settings_changed_called;
extern bool mock_http_server_check_settings_changed_return_value;

void mock_http_server_reset(void);

esp_err_t http_server_init(void);
esp_err_t http_server_deinit(void);
bool http_server_check_settings_changed(void);
