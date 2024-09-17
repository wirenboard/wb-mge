#pragma once

#include "esp_err.h"
#include "ssdp.h"

esp_err_t http_server_init(ssdp_config_t* ssdp_config);
