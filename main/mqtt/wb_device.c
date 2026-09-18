#include "wb_device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mixer.h"
#include "setting_items.h"

static const char *TAG = "wb_device";

#define WB_TOPIC_PREFIX             "/devices/"
#define WB_TOPIC_CONTROLS           "/controls/"
#define WB_TOPIC_META               "/meta"
#define WB_TOPIC_META_ERROR         "/meta/error"
#define WB_TOPIC_COMMAND            "/on"

#define WB_DRIVER_NAME              "wb-mge-mixer"

// The error string a control carries while the value behind it cannot be read.
#define WB_ERROR_UNREADABLE         "r"
#define WB_ERROR_NONE               ""

#define MAC_BYTES                   6

// The panel sends a switch as "0" or "1" and nothing else.
#define WB_SWITCH_OFF               "0"
#define WB_SWITCH_ON                "1"

// A program holds at most 11 steps (main/mixer/program.h), so the step index runs 0..10. Spelled
// out here rather than taken from program.h: this file is the convention, and the only thing it
// needs from the model is the size of the number it has to advertise.
#define WB_PROGRAM_MAX_STEPS        11

// Bounds published in the meta. They are the ranges of the values themselves, not of the
// hardware: mixer_status_t carries the frequency in 0.1 Hz as a uint16, the current in 0.01 A,
// and the drive's own limits (1.0 .. 99.0 Hz) are the engine's business, not the panel's.
#define WB_STEP_MAX                 (WB_PROGRAM_MAX_STEPS - 1)
#define WB_STEP_LEFT_MAX            65535
#define WB_SPEED_MAX                20000
#define WB_FREQUENCY_MAX            100
#define WB_VOLTAGE_MAX              1000
#define WB_CURRENT_MAX              650
#define WB_TEMPERATURE_MIN          (-40)
#define WB_TEMPERATURE_MAX          200

typedef struct {
    const char *name;
    const char *type;               // convention control type
    const char *units;              // NULL when the value is a bare number
    bool readonly;
    double min;
    double max;
    double precision;               // 0 = do not publish a precision
    const char *title_en;
    const char *title_ru;
    const char *const *enum_en;     // NULL when the control is not an enumeration
    const char *const *enum_ru;
    int enum_count;
} wb_control_desc_t;

static const char *const state_en[] = {"Stopped", "Running", "Error"};
static const char *const state_ru[] = {"Стоит", "Работает", "Ошибка"};
static const char *const step_type_en[] = {"Pause", "Forward", "Reverse"};
static const char *const step_type_ru[] = {"Пауза", "Вперёд", "Реверс"};

// The table is indexed by wb_control_id_t, so its order is the order of the enum and the order
// the controls appear in the panel.
static const wb_control_desc_t controls[WB_CONTROL_COUNT] = {
    [WB_CONTROL_ENABLED] = {
        .name = "enabled", .type = "switch", .units = NULL, .readonly = false,
        .min = 0, .max = 1, .precision = 0,
        .title_en = "Enabled", .title_ru = "Включена",
    },
    [WB_CONTROL_PROGRAM] = {
        // The enum labels of this one are the mix_pN_name settings, so they are filled in by
        // the JSON builder rather than listed here.
        .name = "program", .type = "value", .units = NULL, .readonly = false,
        .min = 0, .max = MIXER_PROGRAM_COUNT - 1, .precision = 1,
        .title_en = "Program", .title_ru = "Программа",
    },
    [WB_CONTROL_STATE] = {
        .name = "state", .type = "value", .units = NULL, .readonly = true,
        .min = 0, .max = 2, .precision = 1,
        .title_en = "State", .title_ru = "Состояние",
        .enum_en = state_en, .enum_ru = state_ru, .enum_count = 3,
    },
    [WB_CONTROL_STEP] = {
        .name = "step", .type = "value", .units = NULL, .readonly = true,
        .min = 0, .max = WB_STEP_MAX, .precision = 1,
        .title_en = "Step", .title_ru = "Шаг",
    },
    [WB_CONTROL_STEP_LEFT] = {
        .name = "step_left", .type = "value", .units = "s", .readonly = true,
        .min = 0, .max = WB_STEP_LEFT_MAX, .precision = 1,
        .title_en = "Step time left", .title_ru = "Осталось в шаге",
    },
    [WB_CONTROL_SPEED] = {
        .name = "speed", .type = "value", .units = "rpm", .readonly = true,
        .min = 0, .max = WB_SPEED_MAX, .precision = 1,
        .title_en = "Speed", .title_ru = "Скорость",
    },
    [WB_CONTROL_STEP_TYPE] = {
        .name = "step_type", .type = "value", .units = NULL, .readonly = true,
        .min = 0, .max = 2, .precision = 1,
        .title_en = "Step type", .title_ru = "Тип шага",
        .enum_en = step_type_en, .enum_ru = step_type_ru, .enum_count = 3,
    },
    [WB_CONTROL_FREQUENCY] = {
        .name = "frequency", .type = "value", .units = "Hz", .readonly = true,
        .min = 0, .max = WB_FREQUENCY_MAX, .precision = 0.1,
        .title_en = "Frequency", .title_ru = "Частота",
    },
    [WB_CONTROL_VOLTAGE] = {
        .name = "voltage", .type = "value", .units = "V", .readonly = true,
        .min = 0, .max = WB_VOLTAGE_MAX, .precision = 0.1,
        .title_en = "DC bus voltage", .title_ru = "Напряжение шины",
    },
    [WB_CONTROL_CURRENT] = {
        .name = "current", .type = "value", .units = "A", .readonly = true,
        .min = 0, .max = WB_CURRENT_MAX, .precision = 0.01,
        .title_en = "Current", .title_ru = "Ток",
    },
    [WB_CONTROL_TEMPERATURE] = {
        .name = "temperature", .type = "value", .units = "deg C", .readonly = true,
        .min = WB_TEMPERATURE_MIN, .max = WB_TEMPERATURE_MAX, .precision = 1,
        .title_en = "Temperature", .title_ru = "Температура",
    },
    [WB_CONTROL_VFD_ONLINE] = {
        .name = "vfd_online", .type = "switch", .units = NULL, .readonly = true,
        .min = 0, .max = 1, .precision = 0,
        .title_en = "Drive online", .title_ru = "Связь с частотником",
    },
};

// The four controls whose value comes from the drive over Modbus. When the drive is silent they
// are the ones that get "r" and keep the value they last had.
static bool wb_control_needs_vfd(wb_control_id_t id)
{
    return ((id == WB_CONTROL_FREQUENCY) || (id == WB_CONTROL_VOLTAGE) ||
            (id == WB_CONTROL_CURRENT) || (id == WB_CONTROL_TEMPERATURE));
}

static const char *const program_name_keys[MIXER_PROGRAM_COUNT] = {
    KEY_MIX_P0_NAME, KEY_MIX_P1_NAME, KEY_MIX_P2_NAME, KEY_MIX_P3_NAME,
};

static char device_id[WB_DEVICE_ID_MAX_LEN];
static wb_device_transport_t transport;

// What the broker has been told. A value is republished only when it differs from what is here,
// which is the whole point of keeping it: the panel is fed once a second and almost nothing
// changes between two of those.
static char last_value[WB_CONTROL_COUNT][WB_DEVICE_VALUE_MAX_LEN];
static bool last_value_valid[WB_CONTROL_COUNT];
static char last_error[WB_CONTROL_COUNT][WB_DEVICE_ERROR_MAX_LEN];
static bool last_error_valid[WB_CONTROL_COUNT];

// The program names as they were when the program control's meta was last published. A user who
// renames a program has to see the new name on the panel's selector, and the name lives in the
// meta rather than in the value.
static char last_program_names[MIXER_PROGRAM_COUNT][SETTING_ITEM_MAX_STR_LEN];


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

// Every topic builder below is this one call with a different tail. Returns
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

esp_err_t wb_device_topic_meta(const char *device_id_str, char *buf, size_t len)
{
    return wb_device_build_topic(buf, len, device_id_str, NULL, WB_TOPIC_META);
}

esp_err_t wb_device_topic_error(const char *device_id_str, char *buf, size_t len)
{
    return wb_device_build_topic(buf, len, device_id_str, NULL, WB_TOPIC_META_ERROR);
}

esp_err_t wb_device_topic_control(const char *device_id_str, const char *control, char *buf, size_t len)
{
    return wb_device_build_topic(buf, len, device_id_str, control, "");
}

esp_err_t wb_device_topic_control_meta(const char *device_id_str, const char *control, char *buf, size_t len)
{
    return wb_device_build_topic(buf, len, device_id_str, control, WB_TOPIC_META);
}

esp_err_t wb_device_topic_control_error(const char *device_id_str, const char *control, char *buf, size_t len)
{
    return wb_device_build_topic(buf, len, device_id_str, control, WB_TOPIC_META_ERROR);
}

esp_err_t wb_device_topic_control_command(const char *device_id_str, const char *control, char *buf, size_t len)
{
    return wb_device_build_topic(buf, len, device_id_str, control, WB_TOPIC_COMMAND);
}

const char *wb_device_control_name(wb_control_id_t id)
{
    if ((id < 0) || (id >= WB_CONTROL_COUNT)) {
        return NULL;
    }
    return controls[id].name;
}

char *wb_device_meta_json(void)
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
    cJSON_AddStringToObject(title, "en", "Mixer");
    cJSON_AddStringToObject(title, "ru", "Мешалка");
    cJSON_AddItemToObject(root, "title", title);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

// The enum of the program control: index -> the name stored in mix_pN_name. By the convention an
// enumeration is a field of a value control and not a type of its own, so what travels on the
// bus is the number and what the panel shows is this table.
static bool wb_device_add_program_enum(cJSON *meta)
{
    cJSON *enum_obj = cJSON_AddObjectToObject(meta, "enum");
    if (enum_obj == NULL) {
        return false;
    }

    for (int i = 0; i < MIXER_PROGRAM_COUNT; i++) {
        char name[SETTING_ITEM_MAX_STR_LEN] = {0};
        if (setting_items_read(program_name_keys[i], name) != ESP_OK) {
            name[0] = '\0';
        }

        char index[12];         // the widest an int can print, so the compiler sees no truncation
        snprintf(index, sizeof(index), "%d", i);

        cJSON *titles = cJSON_AddObjectToObject(enum_obj, index);
        if (titles == NULL) {
            return false;
        }
        // One name per program, in whatever alphabet the user typed it: there is nothing to
        // translate, so both languages carry the same string.
        cJSON_AddStringToObject(titles, "en", name);
        cJSON_AddStringToObject(titles, "ru", name);
    }
    return true;
}

static bool wb_device_add_enum(cJSON *meta, const wb_control_desc_t *desc)
{
    cJSON *enum_obj = cJSON_AddObjectToObject(meta, "enum");
    if (enum_obj == NULL) {
        return false;
    }

    for (int i = 0; i < desc->enum_count; i++) {
        char index[12];         // the widest an int can print, so the compiler sees no truncation
        snprintf(index, sizeof(index), "%d", i);

        cJSON *titles = cJSON_AddObjectToObject(enum_obj, index);
        if (titles == NULL) {
            return false;
        }
        cJSON_AddStringToObject(titles, "en", desc->enum_en[i]);
        cJSON_AddStringToObject(titles, "ru", desc->enum_ru[i]);
    }
    return true;
}

char *wb_device_control_meta_json(wb_control_id_t id)
{
    if ((id < 0) || (id >= WB_CONTROL_COUNT)) {
        return NULL;
    }

    const wb_control_desc_t *desc = &controls[id];

    cJSON *meta = cJSON_CreateObject();
    if (meta == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(meta, "type", desc->type);
    if (desc->units != NULL) {
        cJSON_AddStringToObject(meta, "units", desc->units);
    }
    // readonly, min and max are always spelled out: the specification contradicts itself on what
    // they default to, so a control that leaves them out is read differently by different panels.
    cJSON_AddBoolToObject(meta, "readonly", desc->readonly);
    cJSON_AddNumberToObject(meta, "order", (double)(id + 1));
    cJSON_AddNumberToObject(meta, "min", desc->min);
    cJSON_AddNumberToObject(meta, "max", desc->max);
    if (desc->precision != 0) {
        cJSON_AddNumberToObject(meta, "precision", desc->precision);
    }

    cJSON *title = cJSON_AddObjectToObject(meta, "title");
    if (title == NULL) {
        cJSON_Delete(meta);
        return NULL;
    }
    cJSON_AddStringToObject(title, "en", desc->title_en);
    cJSON_AddStringToObject(title, "ru", desc->title_ru);

    bool ok = true;
    if (id == WB_CONTROL_PROGRAM) {
        ok = wb_device_add_program_enum(meta);
    } else if (desc->enum_count > 0) {
        ok = wb_device_add_enum(meta, desc);
    }
    if (!ok) {
        cJSON_Delete(meta);
        return NULL;
    }

    char *out = cJSON_PrintUnformatted(meta);
    cJSON_Delete(meta);
    return out;
}

// Fixed-point formatting rather than "%.1f": the values arrive from the drive as integers in
// tenths and hundredths, so printing them as integers is both exact and cheap.
//
// The switch and the program come in as arguments rather than out of the snapshot, and from the
// same place as each other (see wb_device_current_program): the snapshot only moves on the
// engine's own tick, so a program taken from it would flicker back to the old number for the
// pass that follows a command.
static void wb_device_format_value(wb_control_id_t id, const mixer_status_t *status, bool enabled,
                                   uint8_t program, char *buf, size_t len)
{
    switch (id) {
    case WB_CONTROL_ENABLED:
        snprintf(buf, len, "%s", enabled ? WB_SWITCH_ON : WB_SWITCH_OFF);
        break;
    case WB_CONTROL_PROGRAM:
        snprintf(buf, len, "%u", (unsigned)program);
        break;
    case WB_CONTROL_STATE:
        snprintf(buf, len, "%d", (int)status->state);
        break;
    case WB_CONTROL_STEP:
        snprintf(buf, len, "%u", (unsigned)status->step);
        break;
    case WB_CONTROL_STEP_LEFT:
        snprintf(buf, len, "%u", (unsigned)status->step_left_s);
        break;
    case WB_CONTROL_SPEED:
        snprintf(buf, len, "%u", (unsigned)status->speed_rpm);
        break;
    case WB_CONTROL_STEP_TYPE:
        snprintf(buf, len, "%u", (unsigned)status->step_type);
        break;
    case WB_CONTROL_FREQUENCY:
        snprintf(buf, len, "%u.%u", (unsigned)(status->freq_dhz / 10u),
                 (unsigned)(status->freq_dhz % 10u));
        break;
    case WB_CONTROL_VOLTAGE:
        snprintf(buf, len, "%u.%u", (unsigned)(status->bus_voltage_dv / 10u),
                 (unsigned)(status->bus_voltage_dv % 10u));
        break;
    case WB_CONTROL_CURRENT:
        snprintf(buf, len, "%u.%02u", (unsigned)(status->current_ca / 100u),
                 (unsigned)(status->current_ca % 100u));
        break;
    case WB_CONTROL_TEMPERATURE:
        snprintf(buf, len, "%d", (int)status->temp_c);
        break;
    case WB_CONTROL_VFD_ONLINE:
        snprintf(buf, len, "%s", status->vfd_online ? WB_SWITCH_ON : WB_SWITCH_OFF);
        break;
    default:
        snprintf(buf, len, "0");
        break;
    }
}

static esp_err_t wb_device_publish(const char *topic, const char *payload, bool retain)
{
    if ((transport.publish == NULL) || (topic == NULL) || (payload == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    return transport.publish(transport.ctx, topic, payload, retain);
}

static void wb_device_publish_control_meta(wb_control_id_t id)
{
    char topic[WB_DEVICE_TOPIC_MAX_LEN];
    if (wb_device_topic_control_meta(device_id, controls[id].name, topic, sizeof(topic)) != ESP_OK) {
        ESP_LOGE(TAG, "Topic for the meta of control %s does not fit", controls[id].name);
        return;
    }

    char *meta = wb_device_control_meta_json(id);
    if (meta == NULL) {
        ESP_LOGE(TAG, "Failed to build the meta of control %s", controls[id].name);
        return;
    }
    wb_device_publish(topic, meta, true);
    free(meta);
}

static void wb_device_publish_control_value(wb_control_id_t id, const char *value)
{
    char topic[WB_DEVICE_TOPIC_MAX_LEN];
    if (wb_device_topic_control(device_id, controls[id].name, topic, sizeof(topic)) != ESP_OK) {
        ESP_LOGE(TAG, "Topic for the value of control %s does not fit", controls[id].name);
        return;
    }

    if (wb_device_publish(topic, value, true) == ESP_OK) {
        strncpy(last_value[id], value, WB_DEVICE_VALUE_MAX_LEN - 1);
        last_value[id][WB_DEVICE_VALUE_MAX_LEN - 1] = '\0';
        last_value_valid[id] = true;
    }
}

static void wb_device_publish_control_error(wb_control_id_t id, const char *error)
{
    char topic[WB_DEVICE_TOPIC_MAX_LEN];
    if (wb_device_topic_control_error(device_id, controls[id].name, topic, sizeof(topic)) != ESP_OK) {
        ESP_LOGE(TAG, "Topic for the error of control %s does not fit", controls[id].name);
        return;
    }

    if (wb_device_publish(topic, error, true) == ESP_OK) {
        strncpy(last_error[id], error, WB_DEVICE_ERROR_MAX_LEN - 1);
        last_error[id][WB_DEVICE_ERROR_MAX_LEN - 1] = '\0';
        last_error_valid[id] = true;
    }
}

// Snapshot the program names the meta was built from, so a later rename can be spotted.
static void wb_device_cache_program_names(void)
{
    for (int i = 0; i < MIXER_PROGRAM_COUNT; i++) {
        if (setting_items_read(program_name_keys[i], last_program_names[i]) != ESP_OK) {
            last_program_names[i][0] = '\0';
        }
    }
}

static bool wb_device_program_names_changed(void)
{
    for (int i = 0; i < MIXER_PROGRAM_COUNT; i++) {
        char name[SETTING_ITEM_MAX_STR_LEN] = {0};
        if (setting_items_read(program_name_keys[i], name) != ESP_OK) {
            name[0] = '\0';
        }
        if (strncmp(name, last_program_names[i], SETTING_ITEM_MAX_STR_LEN) != 0) {
            return true;
        }
    }
    return false;
}

void wb_device_init(const char *device_id_str, const wb_device_transport_t *iface)
{
    if ((device_id_str == NULL) || (iface == NULL)) {
        return;
    }

    strncpy(device_id, device_id_str, sizeof(device_id) - 1);
    device_id[sizeof(device_id) - 1] = '\0';
    transport = *iface;

    wb_device_reset();
}

void wb_device_reset(void)
{
    memset(last_value, 0, sizeof(last_value));
    memset(last_value_valid, 0, sizeof(last_value_valid));
    memset(last_error, 0, sizeof(last_error));
    memset(last_error_valid, 0, sizeof(last_error_valid));
    memset(last_program_names, 0, sizeof(last_program_names));
}

void wb_device_publish_all(void)
{
    char topic[WB_DEVICE_TOPIC_MAX_LEN];

    // The device first: without its meta the controls below belong to nothing.
    char *meta = wb_device_meta_json();
    if (meta != NULL) {
        if (wb_device_topic_meta(device_id, topic, sizeof(topic)) == ESP_OK) {
            wb_device_publish(topic, meta, true);
        }
        free(meta);
    }

    // The will published "rw" when the last session died, and that topic is retained, so the
    // only thing that can clear it is this publish.
    if (wb_device_topic_error(device_id, topic, sizeof(topic)) == ESP_OK) {
        wb_device_publish(topic, WB_ERROR_NONE, true);
    }

    mixer_status_t status;
    mixer_get_status(&status);
    bool enabled = mixer_is_enabled();
    uint8_t program = mixer_get_program();

    wb_device_cache_program_names();

    for (int i = 0; i < WB_CONTROL_COUNT; i++) {
        wb_control_id_t id = (wb_control_id_t)i;

        wb_device_publish_control_meta(id);

        const char *error = (wb_control_needs_vfd(id) && !status.vfd_online) ? WB_ERROR_UNREADABLE
                                                                            : WB_ERROR_NONE;
        wb_device_publish_control_error(id, error);

        // Published even for a control that is carrying "r": a control with no value at all is
        // not shown by the panel, and the last reading the engine holds is what the convention
        // asks such a control to keep.
        char value[WB_DEVICE_VALUE_MAX_LEN];
        wb_device_format_value(id, &status, enabled, program, value, sizeof(value));
        wb_device_publish_control_value(id, value);
    }

    // Last, and only for what the panel may write: a subscription to a control that cannot be
    // set would accept commands this device has nothing to do with.
    if (transport.subscribe != NULL) {
        for (int i = 0; i < WB_CONTROL_COUNT; i++) {
            if (controls[i].readonly) {
                continue;
            }
            if (wb_device_topic_control_command(device_id, controls[i].name, topic, sizeof(topic)) == ESP_OK) {
                transport.subscribe(transport.ctx, topic);
            }
        }
    }
}

void wb_device_publish_changed(void)
{
    mixer_status_t status;
    mixer_get_status(&status);
    bool enabled = mixer_is_enabled();
    uint8_t program = mixer_get_program();

    // A renamed program changes no value, only the labels the selector is built from.
    if (wb_device_program_names_changed()) {
        ESP_LOGI(TAG, "Program names changed, republishing the meta of the program control");
        wb_device_publish_control_meta(WB_CONTROL_PROGRAM);
        wb_device_cache_program_names();
    }

    for (int i = 0; i < WB_CONTROL_COUNT; i++) {
        wb_control_id_t id = (wb_control_id_t)i;

        bool unreadable = (wb_control_needs_vfd(id) && !status.vfd_online);
        const char *error = unreadable ? WB_ERROR_UNREADABLE : WB_ERROR_NONE;

        // The error goes out before the value, so a drive that has just come back is never seen
        // holding a fresh value under a stale "r".
        if (!last_error_valid[id] || (strncmp(last_error[id], error, WB_DEVICE_ERROR_MAX_LEN) != 0)) {
            wb_device_publish_control_error(id, error);
        }

        // Nothing is written over a value that cannot be read: by the convention what stays in
        // the topic is the last good reading, and the error string is what says it is stale.
        if (unreadable) {
            continue;
        }

        char value[WB_DEVICE_VALUE_MAX_LEN];
        wb_device_format_value(id, &status, enabled, program, value, sizeof(value));

        if (!last_value_valid[id] || (strncmp(last_value[id], value, WB_DEVICE_VALUE_MAX_LEN) != 0)) {
            wb_device_publish_control_value(id, value);
        }
    }
}

// The control a command topic names, or WB_CONTROL_COUNT when the topic is not a command topic
// of this device at all.
static wb_control_id_t wb_device_control_from_topic(const char *topic, int topic_len)
{
    char expected[WB_DEVICE_TOPIC_MAX_LEN];

    for (int i = 0; i < WB_CONTROL_COUNT; i++) {
        if (wb_device_topic_control_command(device_id, controls[i].name, expected, sizeof(expected)) != ESP_OK) {
            continue;
        }
        size_t expected_len = strlen(expected);
        if (((size_t)topic_len == expected_len) && (strncmp(topic, expected, expected_len) == 0)) {
            return (wb_control_id_t)i;
        }
    }
    return WB_CONTROL_COUNT;
}

// A whole non-negative number and nothing else: no sign, no spaces, no trailing characters. The
// panel sends exactly this, and anything else is a message from something that is not the panel.
static bool wb_device_parse_uint(const char *payload, int payload_len, unsigned *out)
{
    if ((payload_len <= 0) || (payload_len > 5)) {
        return false;
    }

    unsigned value = 0;
    for (int i = 0; i < payload_len; i++) {
        char c = payload[i];
        if ((c < '0') || (c > '9')) {
            return false;
        }
        value = (value * 10u) + (unsigned)(c - '0');
    }

    *out = value;
    return true;
}

esp_err_t wb_device_handle_command(const char *topic, int topic_len, const char *payload, int payload_len)
{
    if ((topic == NULL) || (payload == NULL) || (topic_len <= 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    wb_control_id_t id = wb_device_control_from_topic(topic, topic_len);
    if (id == WB_CONTROL_COUNT) {
        ESP_LOGW(TAG, "Command on a topic that belongs to no control of this device");
        return ESP_ERR_NOT_FOUND;
    }
    if (controls[id].readonly) {
        // Unreachable through a subscription this layer made - only read-write controls are
        // subscribed - so this is a command that arrived some other way.
        ESP_LOGW(TAG, "Command for the read-only control %s, ignored", controls[id].name);
        return ESP_ERR_NOT_FOUND;
    }

    unsigned parsed = 0;
    if (!wb_device_parse_uint(payload, payload_len, &parsed)) {
        ESP_LOGW(TAG, "Unparsable command payload for control %s", controls[id].name);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;
    char value[WB_DEVICE_VALUE_MAX_LEN];

    if (id == WB_CONTROL_ENABLED) {
        if (parsed > 1) {
            ESP_LOGW(TAG, "A switch takes 0 or 1, got %u", parsed);
            return ESP_ERR_INVALID_ARG;
        }
        ret = mixer_set_enabled(parsed != 0);
        snprintf(value, sizeof(value), "%u", parsed);
    } else if (id == WB_CONTROL_PROGRAM) {
        if (parsed >= MIXER_PROGRAM_COUNT) {
            ESP_LOGW(TAG, "There is no program %u", parsed);
            return ESP_ERR_INVALID_ARG;
        }
        ret = mixer_set_program((uint8_t)parsed);
        snprintf(value, sizeof(value), "%u", parsed);
    } else {
        // A writable control with no command of its own: adding one to the table without adding
        // it here is the only way to get in, and silently accepting it would be worse.
        ESP_LOGE(TAG, "No command handler for the writable control %s", controls[id].name);
        return ESP_ERR_NOT_FOUND;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Control %s refused the command: %s", controls[id].name, esp_err_to_name(ret));
        return ret;
    }

    // Echo the accepted value straight back into the value topic instead of waiting for the next
    // periodic pass: the panel has to see the switch it just flipped stay flipped.
    wb_device_publish_control_value(id, value);
    return ESP_OK;
}
