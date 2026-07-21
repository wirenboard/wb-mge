#include "setting_validators.h"
#include "setting_items.h"
#include <string.h>

bool mock_validate_hostname_called = false;
bool mock_validate_ssid_called = false;
bool mock_validate_port_called = false;
bool mock_validate_timeout_called = false;
bool mock_validate_baudrate_called = false;
bool mock_validate_stopbits_called = false;
bool mock_validate_parity_called = false;
bool mock_validate_databits_called = false;
bool mock_validate_ip_called = false;
bool mock_validate_wifi_mode_called = false;
bool mock_validate_wifi_auth_called = false;
bool mock_validate_bridge_mode_called = false;
bool mock_validate_port_mode_called = false;
bool mock_validate_bool_called = false;
bool mock_validate_login_called = false;
bool mock_validate_password_called = false;
bool mock_validate_wifi_password_called = false;
bool mock_validate_modbus_slave_id_called = false;

void mock_reset_validator_flags(void)
{
    mock_validate_hostname_called = false;
    mock_validate_ssid_called = false;
    mock_validate_port_called = false;
    mock_validate_timeout_called = false;
    mock_validate_baudrate_called = false;
    mock_validate_stopbits_called = false;
    mock_validate_parity_called = false;
    mock_validate_databits_called = false;
    mock_validate_ip_called = false;
    mock_validate_wifi_mode_called = false;
    mock_validate_wifi_auth_called = false;
    mock_validate_bridge_mode_called = false;
    mock_validate_port_mode_called = false;
    mock_validate_bool_called = false;
    mock_validate_login_called = false;
    mock_validate_password_called = false;
    mock_validate_wifi_password_called = false;
    mock_validate_modbus_slave_id_called = false;
}

bool validate_hostname(const char *value)
{
    mock_validate_hostname_called = true;

    if (value == NULL) {
        return false;
    }

    if (strlen(value) == 0) {
        return false;
    }

    return true;
}

bool validate_ssid(const char *value)
{
    mock_validate_ssid_called = true;
    return true;
}

bool validate_port(const char *value)
{
    mock_validate_port_called = true;
    return true;
}

bool validate_timeout(const char *value)
{
    mock_validate_timeout_called = true;
    return true;
}

bool validate_baudrate(const char *value)
{
    mock_validate_baudrate_called = true;
    return true;
}

bool validate_stopbits(const char *value)
{
    mock_validate_stopbits_called = true;
    return true;
}

bool validate_parity(const char *value)
{
    mock_validate_parity_called = true;
    return true;
}

bool validate_databits(const char *value)
{
    mock_validate_databits_called = true;
    return true;
}

bool validate_ip(const char *value)
{
    mock_validate_ip_called = true;
    return true;
}

bool validate_wifi_mode(const char *value)
{
    mock_validate_wifi_mode_called = true;
    return true;
}

bool validate_wifi_auth(const char *value)
{
    mock_validate_wifi_auth_called = true;
    return true;
}

bool validate_bridge_mode(const char *value)
{
    mock_validate_bridge_mode_called = true;

    // Unlike the other mocks this one must mirror the real accept-list from
    // setting_validators.c: the legacy port_mode migration in setting_items.c uses the
    // RESULT of this validator to decide whether a legacy bridge_mode record is stale.
    if (value == NULL) {
        return false;
    }
    return (strncmp(value, BRIDGE_MODE_SERVER_STR, SETTING_ITEM_MAX_STR_LEN) == 0) ||
           (strncmp(value, BRIDGE_MODE_CLIENT_STR, SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_port_mode(const char *value)
{
    mock_validate_port_mode_called = true;
    return true;
}

bool validate_bool(const char *value)
{
    mock_validate_bool_called = true;
    return true;
}

bool validate_login(const char *value)
{
    mock_validate_login_called = true;
    return true;
}

bool validate_password(const char *value)
{
    mock_validate_password_called = true;
    return true;
}

bool validate_wifi_password(const char *value)
{
    (void)value;
    mock_validate_wifi_password_called = true;
    return true;
}

bool validate_modbus_slave_id(const char *value)
{
    mock_validate_modbus_slave_id_called = true;
    return true;
}
