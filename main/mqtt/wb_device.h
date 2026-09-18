#pragma once

/*
 * The Wiren Board MQTT convention, as this device speaks it.
 *
 * Everything that knows what a topic looks like and what a control's meta JSON contains lives
 * here; the client below it (mqtt_client_wb.c) only opens the connection and hands this layer
 * a way to publish and to subscribe. That split is what lets the whole convention be tested on
 * the host with no broker and no esp-mqtt at all - the transport is two function pointers.
 *
 * The topics, straight from wirenboard/conventions:
 *     /devices/<id>/meta                      device meta, retained
 *     /devices/<id>/meta/error                device error string, retained
 *     /devices/<id>/controls/<c>/meta         control meta, retained
 *     /devices/<id>/controls/<c>              control value, retained
 *     /devices/<id>/controls/<c>/meta/error   control error string, retained
 *     /devices/<id>/controls/<c>/on           command from the panel, NEVER retained
 *
 * An error string is a set of letters: "r" the value cannot be read, "w" it cannot be written,
 * empty everything is fine. The topic is retained, so clearing an error is an explicit publish
 * of the empty string rather than a publish that is left out.
 */

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

// The identifier this device appears under in /devices/<id>/... . Its own setting (mqtt_id) is
// a setting value like any other, so it is capped the same way.
#define WB_DEVICE_ID_MAX_LEN        64

// Long enough for the longest topic this layer builds: the prefix, a full-length device id, the
// longest control name and "/meta/error".
#define WB_DEVICE_TOPIC_MAX_LEN     160

// A formatted control value: "-32768", "99.0", "655.35", "1".
#define WB_DEVICE_VALUE_MAX_LEN     16

// An error string is a set of single letters, and this device only ever reports "r".
#define WB_DEVICE_ERROR_MAX_LEN     8

// The whole device is unreachable: neither readable nor writable. Published as the Last Will,
// so a device that drops off the network says so itself.
#define WB_DEVICE_ERROR_OFFLINE     "rw"

/*
 * The controls, in the order they are published. The enum value IS the "order" field of the
 * meta (plus one), so reordering this enum reorders the panel.
 */
typedef enum {
    WB_CONTROL_ENABLED = 0,
    WB_CONTROL_PROGRAM,
    WB_CONTROL_STATE,
    WB_CONTROL_STEP,
    WB_CONTROL_STEP_LEFT,
    WB_CONTROL_SPEED,
    WB_CONTROL_STEP_TYPE,
    WB_CONTROL_FREQUENCY,
    WB_CONTROL_VOLTAGE,
    WB_CONTROL_CURRENT,
    WB_CONTROL_TEMPERATURE,
    WB_CONTROL_VFD_ONLINE,
    WB_CONTROL_COUNT
} wb_control_id_t;

/*
 * What this layer needs from the connection underneath it. Both calls return ESP_OK when the
 * message was handed to the client; nothing here waits for a broker acknowledgement.
 */
typedef struct {
    esp_err_t (*publish)(void *ctx, const char *topic, const char *payload, bool retain);
    esp_err_t (*subscribe)(void *ctx, const char *topic);
    void *ctx;
} wb_device_transport_t;

// The device id to use when mqtt_id is empty: the model name and the last three bytes of the
// MAC, which is the same shape as the default hostname.
void wb_device_build_default_id(char *buf, size_t len);

// Topic builders. Each returns ESP_ERR_INVALID_SIZE rather than a truncated topic.
esp_err_t wb_device_topic_meta(const char *device_id, char *buf, size_t len);
esp_err_t wb_device_topic_error(const char *device_id, char *buf, size_t len);
esp_err_t wb_device_topic_control(const char *device_id, const char *control, char *buf, size_t len);
esp_err_t wb_device_topic_control_meta(const char *device_id, const char *control, char *buf, size_t len);
esp_err_t wb_device_topic_control_error(const char *device_id, const char *control, char *buf, size_t len);
esp_err_t wb_device_topic_control_command(const char *device_id, const char *control, char *buf, size_t len);

const char *wb_device_control_name(wb_control_id_t id);

// The meta JSON of the device and of one control. Both return a malloc'd string the caller
// frees, or NULL when the JSON could not be built.
char *wb_device_meta_json(void);
char *wb_device_control_meta_json(wb_control_id_t id);

// Bind this layer to a connection. device_id is copied; transport is copied too, so the caller
// may keep it on the stack.
void wb_device_init(const char *device_id, const wb_device_transport_t *transport);

// Everything a fresh session needs, in the order the convention asks for: the device meta, its
// empty error, then per control the meta, the error and the value - and finally the
// subscriptions to the /on topics of the writable controls. Called on every CONNACK, because a
// broker that lost its retained state must be told again.
void wb_device_publish_all(void);

// One pass of the periodic update: publishes the controls whose value changed and nothing else.
// While the drive is not answering, the four controls that come from it keep their last good
// value and carry "r" in their meta/error instead.
void wb_device_publish_changed(void);

// Handle one message from a /devices/<id>/controls/<c>/on topic. Returns ESP_OK when the
// command was accepted and applied, ESP_ERR_NOT_FOUND when the topic is not a command topic of
// this device (or names a read-only control), and ESP_ERR_INVALID_ARG when the payload is not a
// value this control takes. The strings need not be null-terminated: both lengths are explicit,
// which is how esp-mqtt hands them over.
esp_err_t wb_device_handle_command(const char *topic, int topic_len, const char *payload, int payload_len);

// Forget what has been published, so the next wb_device_publish_all() is a full republish.
void wb_device_reset(void);
