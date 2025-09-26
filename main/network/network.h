#pragma once

#include "esp_err.h"
#include "esp_wifi.h"


esp_err_t network_init(void);

bool network_check_eth_settings_changed(void);
esp_err_t network_update_eth_settings(void);

bool network_check_wifi_settings_changed(void);
esp_err_t network_update_wifi_settings(void);

bool network_check_mdns_settings_changed(void);
esp_err_t network_update_mdns_settings(void);

wifi_mode_t network_get_wifi_mode(void);
