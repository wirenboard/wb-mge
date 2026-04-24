#pragma once
/*
 * Thin MQTT abstraction.
 *
 * Linux PoC: backed by libmosquitto.
 * MCU: swap this file for an ESP-IDF/arduino mqtt wrapper;
 * the bridge.c code only calls the four functions below.
 */

#include <stdint.h>

typedef struct mqtt_client mqtt_client_t;

typedef void (*mqtt_message_cb_t)(const char *topic, const char *payload, void *userdata);

/*
 * Connect to an MQTT broker.
 * client_id: unique string for this client
 * on_message: called for every incoming message on subscribed topics
 * userdata: opaque pointer passed to the callback
 * Returns NULL on error.
 */
mqtt_client_t *mqtt_connect(const char *host, int port,
                             const char *client_id,
                             mqtt_message_cb_t on_message, void *userdata);

void mqtt_disconnect(mqtt_client_t *c);

/* Publish a string payload (QoS 0, retain=0). Returns 0 on success. */
int mqtt_publish(mqtt_client_t *c, const char *topic, const char *payload);

/* Subscribe to a topic. Returns 0 on success. */
int mqtt_subscribe(mqtt_client_t *c, const char *topic);

/*
 * Process pending network I/O (incoming messages, keepalive).
 * Call this regularly from the main loop (every ~100 ms).
 * Returns 0 on success.
 */
int mqtt_loop(mqtt_client_t *c);
