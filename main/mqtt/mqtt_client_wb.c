#include "mqtt_client_wb.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "setting_items.h"
#include "wb_device.h"

static const char *TAG = "mqtt_client_wb";

#define MQTT_WB_TASK_STACK_SIZE     (4 * 1024)
#define MQTT_WB_TASK_PRIORITY       3

// How often the controls are refreshed. Only what changed is actually published, so an idle
// mixer costs one pass of comparisons a second and no traffic at all.
#define MQTT_WB_PUBLISH_PERIOD_MS   1000

// Everything this device publishes is state rather than an event, so it all goes out at QoS 1 -
// a message that is lost on the way to the broker would leave a retained topic stale until the
// value changes again, which for a control that rarely moves can be a long time.
#define MQTT_WB_QOS                 1

// How many events from the broker may be in flight towards the publishing task. The panel
// sends commands one at a time, so the queue is only ever more than one deep while the task is
// busy inside a full republish.
#define MQTT_WB_EVENT_QUEUE_LEN     8

// The longest payload that can still be a command of this device: wb_device_handle_command()
// refuses anything longer than five digits, so a payload that does not fit here is not one.
#define MQTT_WB_PAYLOAD_MAX_LEN     16

typedef struct {
    bool enabled;
    int  port;
    char host[SETTING_ITEM_MAX_STR_LEN];
    char user[SETTING_ITEM_MAX_STR_LEN];
    char pass[SETTING_ITEM_MAX_STR_LEN];
    char id[WB_DEVICE_ID_MAX_LEN];
} mqtt_wb_config_t;

typedef enum {
    MQTT_WB_EVENT_SESSION = 0,  // a fresh broker session; only a wake-up, see session_pending
    MQTT_WB_EVENT_COMMAND,      // one message from a control's /on topic
} mqtt_wb_event_type_t;

// One event as it travels from the esp-mqtt task to the publishing task. The strings are
// copied rather than pointed at: what esp-mqtt hands the handler lives in its own receive
// buffer, which is reused the moment the handler returns.
typedef struct {
    mqtt_wb_event_type_t type;
    int  topic_len;
    int  payload_len;
    char topic[WB_DEVICE_TOPIC_MAX_LEN];
    char payload[MQTT_WB_PAYLOAD_MAX_LEN];
} mqtt_wb_event_t;

static esp_mqtt_client_handle_t client;

// The settings the running client was built from. What mqtt_client_wb_check_settings_changed()
// compares against, so it is updated on every start - including a start that does nothing
// because MQTT is switched off, otherwise switching it on would not read as a change.
static mqtt_wb_config_t applied;
static bool applied_valid;

// The will topic esp-mqtt is handed. Static rather than on the stack of the start below: the
// client keeps its configuration for as long as it runs.
static char will_topic[WB_DEVICE_TOPIC_MAX_LEN];

// Raised on CONNACK and cleared on disconnect. Read by the publishing task, written by the
// esp-mqtt event task, hence the atomics.
static bool connected;

// "A session has started and nothing has been published into it yet." A flag rather than a
// queue item, because the queue may be full and a lost session start would leave the device
// invisible on the broker until the next reconnect - the republish is what clears the will's
// error and puts the controls back.
static bool session_pending;

/*
 * The one lock of this module. It guards BOTH the client handle and everything the wb_device
 * layer keeps, because the two are inseparable: a publish reads the handle and updates the
 * "what has already been published" cache in the same breath. Guarding the handle is what makes
 * a restart safe - mqtt_client_wb_stop() destroys the client under this lock, so it can never
 * be destroyed between a NULL check and the publish that follows it, nor while one is running.
 *
 * What must NEVER happen is the esp-mqtt event handler taking it. That handler runs with
 * esp-mqtt's own API lock held, and esp_mqtt_client_publish() takes that same API lock - so a
 * handler waiting here while the publishing task waited for the API lock inside a publish would
 * hang MQTT until the next reboot. Hence the queue below: the handler copies what arrived, hands
 * it over and returns, and every call into wb_device is made by the publishing task alone.
 */
static SemaphoreHandle_t mqtt_mutex;
static StaticSemaphore_t mqtt_mutex_buffer;

// What the esp-mqtt task hands the publishing task, and what wakes that task early: it waits on
// this queue instead of sleeping out the publish period, so a command from the panel is applied
// when it lands rather than up to a second later.
static QueueHandle_t event_queue;

static TaskHandle_t publish_task_handle;


static bool mqtt_wb_lock(void)
{
    if (mqtt_mutex == NULL) {
        return false;
    }
    return (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE);
}

static void mqtt_wb_unlock(void)
{
    if (mqtt_mutex != NULL) {
        xSemaphoreGive(mqtt_mutex);
    }
}

static esp_err_t mqtt_wb_publish(void *ctx, const char *topic, const char *payload, bool retain)
{
    (void)ctx;

    // The handle is read from the module rather than from ctx on purpose: a restart replaces it,
    // and a transport that had captured the old one would publish into a destroyed client.
    // Reading it needs no lock of its own: wb_device is only ever entered with mqtt_mutex held,
    // and nothing else may replace the handle while it is.
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, MQTT_WB_QOS, retain ? 1 : 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Publish to %s failed", topic);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t mqtt_wb_subscribe(void *ctx, const char *topic)
{
    (void)ctx;

    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_subscribe(client, topic, MQTT_WB_QOS);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Subscribe to %s failed", topic);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static const wb_device_transport_t mqtt_wb_transport = {
    .publish = mqtt_wb_publish,
    .subscribe = mqtt_wb_subscribe,
    .ctx = NULL,
};

// Read the six mqtt_* settings. An empty mqtt_id is not a name: it means "call this device what
// the model and the MAC make of it", which is what a device that was never configured answers to.
static void mqtt_wb_read_config(mqtt_wb_config_t *out)
{
    memset(out, 0, sizeof(*out));

    out->enabled = setting_items_read_bool(KEY_MQTT_ENABLED);
    out->port = setting_items_read_int(KEY_MQTT_PORT);
    setting_items_read(KEY_MQTT_HOST, out->host);
    setting_items_read(KEY_MQTT_USER, out->user);
    setting_items_read(KEY_MQTT_PASS, out->pass);

    char stored_id[SETTING_ITEM_MAX_STR_LEN] = {0};
    setting_items_read(KEY_MQTT_ID, stored_id);
    if (stored_id[0] != '\0') {
        strncpy(out->id, stored_id, sizeof(out->id) - 1);
    } else {
        wb_device_build_default_id(out->id, sizeof(out->id));
    }
}

static bool mqtt_wb_config_equal(const mqtt_wb_config_t *a, const mqtt_wb_config_t *b)
{
    return ((a->enabled == b->enabled) &&
            (a->port == b->port) &&
            (strncmp(a->host, b->host, sizeof(a->host)) == 0) &&
            (strncmp(a->user, b->user, sizeof(a->user)) == 0) &&
            (strncmp(a->pass, b->pass, sizeof(a->pass)) == 0) &&
            (strncmp(a->id, b->id, sizeof(a->id)) == 0));
}

// Hand one event to the publishing task. Never waits: this runs on the esp-mqtt task, which is
// holding the client's API lock, so anything that blocks here blocks the whole client.
static void mqtt_wb_post_event(const mqtt_wb_event_t *event)
{
    if (event_queue == NULL) {
        return;
    }
    if (xQueueSend(event_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "The event queue is full, dropping an event of type %d", (int)event->type);
    }
}

static void mqtt_wb_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_id;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (event == NULL) {
        return;
    }

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "Connected to the broker, handing the session to the publishing task");
        __atomic_store_n(&connected, true, __ATOMIC_SEQ_CST);
        __atomic_store_n(&session_pending, true, __ATOMIC_SEQ_CST);

        // The flag above is what the task acts on; this is only the wake-up that saves it a
        // wait, so losing it to a full queue costs a second and nothing else.
        mqtt_wb_event_t wake = { .type = MQTT_WB_EVENT_SESSION };
        mqtt_wb_post_event(&wake);
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        // esp-mqtt reconnects by itself; nothing to do here but stop publishing into a socket
        // that is not there.
        ESP_LOGW(TAG, "Disconnected from the broker");
        __atomic_store_n(&connected, false, __ATOMIC_SEQ_CST);
        break;

    case MQTT_EVENT_DATA: {
        // A payload split across several events arrives with current_data_offset > 0 and no
        // topic at all. Commands are a handful of bytes and never split, so such a fragment is
        // not a command of ours.
        if ((event->topic == NULL) || (event->current_data_offset != 0)) {
            break;
        }

        mqtt_wb_event_t command = { .type = MQTT_WB_EVENT_COMMAND };
        if ((event->topic_len <= 0) || ((size_t)event->topic_len >= sizeof(command.topic)) ||
            (event->data_len < 0) || ((size_t)event->data_len >= sizeof(command.payload))) {
            // Longer than any topic this device subscribes to, or than any payload a control of
            // it takes - so it is not a command of ours, and there is nothing to hand over.
            break;
        }

        memcpy(command.topic, event->topic, (size_t)event->topic_len);
        command.topic_len = event->topic_len;
        if (event->data_len > 0) {
            memcpy(command.payload, event->data, (size_t)event->data_len);
        }
        command.payload_len = event->data_len;

        mqtt_wb_post_event(&command);
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error reported by the client");
        break;

    default:
        break;
    }
}

// Take what the esp-mqtt task left, waiting up to wait_ticks for the first event and draining
// whatever else has piled up behind it without waiting again.
static void mqtt_wb_process_events(TickType_t wait_ticks)
{
    if (event_queue == NULL) {
        // Only reachable when the queue could not be created. The task must still pace itself.
        if (wait_ticks > 0) {
            vTaskDelay(wait_ticks);
        }
        return;
    }

    mqtt_wb_event_t event;
    while (xQueueReceive(event_queue, &event, wait_ticks) == pdTRUE) {
        wait_ticks = 0;

        if (event.type != MQTT_WB_EVENT_COMMAND) {
            continue;
        }
        if (!mqtt_wb_lock()) {
            return;
        }
        wb_device_handle_command(event.topic, event.topic_len, event.payload, event.payload_len);
        mqtt_wb_unlock();
    }
}

// One pass of the publishing task: what the broker sent, then a new session if one has started,
// and then the controls whose value changed. Nothing is published while there is no connection
// to put it on.
static void mqtt_wb_tick(TickType_t wait_ticks)
{
    mqtt_wb_process_events(wait_ticks);

    // The flag is taken BEFORE the connection is read, and in that order deliberately: a session
    // that starts in between sets the flag again after this exchange, so a republish can be
    // dropped here but never lost.
    bool new_session = __atomic_exchange_n(&session_pending, false, __ATOMIC_SEQ_CST);

    if (!__atomic_load_n(&connected, __ATOMIC_SEQ_CST)) {
        return;
    }

    if (!mqtt_wb_lock()) {
        return;
    }
    if (new_session) {
        // A broker that was restarted kept none of the retained topics, and a broker that was
        // not still has to be told the will is over - so every session republishes everything
        // rather than trusting what the last one left behind. It also leaves nothing for the
        // comparison pass to find, which is why that pass is the other branch and not the next
        // statement.
        wb_device_reset();
        wb_device_publish_all();
    } else {
        wb_device_publish_changed();
    }
    mqtt_wb_unlock();
}

static void mqtt_wb_publish_task(void *arg)
{
    (void)arg;

    while (1) {
        // The wait happens inside the tick, on the event queue: a quiet device comes round once
        // a second, and one that is being talked to answers at once.
        mqtt_wb_tick(pdMS_TO_TICKS(MQTT_WB_PUBLISH_PERIOD_MS));
    }
}

esp_err_t mqtt_client_wb_start(void)
{
    if (client != NULL) {
        ESP_LOGW(TAG, "The MQTT client is already running");
        return ESP_OK;
    }

    if (mqtt_mutex == NULL) {
        // Static allocation cannot fail for a non-NULL buffer.
        mqtt_mutex = xSemaphoreCreateMutexStatic(&mqtt_mutex_buffer);
    }
    if (mqtt_mutex == NULL) {
        // Unreachable, and checked once here so that every mqtt_wb_lock() below is known to
        // succeed: a client started without the lock is one nothing could publish into and
        // nothing could destroy.
        ESP_LOGE(TAG, "Failed to create the MQTT lock");
        return ESP_FAIL;
    }
    if (event_queue == NULL) {
        // One queue for the whole lifetime of the firmware, like the task below: a restart of
        // the client neither needs a new one nor may drop what is already in this one.
        event_queue = xQueueCreate(MQTT_WB_EVENT_QUEUE_LEN, sizeof(mqtt_wb_event_t));
        if (event_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create the MQTT event queue, commands will be ignored");
        }
    }

    mqtt_wb_config_t config;
    mqtt_wb_read_config(&config);

    // Recorded even when nothing is started: it is what "the settings changed" is measured
    // against, and switching MQTT on has to read as a change.
    applied = config;
    applied_valid = true;

    if (!config.enabled) {
        ESP_LOGI(TAG, "MQTT is switched off");
        return ESP_OK;
    }
    if (config.host[0] == '\0') {
        ESP_LOGW(TAG, "MQTT is switched on but no broker is configured");
        return ESP_OK;
    }

    // wb_device belongs to the publishing task, so binding it to this client is done under the
    // lock like every other touch of it - that task may well be in the middle of a pass.
    if (mqtt_wb_lock()) {
        wb_device_init(config.id, &mqtt_wb_transport);
        mqtt_wb_unlock();
    }

    // The will is registered BEFORE the connection, because that is the only time a broker
    // accepts one: it is part of CONNECT. It publishes "rw" into the device's error topic, so a
    // device that drops off the network is marked unreadable and unwritable without anything
    // having to notice that it is gone. The empty string that clears it is published by
    // wb_device_publish_all() on CONNACK.
    if (wb_device_topic_error(config.id, will_topic, sizeof(will_topic)) != ESP_OK) {
        ESP_LOGE(TAG, "Device id '%s' makes a topic that does not fit", config.id);
        return ESP_ERR_INVALID_SIZE;
    }

    // Every string below comes from `applied` rather than from the local copy: esp-mqtt does
    // duplicate what it is given, but this configuration outlives the call either way and there
    // is no reason to hand it a pointer into a stack frame that is about to go.
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = applied.host,
        .broker.address.port = (uint32_t)applied.port,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.username = (applied.user[0] != '\0') ? applied.user : NULL,
        .credentials.authentication.password = (applied.pass[0] != '\0') ? applied.pass : NULL,
        .credentials.client_id = applied.id,
        .session.last_will.topic = will_topic,
        .session.last_will.msg = WB_DEVICE_ERROR_OFFLINE,
        .session.last_will.msg_len = (int)strlen(WB_DEVICE_ERROR_OFFLINE),
        .session.last_will.qos = MQTT_WB_QOS,
        .session.last_will.retain = 1,
    };

    // Built as a local and only then published into `client`, under the lock: the publishing
    // task reads that variable, and a client that is not yet registered and started is not one
    // it may publish into.
    esp_mqtt_client_handle_t handle = esp_mqtt_client_init(&mqtt_cfg);
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to create the MQTT client");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(handle, ESP_EVENT_ANY_ID, mqtt_wb_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register the MQTT event handler: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(handle);
        return ret;
    }

    if (mqtt_wb_lock()) {
        client = handle;
        mqtt_wb_unlock();
    }

    ret = esp_mqtt_client_start(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the MQTT client: %s", esp_err_to_name(ret));
        mqtt_client_wb_stop();
        return ret;
    }

    // One task for the whole lifetime of the firmware, not one per connection: it sits idle
    // while there is no connection and a restart of the client does not need a new one.
    if (publish_task_handle == NULL) {
        BaseType_t created = xTaskCreate(mqtt_wb_publish_task, "mqtt_wb_task", MQTT_WB_TASK_STACK_SIZE,
                                         NULL, MQTT_WB_TASK_PRIORITY, &publish_task_handle);
        if (created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create the MQTT publishing task");
            publish_task_handle = NULL;
        }
    }

    ESP_LOGI(TAG, "MQTT client started as '%s', broker %s:%d", config.id, config.host, config.port);
    return ESP_OK;
}

void mqtt_client_wb_stop(void)
{
    // Under the lock from end to end, so the handle cannot be destroyed while the publishing
    // task is holding it - that task reaches the transport only with this same lock taken.
    if (!mqtt_wb_lock()) {
        // No mutex means nothing was ever started: it is created by the start above.
        return;
    }

    if (client != NULL) {
        // A goodbye publish IS needed, and the will does not cover it: the will fires only when
        // the connection dies, and esp_mqtt_client_stop() sends a proper DISCONNECT, on which
        // the broker DROPS the will instead of publishing it. Without this the retained topics
        // of a device whose MQTT was just switched off would stay on the broker for good, with
        // an empty error next to them, and the panel would go on showing a healthy mixer. So
        // the marker the will carries is published by hand, before the connection is closed.
        if (__atomic_load_n(&connected, __ATOMIC_SEQ_CST) && (will_topic[0] != '\0')) {
            esp_mqtt_client_publish(client, will_topic, WB_DEVICE_ERROR_OFFLINE, 0,
                                    MQTT_WB_QOS, 1);
        }

        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = NULL;
    }

    __atomic_store_n(&connected, false, __ATOMIC_SEQ_CST);
    // Whatever session was waiting to be published belonged to the client that has just gone.
    __atomic_store_n(&session_pending, false, __ATOMIC_SEQ_CST);

    mqtt_wb_unlock();
}

void mqtt_client_wb_restart(void)
{
    mqtt_client_wb_stop();
    mqtt_client_wb_start();
}

bool mqtt_client_wb_check_settings_changed(void)
{
    mqtt_wb_config_t config;
    mqtt_wb_read_config(&config);

    if (!applied_valid) {
        // Nothing has been applied yet, so there is nothing the settings can differ from.
        return false;
    }
    return !mqtt_wb_config_equal(&config, &applied);
}

#ifdef __unittest_env__
    void mqtt_client_wb_test_tick(void)
    {
        // Without waiting: a host test has no other task to wait for, and everything the queue
        // holds was put there by the test itself before the call.
        mqtt_wb_tick(0);
    }

    void mqtt_client_wb_test_reset(void)
    {
        client = NULL;
        connected = false;
        session_pending = false;
        mqtt_mutex = NULL;
        event_queue = NULL;
        publish_task_handle = NULL;
        applied_valid = false;
        memset(&applied, 0, sizeof(applied));
        memset(will_topic, 0, sizeof(will_topic));
    }
#endif
