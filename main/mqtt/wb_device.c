#include "wb_device.h"

// Compiled only where the fingerprint access control is (see main/fp_acs/fp_acs.h).
#include "fp_acs.h"

#if FP_ACS_ENABLED

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "fp_store.h"

static const char *TAG = "wb_device";

#define WB_TOPIC_PREFIX             "/devices/"
#define WB_TOPIC_CONTROLS           "/controls/"
#define WB_TOPIC_VALUE              ""
#define WB_TOPIC_META               "/meta"
#define WB_TOPIC_META_ERROR         "/meta/error"
#define WB_TOPIC_COMMAND            "/on"
#define WB_TOPIC_ANY_CONTROL        "+"

#define WB_DRIVER_NAME              "wb-mge-fp-acs"

// An empty error string, and - retained - the way to delete a retained topic from the broker.
#define WB_PAYLOAD_EMPTY            ""

#define MAC_BYTES                   6

// The panel sends a switch as "0" or "1" and nothing else.
#define WB_SWITCH_OFF               "0"
#define WB_SWITCH_ON                "1"
#define WB_SWITCH_LEN               1
#define WB_SWITCH_MAX               1

// user_<id>, id 1..FP_STORE_USERS_MAX: at most three digits, no leading zero.
#define WB_USER_CONTROL_PREFIX      "user_"
#define WB_USER_CONTROL_MAX_LEN     16
#define WB_USER_ID_MAX_DIGITS       3
#define WB_DECIMAL_BASE             10u

// event_count carries a uint32_t sequence number.
#define WB_EVENT_COUNT_MAX          4294967295.0

_Static_assert(WB_DEVICE_VALUE_MAX_LEN > FP_STORE_NAME_MAX, "a user name must fit in a control value");

/*
 * The fixed controls, in the order they are published. The enum value IS the "order" field of
 * the meta (plus one); the user controls come after them. event_count is deliberately last: a
 * rule triggered by it reads the other three as they are for the same event.
 */
typedef enum {
    WB_CONTROL_SENSOR_ONLINE = 0,
    WB_CONTROL_LAST_RESULT,
    WB_CONTROL_LAST_USER,
    WB_CONTROL_LAST_SOURCE,
    WB_CONTROL_EVENT_COUNT,
    WB_CONTROL_COUNT
} wb_control_id_t;

typedef struct {
    const char *type;               // convention control type
    bool        has_range;          // min, max and precision are published (numbers and switches)
    double      max;                // min is always 0
    double      precision;          // 0 = do not publish a precision
    const char *title_en;
    const char *title_ru;
} wb_control_desc_t;

typedef struct {
    const char       *name;
    wb_control_desc_t desc;
} wb_control_t;

// Indexed by wb_control_id_t. Every one of them is read-only.
static const wb_control_t controls[WB_CONTROL_COUNT] = {
    [WB_CONTROL_SENSOR_ONLINE] = {
        .name = "sensor_online",
        .desc = { .type = "switch", .has_range = true, .max = WB_SWITCH_MAX,
                  .title_en = "Sensor online", .title_ru = "Связь со сканером" },
    },
    [WB_CONTROL_LAST_RESULT] = {
        .name = "last_result",
        .desc = { .type = "text", .title_en = "Last result", .title_ru = "Последний результат" },
    },
    [WB_CONTROL_LAST_USER] = {
        .name = "last_user",
        .desc = { .type = "text", .title_en = "Last user", .title_ru = "Последний пользователь" },
    },
    [WB_CONTROL_LAST_SOURCE] = {
        .name = "last_source",
        .desc = { .type = "text", .title_en = "Last source", .title_ru = "Последний способ" },
    },
    [WB_CONTROL_EVENT_COUNT] = {
        .name = "event_count",
        .desc = { .type = "value", .has_range = true, .max = WB_EVENT_COUNT_MAX, .precision = 1,
                  .title_en = "Event count", .title_ru = "Счётчик событий" },
    },
};

// What the broker holds for the user control of one user id.
typedef struct {
    bool on_broker;                         // its topics may exist; survives a session reset
    bool meta_valid;
    char name[FP_STORE_NAME_MAX + 1];       // the title in the published meta
    bool value_valid;
    bool enabled;                           // the published value
} wb_user_state_t;

static char device_id[WB_DEVICE_ID_MAX_LEN];
static wb_device_transport_t transport;

// What the broker has been told. A value is republished only when it differs from what is here.
static char last_value[WB_CONTROL_COUNT][WB_DEVICE_VALUE_MAX_LEN];
static bool last_value_valid[WB_CONTROL_COUNT];

// Indexed by user id - 1. The user controls are brought in line with the store only when its
// users_rev moves, which is every change to a user, enabling and disabling included.
static wb_user_state_t users[FP_STORE_USERS_MAX];
static bool            users_rev_valid;
static uint32_t        users_rev;


void wb_device_build_default_id(char *buf, size_t len)
{
    if ((buf == NULL) || (len == 0)) {
        return;
    }

    uint8_t mac[MAC_BYTES] = {0};
    if (esp_read_mac(mac, ESP_MAC_ETH) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read the MAC, falling back to the bare model name");
        snprintf(buf, len, "%s", BASE_HOSTNAME);
        return;
    }

    snprintf(buf, len, "%s-%02X%02X%02X", BASE_HOSTNAME, mac[3], mac[4], mac[5]);
}

// Every topic of this device is this one call with a different tail. Returns
// ESP_ERR_INVALID_SIZE rather than publishing to a truncated topic.
static esp_err_t wb_device_build_topic(char *buf, size_t len, const char *device,
                                       const char *control, const char *tail)
{
    if ((buf == NULL) || (len == 0) || (device == NULL) || (device[0] == '\0')) {
        return ESP_ERR_INVALID_ARG;
    }

    int written;
    if (control != NULL) {
        written = snprintf(buf, len, WB_TOPIC_PREFIX "%s" WB_TOPIC_CONTROLS "%s%s",
                           device, control, tail);
    } else {
        written = snprintf(buf, len, WB_TOPIC_PREFIX "%s%s", device, tail);
    }

    if ((written < 0) || ((size_t)written >= len)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t wb_device_topic_error(const char *device_id_str, char *buf, size_t len)
{
    return wb_device_build_topic(buf, len, device_id_str, NULL, WB_TOPIC_META_ERROR);
}

static esp_err_t wb_device_publish(const char *topic, const char *payload, bool retain)
{
    if ((transport.publish == NULL) || (topic == NULL) || (payload == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    return transport.publish(transport.ctx, topic, payload, retain);
}

// One retained topic of a control: its value (WB_TOPIC_VALUE), meta or meta/error.
static esp_err_t wb_device_publish_control_topic(const char *control, const char *tail, const char *payload)
{
    char topic[WB_DEVICE_TOPIC_MAX_LEN];
    esp_err_t err = wb_device_build_topic(topic, sizeof(topic), device_id, control, tail);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "A topic of control %s does not fit", control);
        return err;
    }
    return wb_device_publish(topic, payload, true);
}

static char *wb_device_meta_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON *title = cJSON_CreateObject();
    if (title == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddStringToObject(root, "driver", WB_DRIVER_NAME);
    cJSON_AddStringToObject(title, "en", "Fingerprint access");
    cJSON_AddStringToObject(title, "ru", "Доступ по отпечатку");
    cJSON_AddItemToObject(root, "title", title);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

// The meta JSON of one control, as a malloc'd string the caller frees, or NULL when it could not
// be built.
static char *wb_device_control_meta_json(const wb_control_desc_t *desc, bool readonly, int order)
{
    cJSON *meta = cJSON_CreateObject();
    if (meta == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(meta, "type", desc->type);
    // readonly, min and max are always spelled out where they apply: the specification
    // contradicts itself on what they default to, so a control that leaves them out is read
    // differently by different panels.
    cJSON_AddBoolToObject(meta, "readonly", readonly);
    cJSON_AddNumberToObject(meta, "order", (double)order);
    if (desc->has_range) {
        cJSON_AddNumberToObject(meta, "min", 0);
        cJSON_AddNumberToObject(meta, "max", desc->max);
        if (desc->precision != 0) {
            cJSON_AddNumberToObject(meta, "precision", desc->precision);
        }
    }

    cJSON *title = cJSON_AddObjectToObject(meta, "title");
    if (title == NULL) {
        cJSON_Delete(meta);
        return NULL;
    }
    cJSON_AddStringToObject(title, "en", desc->title_en);
    cJSON_AddStringToObject(title, "ru", desc->title_ru);

    char *out = cJSON_PrintUnformatted(meta);
    cJSON_Delete(meta);
    return out;
}

static void wb_device_format_value(wb_control_id_t id, bool online, bool has_event, const fp_acs_event_t *event,
                                   char *buf, size_t len)
{
    switch (id) {
    case WB_CONTROL_SENSOR_ONLINE:
        snprintf(buf, len, "%s", online ? WB_SWITCH_ON : WB_SWITCH_OFF);
        break;
    case WB_CONTROL_LAST_RESULT:
        snprintf(buf, len, "%s", has_event ? fp_acs_result_str(event->result) : WB_PAYLOAD_EMPTY);
        break;
    case WB_CONTROL_LAST_USER:
        snprintf(buf, len, "%s", has_event ? event->user_name : WB_PAYLOAD_EMPTY);
        break;
    case WB_CONTROL_LAST_SOURCE:
        snprintf(buf, len, "%s", has_event ? fp_acs_source_str(event->source) : WB_PAYLOAD_EMPTY);
        break;
    case WB_CONTROL_EVENT_COUNT:
        snprintf(buf, len, "%" PRIu32, has_event ? event->seq : 0u);
        break;
    default:
        snprintf(buf, len, "%s", WB_PAYLOAD_EMPTY);
        break;
    }
}

static void wb_device_publish_control_meta(wb_control_id_t id)
{
    char *meta = wb_device_control_meta_json(&controls[id].desc, true, (int)id + 1);
    if (meta == NULL) {
        ESP_LOGE(TAG, "Failed to build the meta of control %s", controls[id].name);
        return;
    }
    wb_device_publish_control_topic(controls[id].name, WB_TOPIC_META, meta);
    free(meta);
}

static void wb_device_publish_control_value(wb_control_id_t id, const char *value)
{
    if (wb_device_publish_control_topic(controls[id].name, WB_TOPIC_VALUE, value) == ESP_OK) {
        snprintf(last_value[id], sizeof(last_value[id]), "%s", value);
        last_value_valid[id] = true;
    }
}

// The fixed controls: every meta and value when all is set, otherwise only the values that
// changed.
static void wb_device_publish_controls(bool all)
{
    fp_acs_event_t event;
    memset(&event, 0, sizeof(event));
    bool has_event = fp_acs_get_last_event(&event);
    bool online = fp_acs_sensor_online();

    for (int i = 0; i < WB_CONTROL_COUNT; i++) {
        wb_control_id_t id = (wb_control_id_t)i;

        if (all) {
            wb_device_publish_control_meta(id);
        }

        char value[WB_DEVICE_VALUE_MAX_LEN];
        wb_device_format_value(id, online, has_event, &event, value, sizeof(value));
        if (all || (!last_value_valid[id]) || (strncmp(last_value[id], value, sizeof(value)) != 0)) {
            wb_device_publish_control_value(id, value);
        }
    }
}

static void wb_device_user_control_name(uint8_t id, char *buf, size_t len)
{
    snprintf(buf, len, WB_USER_CONTROL_PREFIX "%u", (unsigned)id);
}

// Brings the broker in line with one user of the store: its meta when it is new or renamed, its
// value when it is new or toggled - or both when all is set. False when a publish failed.
static bool wb_device_sync_user(const fp_store_user_t *user, bool all)
{
    wb_user_state_t *state = &users[user->id - 1];
    char control[WB_USER_CONTROL_MAX_LEN];
    wb_device_user_control_name(user->id, control, sizeof(control));
    bool ok = true;

    // Set before the publishes rather than after: a clearing that is not needed costs three empty
    // messages, a topic nobody knows about stays on the broker for good.
    state->on_broker = true;

    if (all || (!state->meta_valid) || (strncmp(state->name, user->name, sizeof(state->name)) != 0)) {
        const wb_control_desc_t desc = {
            .type = "switch",
            .has_range = true,
            .max = WB_SWITCH_MAX,
            // A name is what the user typed, in whatever alphabet: there is nothing to translate,
            // so both languages carry the same string.
            .title_en = user->name,
            .title_ru = user->name,
        };
        char *meta = wb_device_control_meta_json(&desc, false, WB_CONTROL_COUNT + user->id);
        if ((meta != NULL) && (wb_device_publish_control_topic(control, WB_TOPIC_META, meta) == ESP_OK)) {
            state->meta_valid = true;
            snprintf(state->name, sizeof(state->name), "%s", user->name);
        } else {
            ok = false;
        }
        free(meta);
    }

    if (all || (!state->value_valid) || (state->enabled != user->enabled)) {
        const char *value = user->enabled ? WB_SWITCH_ON : WB_SWITCH_OFF;
        if (wb_device_publish_control_topic(control, WB_TOPIC_VALUE, value) == ESP_OK) {
            state->value_valid = true;
            state->enabled = user->enabled;
        } else {
            ok = false;
        }
    }
    return ok;
}

// Deletes the retained topics of a user control whose user is gone. False when a publish failed.
static bool wb_device_clear_user(uint8_t id)
{
    char control[WB_USER_CONTROL_MAX_LEN];
    wb_device_user_control_name(id, control, sizeof(control));

    bool ok = (wb_device_publish_control_topic(control, WB_TOPIC_VALUE, WB_PAYLOAD_EMPTY) == ESP_OK);
    ok = (wb_device_publish_control_topic(control, WB_TOPIC_META_ERROR, WB_PAYLOAD_EMPTY) == ESP_OK) && ok;
    ok = (wb_device_publish_control_topic(control, WB_TOPIC_META, WB_PAYLOAD_EMPTY) == ESP_OK) && ok;
    if (ok) {
        ESP_LOGI(TAG, "User %u is gone, its control is cleared", (unsigned)id);
        memset(&users[id - 1], 0, sizeof(users[id - 1]));
    }
    return ok;
}

// The user controls against the store: skipped while users_rev has not moved, unless all is set.
static void wb_device_sync_users(bool all)
{
    if ((!all) && users_rev_valid && (fp_store_users_rev() == users_rev)) {
        return;
    }

    // Too big for the stack of the publishing task, and needed only when something changed.
    fp_store_user_t *snapshot = malloc(FP_STORE_USERS_MAX * sizeof(*snapshot));
    if (snapshot == NULL) {
        ESP_LOGE(TAG, "No memory for the user list, the user controls wait for the next pass");
        return;
    }
    uint32_t rev = 0;
    size_t count = fp_store_users_snapshot(snapshot, FP_STORE_USERS_MAX, &rev);

    bool present[FP_STORE_USERS_MAX] = {false};
    bool ok = true;
    for (size_t i = 0; i < count; i++) {
        const fp_store_user_t *user = &snapshot[i];
        if ((user->id == FP_STORE_NO_USER) || (user->id > FP_STORE_USERS_MAX)) {
            continue;
        }
        present[user->id - 1] = true;
        ok = wb_device_sync_user(user, all) && ok;
    }
    free(snapshot);

    for (int i = 0; i < FP_STORE_USERS_MAX; i++) {
        if ((!present[i]) && users[i].on_broker) {
            ok = wb_device_clear_user((uint8_t)(i + 1)) && ok;
        }
    }

    // Recorded only once the broker has been told everything, so a failed publish is retried on
    // the next pass instead of waiting for the next change to a user.
    if (ok) {
        users_rev = rev;
        users_rev_valid = true;
    }
}

void wb_device_init(const char *device_id_str, const wb_device_transport_t *iface)
{
    if ((device_id_str == NULL) || (iface == NULL)) {
        return;
    }

    strncpy(device_id, device_id_str, sizeof(device_id) - 1);
    device_id[sizeof(device_id) - 1] = '\0';
    transport = *iface;

    // A new binding may be a new device id or a new broker, where none of the old user controls is.
    memset(users, 0, sizeof(users));
    wb_device_reset();
}

void wb_device_reset(void)
{
    memset(last_value, 0, sizeof(last_value));
    memset(last_value_valid, 0, sizeof(last_value_valid));
    for (int i = 0; i < FP_STORE_USERS_MAX; i++) {
        users[i].meta_valid = false;
        users[i].value_valid = false;
    }
    users_rev_valid = false;
}

void wb_device_publish_all(void)
{
    char topic[WB_DEVICE_TOPIC_MAX_LEN];

    // The device first: without its meta the controls below belong to nothing.
    char *meta = wb_device_meta_json();
    if (meta != NULL) {
        if (wb_device_build_topic(topic, sizeof(topic), device_id, NULL, WB_TOPIC_META) == ESP_OK) {
            wb_device_publish(topic, meta, true);
        }
        free(meta);
    }

    // The will published "rw" when the last session died, and that topic is retained, so the
    // only thing that can clear it is this publish.
    if (wb_device_topic_error(device_id, topic, sizeof(topic)) == ESP_OK) {
        wb_device_publish(topic, WB_PAYLOAD_EMPTY, true);
    }

    wb_device_publish_controls(true);

    // Every user of the store, and the clearing of every user deleted while there was no session.
    wb_device_sync_users(true);

    // Last: one wildcard for the /on topics of all controls, so a user added later needs no
    // subscription of its own. The read-only controls it also covers are refused by
    // wb_device_handle_command().
    if (transport.subscribe != NULL) {
        if (wb_device_build_topic(topic, sizeof(topic), device_id, WB_TOPIC_ANY_CONTROL, WB_TOPIC_COMMAND) == ESP_OK) {
            transport.subscribe(transport.ctx, topic);
        }
    }
}

void wb_device_publish_changed(void)
{
    wb_device_publish_controls(false);
    wb_device_sync_users(false);
}

// The user a command topic names (/devices/<id>/controls/user_<n>/on), or FP_STORE_NO_USER when
// the topic is not the command topic of a user control of this device.
static uint8_t wb_device_user_from_topic(const char *topic, int topic_len)
{
    char prefix[WB_DEVICE_TOPIC_MAX_LEN];
    if (wb_device_build_topic(prefix, sizeof(prefix), device_id, WB_USER_CONTROL_PREFIX, "") != ESP_OK) {
        return FP_STORE_NO_USER;
    }
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(WB_TOPIC_COMMAND);
    size_t len = (size_t)topic_len;

    bool shaped = ((len > (prefix_len + suffix_len)) && (strncmp(topic, prefix, prefix_len) == 0) &&
                   (strncmp(topic + (len - suffix_len), WB_TOPIC_COMMAND, suffix_len) == 0));
    if (!shaped) {
        return FP_STORE_NO_USER;
    }

    size_t digits = (len - prefix_len) - suffix_len;
    const char *number = topic + prefix_len;
    if ((digits > WB_USER_ID_MAX_DIGITS) || (number[0] == '0')) {
        return FP_STORE_NO_USER;
    }

    unsigned id = 0;
    for (size_t i = 0; i < digits; i++) {
        if ((number[i] < '0') || (number[i] > '9')) {
            return FP_STORE_NO_USER;
        }
        id = (id * WB_DECIMAL_BASE) + (unsigned)(number[i] - '0');
    }
    if (id > FP_STORE_USERS_MAX) {
        return FP_STORE_NO_USER;
    }
    return (uint8_t)id;
}

esp_err_t wb_device_handle_command(const char *topic, int topic_len, const char *payload, int payload_len)
{
    if ((topic == NULL) || (payload == NULL) || (topic_len <= 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t id = wb_device_user_from_topic(topic, topic_len);
    if (id == FP_STORE_NO_USER) {
        // The wildcard subscription also brings the /on topics of the read-only controls.
        ESP_LOGW(TAG, "Command on a topic that is no writable control of this device, ignored");
        return ESP_ERR_NOT_FOUND;
    }

    bool enabled;
    if ((payload_len == WB_SWITCH_LEN) && (strncmp(payload, WB_SWITCH_ON, WB_SWITCH_LEN) == 0)) {
        enabled = true;
    } else if ((payload_len == WB_SWITCH_LEN) && (strncmp(payload, WB_SWITCH_OFF, WB_SWITCH_LEN) == 0)) {
        enabled = false;
    } else {
        ESP_LOGW(TAG, "A switch takes 0 or 1, user %u left as it is", (unsigned)id);
        return ESP_ERR_INVALID_ARG;
    }

    // ESP_ERR_NOT_FOUND for a user that does not exist - the retained topics of one that was just
    // deleted can outlive it until the next pass - so nothing is echoed and nothing comes back.
    esp_err_t ret = fp_store_set_enabled(id, enabled);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "User %u refused the command: %s", (unsigned)id, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "User %u %s over MQTT", (unsigned)id, enabled ? "enabled" : "disabled");

    // Echo the accepted value straight back into the value topic instead of waiting for the next
    // pass: the panel has to see the switch it just flipped stay flipped.
    char control[WB_USER_CONTROL_MAX_LEN];
    wb_device_user_control_name(id, control, sizeof(control));
    wb_user_state_t *state = &users[id - 1];
    state->on_broker = true;
    if (wb_device_publish_control_topic(control, WB_TOPIC_VALUE, enabled ? WB_SWITCH_ON : WB_SWITCH_OFF) == ESP_OK) {
        state->value_valid = true;
        state->enabled = enabled;
    }
    return ESP_OK;
}

#endif // FP_ACS_ENABLED
