#pragma once

#include <esp_err.h>
#include <stdbool.h>

esp_err_t mqtt_manager_init(void);
void mqtt_manager_restart(void);
bool mqtt_manager_is_connected(void);
