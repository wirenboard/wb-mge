#pragma once

#include "esp_err.h"

/*
 * Start the Modbus->MQTT bridge as a FreeRTOS task.
 * Reads settings from NVS (KEY_MQTT_*, KEY_MQTS_*, KEY_BAUDRATE1/2, etc.)
 * and starts polling if KEY_MQTS_ENABLED == true and KEY_MQTT_ENABLED == true.
 *
 * Safe to call multiple times: stops existing task before restarting.
 */
esp_err_t mqtt_serial_bridge_start(void);

/* Stop the bridge task (blocking until stopped). */
void mqtt_serial_bridge_stop(void);
