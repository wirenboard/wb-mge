#pragma once

#include <stdbool.h>

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
bool validate_update_channel(const char *value);
bool validate_bool(const char *value);
bool validate_login(const char *value);
bool validate_password(const char *value);
bool validate_wifi_password(const char *value);

// Fingerprint access control. lock_addr reuses validate_slave_id(): the relay's unit id takes
// the 1..247 any Modbus unit id may. The two Telegram validators accept the empty string - no bot
// configured, no admins yet.
bool validate_lock_channel(const char *value);
bool validate_lock_pulse_ms(const char *value);
bool validate_tg_token(const char *value);
bool validate_tg_admins(const char *value);

// MQTT settings. All three string validators accept the empty string - an unconfigured broker,
// an anonymous connection and an unnamed device are all legitimate states - and mqtt_port reuses
// validate_port(). validate_mqtt_id() is the strict one: what it accepts ends up inside a topic.
bool validate_mqtt_host(const char *value);
bool validate_mqtt_credential(const char *value);
bool validate_mqtt_id(const char *value);
