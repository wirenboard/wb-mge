#pragma once

#include <esp_err.h>

esp_err_t mqtt_manager_init(void);
void mqtt_manager_restart(void);
