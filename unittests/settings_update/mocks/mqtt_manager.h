#pragma once

#include "esp_err.h"

extern int mock_mqtt_manager_init_called;
extern int mock_mqtt_manager_restart_called;

void mock_mqtt_manager_reset(void);

esp_err_t mqtt_manager_init(void);
void mqtt_manager_restart(void);
