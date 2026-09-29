#pragma once

/*
 * The MQTT client that puts this device on a Wiren Board controller's bus.
 *
 * It owns the connection and nothing else: what a topic looks like and what goes in it is
 * wb_device.c, next to this file. The connection is left to esp-mqtt, which reconnects on its
 * own - there is deliberately no task here waiting for the network to come up, and no retry
 * loop. On every CONNACK the whole device is published again, because a broker that was
 * restarted has kept none of it.
 *
 * Everything it needs is in the settings: mqtt_enabled, mqtt_host, mqtt_port, mqtt_user,
 * mqtt_pass and mqtt_id. A client with mqtt_enabled false, or with an empty host, is simply not
 * started.
 */

#include <stdbool.h>

#include "esp_err.h"

// Read the settings and connect, if the settings say to. Called once from app_main() after
// network_init(). Returns ESP_OK when the client was started or when MQTT is switched off;
// anything else means the client could not be created.
esp_err_t mqtt_client_wb_start(void);

// Tear the connection down. Safe to call when nothing is running.
void mqtt_client_wb_stop(void);

// Stop and start again, so the client picks up changed settings. Called from the settings apply.
void mqtt_client_wb_restart(void);

// Whether any of the six mqtt_* settings differs from what the running client was started with.
bool mqtt_client_wb_check_settings_changed(void);

#ifdef __unittest_env__
    // One pass of the publishing task's body, and the way back to a cold start. The task itself
    // is an endless loop around the first of these, so a host test drives it by hand.
    void mqtt_client_wb_test_tick(void);
    void mqtt_client_wb_test_reset(void);
#endif
