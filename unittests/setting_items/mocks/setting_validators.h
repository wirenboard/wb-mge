#pragma once

#include <stdbool.h>

extern bool mock_validate_hostname_called;
extern bool mock_validate_ssid_called;
extern bool mock_validate_port_called;
extern bool mock_validate_baudrate_called;
extern bool mock_validate_stopbits_called;
extern bool mock_validate_parity_called;
extern bool mock_validate_databits_called;
extern bool mock_validate_ip_called;
extern bool mock_validate_wifi_mode_called;
extern bool mock_validate_wifi_auth_called;
extern bool mock_validate_bool_called;
extern bool mock_validate_login_called;
extern bool mock_validate_password_called;

void mock_reset_validator_flags(void);

bool validate_hostname(const char *value);
bool validate_ssid(const char *value);
bool validate_port(const char *value);
bool validate_baudrate(const char *value);
bool validate_stopbits(const char *value);
bool validate_parity(const char *value);
bool validate_databits(const char *value);
bool validate_ip(const char *value);
bool validate_wifi_mode(const char *value);
bool validate_wifi_auth(const char *value);
bool validate_bool(const char *value);
bool validate_login(const char *value);
bool validate_password(const char *value);
