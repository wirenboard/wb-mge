#pragma once

#include <stdbool.h>

extern bool mock_validate_hostname_called;
extern bool mock_validate_ssid_called;
extern bool mock_validate_sta_ssid_called;
extern bool mock_validate_port_called;
extern bool mock_validate_slave_id_called;
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
extern bool mock_validate_wifi_password_called;

void mock_reset_validator_flags(void);

bool validate_hostname(const char *value);
bool validate_ssid(const char *value);
bool validate_sta_ssid(const char *value);
bool validate_port(const char *value);
bool validate_slave_id(const char *value);
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
bool validate_wifi_password(const char *value);

/* Fingerprint access control and MQTT validators, stubbed to accept anything — see the note in
 * the mock's .c file. */
bool validate_lock_channel(const char *value);
bool validate_lock_pulse_ms(const char *value);
bool validate_lock_mode(const char *value);
bool validate_lock_exit_input(const char *value);
bool validate_tg_token(const char *value);
bool validate_tg_admins(const char *value);
bool validate_tg_api_url(const char *value);
bool validate_tg_notify_chat(const char *value);
bool validate_mqtt_host(const char *value);
bool validate_mqtt_credential(const char *value);
bool validate_mqtt_id(const char *value);
