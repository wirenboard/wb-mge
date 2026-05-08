#pragma once

#include <stdbool.h>

bool validate_hostname(const char *value);
bool validate_ssid(const char *value);
bool validate_port(const char *value);
bool validate_timeout(const char *value);
bool validate_baudrate(const char *value);
bool validate_stopbits(const char *value);
bool validate_parity(const char *value);
bool validate_databits(const char *value);
bool validate_ip(const char *value);
bool validate_wifi_mode(const char *value);
bool validate_wifi_auth(const char *value);
bool validate_bridge_mode(const char *value);
bool validate_bool(const char *value);
bool validate_login(const char *value);
bool validate_password(const char *value);
