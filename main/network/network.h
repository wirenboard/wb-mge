#pragma once

#include "esp_err.h"


esp_err_t network_init(void);

bool network_check_eth_settings_changed(void);
esp_err_t network_update_eth_settings(void);

bool network_check_mdns_settings_changed(void);
esp_err_t network_update_mdns_settings(void);
