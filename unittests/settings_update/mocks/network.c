#include "network.h"

int mock_network_update_mdns_settings_called = 0;
esp_err_t mock_network_update_mdns_settings_return_value = ESP_OK;

int mock_network_check_mdns_settings_changed_called = 0;
bool mock_network_check_mdns_settings_changed_return_value = false;

int mock_network_update_eth_settings_called = 0;
esp_err_t mock_network_update_eth_settings_return_value = ESP_OK;

int mock_network_check_eth_settings_changed_called = 0;
bool mock_network_check_eth_settings_changed_return_value = false;

int mock_network_update_wifi_settings_called = 0;
esp_err_t mock_network_update_wifi_settings_return_value = ESP_OK;

int mock_network_check_wifi_settings_changed_called = 0;
bool mock_network_check_wifi_settings_changed_return_value = false;

esp_err_t network_update_mdns_settings(void)
{
    mock_network_update_mdns_settings_called++;
    return mock_network_update_mdns_settings_return_value;
}

bool network_check_mdns_settings_changed(void)
{
    mock_network_check_mdns_settings_changed_called++;
    return mock_network_check_mdns_settings_changed_return_value;
}

esp_err_t network_update_eth_settings(void)
{
    mock_network_update_eth_settings_called++;
    return mock_network_update_eth_settings_return_value;
}

bool network_check_eth_settings_changed(void)
{
    mock_network_check_eth_settings_changed_called++;
    return mock_network_check_eth_settings_changed_return_value;
}

esp_err_t network_update_wifi_settings(void)
{
    mock_network_update_wifi_settings_called++;
    return mock_network_update_wifi_settings_return_value;
}

bool network_check_wifi_settings_changed(void)
{
    mock_network_check_wifi_settings_changed_called++;
    return mock_network_check_wifi_settings_changed_return_value;
}

void mock_network_reset(void)
{
    mock_network_update_mdns_settings_called = 0;
    mock_network_update_mdns_settings_return_value = ESP_OK;

    mock_network_check_mdns_settings_changed_called = 0;
    mock_network_check_mdns_settings_changed_return_value = false;

    mock_network_update_eth_settings_called = 0;
    mock_network_update_eth_settings_return_value = ESP_OK;

    mock_network_check_eth_settings_changed_called = 0;
    mock_network_check_eth_settings_changed_return_value = false;

    mock_network_update_wifi_settings_called = 0;
    mock_network_update_wifi_settings_return_value = ESP_OK;

    mock_network_check_wifi_settings_changed_called = 0;
    mock_network_check_wifi_settings_changed_return_value = false;
}
