#include "mqtt_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------
 * Linux / PoC backend: libmosquitto
 * On an MCU replace with your MQTT library calls.
 * ------------------------------------------------------------------ */
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)

#include <mosquitto.h>

struct mqtt_client {
    struct mosquitto   *mosq;
    mqtt_message_cb_t   cb;
    void               *userdata;
};

static void on_message_wrapper(struct mosquitto *mosq,
                                void *obj,
                                const struct mosquitto_message *msg)
{
    (void)mosq;
    mqtt_client_t *c = (mqtt_client_t *)obj;
    if (!c->cb || !msg->payload || msg->payloadlen <= 0) return;

    /* Mosquitto does not guarantee null-termination of payload.
     * Copy to a stack buffer and add '\0' before passing to the callback. */
    int plen = msg->payloadlen;
    char *payload = malloc((size_t)plen + 1);
    if (!payload) return;
    memcpy(payload, msg->payload, (size_t)plen);
    payload[plen] = '\0';
    c->cb(msg->topic, payload, c->userdata);
    free(payload);
}

mqtt_client_t *mqtt_connect(const char *host, int port,
                             const char *client_id,
                             mqtt_message_cb_t on_message, void *userdata)
{
    mosquitto_lib_init();

    struct mosquitto *mosq = mosquitto_new(client_id, true, NULL);
    if (!mosq) {
        fprintf(stderr, "mqtt: mosquitto_new failed\n");
        return NULL;
    }

    mqtt_client_t *c = malloc(sizeof(*c));
    if (!c) { mosquitto_destroy(mosq); return NULL; }
    c->mosq     = mosq;
    c->cb       = on_message;
    c->userdata = userdata;

    mosquitto_user_data_set(mosq, c);
    mosquitto_message_callback_set(mosq, on_message_wrapper);

    int rc = mosquitto_connect(mosq, host, port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "mqtt: connect to %s:%d failed: %s\n",
                host, port, mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        free(c);
        return NULL;
    }

    return c;
}

void mqtt_disconnect(mqtt_client_t *c)
{
    if (!c) return;
    mosquitto_disconnect(c->mosq);
    mosquitto_destroy(c->mosq);
    mosquitto_lib_cleanup();
    free(c);
}

int mqtt_publish(mqtt_client_t *c, const char *topic, const char *payload)
{
    /* QoS 0, retain=true so new subscribers immediately see the current value */
    int rc = mosquitto_publish(c->mosq, NULL, topic,
                               (int)strlen(payload), payload, 0, true);
    return rc == MOSQ_ERR_SUCCESS ? 0 : -1;
}

int mqtt_subscribe(mqtt_client_t *c, const char *topic)
{
    int rc = mosquitto_subscribe(c->mosq, NULL, topic, 0);
    return rc == MOSQ_ERR_SUCCESS ? 0 : -1;
}

int mqtt_loop(mqtt_client_t *c)
{
    int rc = mosquitto_loop(c->mosq, 100, 1);
    return rc == MOSQ_ERR_SUCCESS ? 0 : -1;
}

#else /* MCU ------------------------------------------------------- */
/*
 * Replace with your MCU MQTT implementation.
 * Example: esp_mqtt_client (ESP-IDF), PubSubClient (Arduino).
 * The bridge.c only calls mqtt_connect/disconnect/publish/subscribe/loop.
 *
 * Minimal ESP-IDF sketch:
 *
 *   struct mqtt_client { esp_mqtt_client_handle_t h; ... };
 *
 *   mqtt_client_t *mqtt_connect(...) {
 *       esp_mqtt_client_config_t cfg = { .broker.address.hostname = host,
 *                                        .broker.address.port = port };
 *       esp_mqtt_client_handle_t h = esp_mqtt_client_init(&cfg);
 *       esp_mqtt_client_register_event(h, ESP_EVENT_ANY_ID, event_handler, c);
 *       esp_mqtt_client_start(h);
 *       ...
 *   }
 */
#error "Implement MQTT backend for your MCU (see comment above)"
#endif
