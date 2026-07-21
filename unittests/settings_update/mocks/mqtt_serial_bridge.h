#pragma once

#include "esp_err.h"

extern int mock_mqtt_serial_bridge_start_called;
extern int mock_mqtt_serial_bridge_stop_called;

void mock_mqtt_serial_bridge_reset(void);

esp_err_t mqtt_serial_bridge_start(void);
void mqtt_serial_bridge_stop(void);
