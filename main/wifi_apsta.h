#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t wifi_init_apsta(char* ap_ssid, char* ap_pass, char* sta_ssid, char* sta_pass);
