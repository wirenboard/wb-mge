/* Mock of esp-mqtt.
 *
 * The suite runs no broker and opens no socket: this stands in for the whole component, records
 * the configuration the client was built with, keeps the event handler it was given so a test
 * can deliver a CONNACK by hand, and logs every publish and subscribe in call order.
 *
 * The configuration struct mirrors the real one's nesting exactly, because the code under test
 * fills it with designated initialisers (.broker.address.hostname and friends). */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define MOCK_MQTT_MAX_MESSAGES      128
#define MOCK_MQTT_MAX_TOPIC_LEN     160
#define MOCK_MQTT_MAX_PAYLOAD_LEN   512

typedef struct esp_mqtt_client *esp_mqtt_client_handle_t;

typedef const char *esp_event_base_t;

#define ESP_EVENT_ANY_ID            (-1)

typedef enum {
    MQTT_EVENT_ERROR = 0,
    MQTT_EVENT_CONNECTED,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_SUBSCRIBED,
    MQTT_EVENT_UNSUBSCRIBED,
    MQTT_EVENT_PUBLISHED,
    MQTT_EVENT_DATA,
    MQTT_EVENT_BEFORE_CONNECT,
    MQTT_EVENT_DELETED,
} esp_mqtt_event_id_t;

typedef enum {
    MQTT_TRANSPORT_UNKNOWN = 0,
    MQTT_TRANSPORT_OVER_TCP,
    MQTT_TRANSPORT_OVER_SSL,
    MQTT_TRANSPORT_OVER_WS,
    MQTT_TRANSPORT_OVER_WSS,
} esp_mqtt_transport_t;

typedef struct esp_mqtt_event_t {
    esp_mqtt_event_id_t event_id;
    esp_mqtt_client_handle_t client;
    char *data;
    int data_len;
    int total_data_len;
    int current_data_offset;
    char *topic;
    int topic_len;
    int msg_id;
    int session_present;
} esp_mqtt_event_t;

typedef esp_mqtt_event_t *esp_mqtt_event_handle_t;

typedef struct esp_mqtt_client_config_t {
    struct broker_t {
        struct address_t {
            const char *uri;
            const char *hostname;
            esp_mqtt_transport_t transport;
            const char *path;
            uint32_t port;
        } address;
    } broker;
    struct credentials_t {
        const char *username;
        const char *client_id;
        struct authentication_t {
            const char *password;
        } authentication;
    } credentials;
    struct session_t {
        struct last_will_t {
            const char *topic;
            const char *msg;
            int msg_len;
            int qos;
            int retain;
        } last_will;
        int keepalive;
    } session;
    struct network_t {
        int reconnect_timeout_ms;
        bool disable_auto_reconnect;
    } network;
} esp_mqtt_client_config_t;

typedef void (*esp_event_handler_t)(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data);

/* One message the code under test handed to the client. */
typedef struct {
    char topic[MOCK_MQTT_MAX_TOPIC_LEN];
    char payload[MOCK_MQTT_MAX_PAYLOAD_LEN];
    int qos;
    bool retain;
} mock_mqtt_message_t;

/* The whole configuration, with the strings copied out of it: the client under test may free or
 * reuse its own buffers, and a test asserting on a dangling pointer proves nothing. */
typedef struct {
    char host[MOCK_MQTT_MAX_TOPIC_LEN];
    char username[MOCK_MQTT_MAX_TOPIC_LEN];
    char password[MOCK_MQTT_MAX_TOPIC_LEN];
    char client_id[MOCK_MQTT_MAX_TOPIC_LEN];
    char will_topic[MOCK_MQTT_MAX_TOPIC_LEN];
    char will_msg[MOCK_MQTT_MAX_PAYLOAD_LEN];
    uint32_t port;
    esp_mqtt_transport_t transport;
    int will_qos;
    int will_retain;
    bool has_username;
    bool has_password;
} mock_mqtt_config_t;

extern int mock_mqtt_init_called;
extern int mock_mqtt_start_called;
extern int mock_mqtt_stop_called;
extern int mock_mqtt_destroy_called;
extern bool mock_mqtt_init_should_fail;

extern mock_mqtt_config_t mock_mqtt_config;

extern mock_mqtt_message_t mock_mqtt_messages[MOCK_MQTT_MAX_MESSAGES];
extern int mock_mqtt_message_count;

extern char mock_mqtt_subscriptions[MOCK_MQTT_MAX_MESSAGES][MOCK_MQTT_MAX_TOPIC_LEN];
extern int mock_mqtt_subscription_count;

/* Number of messages published before the client was started. The will is not one of them - it
 * travels inside CONNECT - so this must stay zero for a correctly ordered bring-up. */
extern int mock_mqtt_messages_before_start;

void mock_mqtt_reset(void);

/* Deliver an event to the handler the client registered. Does nothing when no handler has been
 * registered, which is itself what a test can assert on. */
void mock_mqtt_fire_event(esp_mqtt_event_id_t event_id, const char *topic, const char *data);

/* The messages a given topic received, oldest first, or NULL when it received none. */
const mock_mqtt_message_t *mock_mqtt_find_message(const char *topic);
const mock_mqtt_message_t *mock_mqtt_find_last_message(const char *topic);
int mock_mqtt_count_messages(const char *topic);
int mock_mqtt_message_index(const char *topic);
bool mock_mqtt_is_subscribed(const char *topic);

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, int32_t event,
                                         esp_event_handler_t event_handler, void *event_handler_arg);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);
int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data,
                            int len, int qos, int retain);
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char *topic, int qos);
