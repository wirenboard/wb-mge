#include "mqtt_client.h"

#include <stdio.h>
#include <string.h>

/* The handle the mock hands out. Only its address matters - nothing dereferences it. */
static struct esp_mqtt_client {
    int dummy;
} mock_client_instance;

int mock_mqtt_init_called = 0;
int mock_mqtt_start_called = 0;
int mock_mqtt_stop_called = 0;
int mock_mqtt_destroy_called = 0;
bool mock_mqtt_init_should_fail = false;

mock_mqtt_config_t mock_mqtt_config;

mock_mqtt_message_t mock_mqtt_messages[MOCK_MQTT_MAX_MESSAGES];
int mock_mqtt_message_count = 0;

char mock_mqtt_subscriptions[MOCK_MQTT_MAX_MESSAGES][MOCK_MQTT_MAX_TOPIC_LEN];
int mock_mqtt_subscription_count = 0;

int mock_mqtt_messages_before_start = 0;

static esp_event_handler_t registered_handler;
static void *registered_handler_arg;
static bool client_started;

static void copy_string(char *dst, size_t dst_len, const char *src)
{
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

void mock_mqtt_reset(void)
{
    mock_mqtt_init_called = 0;
    mock_mqtt_start_called = 0;
    mock_mqtt_stop_called = 0;
    mock_mqtt_destroy_called = 0;
    mock_mqtt_init_should_fail = false;
    mock_mqtt_message_count = 0;
    mock_mqtt_subscription_count = 0;
    mock_mqtt_messages_before_start = 0;
    registered_handler = NULL;
    registered_handler_arg = NULL;
    client_started = false;

    memset(&mock_mqtt_config, 0, sizeof(mock_mqtt_config));
    memset(mock_mqtt_messages, 0, sizeof(mock_mqtt_messages));
    memset(mock_mqtt_subscriptions, 0, sizeof(mock_mqtt_subscriptions));
}

void mock_mqtt_fire_event(esp_mqtt_event_id_t event_id, const char *topic, const char *data)
{
    if (registered_handler == NULL) {
        return;
    }

    /* The real client hands over pointers into its own receive buffer, neither of which is null
     * terminated, so the mock does the same: a length is the only thing the handler may trust. */
    char topic_buf[MOCK_MQTT_MAX_TOPIC_LEN];
    char data_buf[MOCK_MQTT_MAX_PAYLOAD_LEN];

    esp_mqtt_event_t event = {0};
    event.event_id = event_id;
    event.client = &mock_client_instance;

    if (topic != NULL) {
        copy_string(topic_buf, sizeof(topic_buf), topic);
        event.topic = topic_buf;
        event.topic_len = (int)strlen(topic_buf);
    }
    if (data != NULL) {
        copy_string(data_buf, sizeof(data_buf), data);
        event.data = data_buf;
        event.data_len = (int)strlen(data_buf);
        event.total_data_len = event.data_len;
    }

    registered_handler(registered_handler_arg, "MQTT_EVENTS", (int32_t)event_id, &event);
}

const mock_mqtt_message_t *mock_mqtt_find_message(const char *topic)
{
    for (int i = 0; i < mock_mqtt_message_count; i++) {
        if (strcmp(mock_mqtt_messages[i].topic, topic) == 0) {
            return &mock_mqtt_messages[i];
        }
    }
    return NULL;
}

const mock_mqtt_message_t *mock_mqtt_find_last_message(const char *topic)
{
    for (int i = mock_mqtt_message_count - 1; i >= 0; i--) {
        if (strcmp(mock_mqtt_messages[i].topic, topic) == 0) {
            return &mock_mqtt_messages[i];
        }
    }
    return NULL;
}

int mock_mqtt_count_messages(const char *topic)
{
    int count = 0;
    for (int i = 0; i < mock_mqtt_message_count; i++) {
        if (strcmp(mock_mqtt_messages[i].topic, topic) == 0) {
            count++;
        }
    }
    return count;
}

int mock_mqtt_message_index(const char *topic)
{
    for (int i = 0; i < mock_mqtt_message_count; i++) {
        if (strcmp(mock_mqtt_messages[i].topic, topic) == 0) {
            return i;
        }
    }
    return -1;
}

bool mock_mqtt_is_subscribed(const char *topic)
{
    for (int i = 0; i < mock_mqtt_subscription_count; i++) {
        if (strcmp(mock_mqtt_subscriptions[i], topic) == 0) {
            return true;
        }
    }
    return false;
}

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config)
{
    mock_mqtt_init_called++;

    if (mock_mqtt_init_should_fail) {
        return NULL;
    }
    if (config != NULL) {
        copy_string(mock_mqtt_config.host, sizeof(mock_mqtt_config.host), config->broker.address.hostname);
        mock_mqtt_config.port = config->broker.address.port;
        mock_mqtt_config.transport = config->broker.address.transport;

        mock_mqtt_config.has_username = (config->credentials.username != NULL);
        mock_mqtt_config.has_password = (config->credentials.authentication.password != NULL);
        copy_string(mock_mqtt_config.username, sizeof(mock_mqtt_config.username), config->credentials.username);
        copy_string(mock_mqtt_config.password, sizeof(mock_mqtt_config.password),
                    config->credentials.authentication.password);
        copy_string(mock_mqtt_config.client_id, sizeof(mock_mqtt_config.client_id), config->credentials.client_id);

        copy_string(mock_mqtt_config.will_topic, sizeof(mock_mqtt_config.will_topic), config->session.last_will.topic);
        copy_string(mock_mqtt_config.will_msg, sizeof(mock_mqtt_config.will_msg), config->session.last_will.msg);
        mock_mqtt_config.will_qos = config->session.last_will.qos;
        mock_mqtt_config.will_retain = config->session.last_will.retain;
    }

    return &mock_client_instance;
}

esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, int32_t event,
                                         esp_event_handler_t event_handler, void *event_handler_arg)
{
    (void)client;
    (void)event;

    registered_handler = event_handler;
    registered_handler_arg = event_handler_arg;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client)
{
    (void)client;

    mock_mqtt_start_called++;
    mock_mqtt_messages_before_start = mock_mqtt_message_count;
    client_started = true;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client)
{
    (void)client;

    mock_mqtt_stop_called++;
    client_started = false;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client)
{
    (void)client;

    mock_mqtt_destroy_called++;
    registered_handler = NULL;
    registered_handler_arg = NULL;
    return ESP_OK;
}

int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data,
                            int len, int qos, int retain)
{
    (void)client;
    (void)len;

    if (mock_mqtt_message_count >= MOCK_MQTT_MAX_MESSAGES) {
        return -1;
    }

    mock_mqtt_message_t *message = &mock_mqtt_messages[mock_mqtt_message_count++];
    copy_string(message->topic, sizeof(message->topic), topic);
    copy_string(message->payload, sizeof(message->payload), data);
    message->qos = qos;
    message->retain = (retain != 0);

    return mock_mqtt_message_count;
}

int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char *topic, int qos)
{
    (void)client;
    (void)qos;

    if (mock_mqtt_subscription_count >= MOCK_MQTT_MAX_MESSAGES) {
        return -1;
    }

    copy_string(mock_mqtt_subscriptions[mock_mqtt_subscription_count],
                MOCK_MQTT_MAX_TOPIC_LEN, topic);
    mock_mqtt_subscription_count++;

    return mock_mqtt_subscription_count;
}
