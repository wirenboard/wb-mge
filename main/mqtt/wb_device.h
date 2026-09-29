#pragma once

/*
 * The Wiren Board MQTT convention, as the fingerprint access control speaks it.
 *
 * Everything that knows what a topic looks like and what a control's meta JSON contains lives
 * here; the client below it (mqtt_client_wb.c) only opens the connection and hands this layer
 * a way to publish and to subscribe - the transport is two function pointers.
 *
 * The topics, straight from wirenboard/conventions:
 *     /devices/<id>/meta                      device meta, retained
 *     /devices/<id>/meta/error                device error string, retained
 *     /devices/<id>/controls/<c>/meta         control meta, retained
 *     /devices/<id>/controls/<c>              control value, retained
 *     /devices/<id>/controls/<c>/meta/error   control error string, retained
 *     /devices/<id>/controls/<c>/on           command from the panel, NEVER retained
 *
 * The controls:
 *     sensor_online   switch, read-only    the sensor answers
 *     last_result     text, read-only      granted | denied | unknown, empty before the first event
 *     last_user       text, read-only      the user of the last event, empty when nobody
 *     last_source     text, read-only      fingerprint | telegram, empty before the first event
 *     event_count     value, read-only     the sequence number of the last event (0: none yet);
 *                                          it moves on every attempt, so a rule can trigger on it
 *     user_<id>       switch, read-write   one per user of the store, titled with the user's
 *                                          name; the value is "enabled", and a write sets it
 *
 * The user controls follow the store: a new user gets one, a rename republishes its meta, and a
 * deleted user's retained topics are cleared with empty payloads. The first session after
 * wb_device_init() clears them for every id the store does not have, so a user deleted while
 * there was no session is cleared too, even across a reboot or a restart of the client.
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

// A formatted control value. The longest is a user name (FP_STORE_NAME_MAX bytes).
#define WB_DEVICE_VALUE_MAX_LEN     64

// The whole device is unreachable: neither readable nor writable. Published as the Last Will,
// so a device that drops off the network says so itself.
#define WB_DEVICE_ERROR_OFFLINE     "rw"

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

// /devices/<id>/meta/error, the topic of the Last Will. ESP_ERR_INVALID_SIZE rather than a
// truncated topic.
esp_err_t wb_device_topic_error(const char *device_id, char *buf, size_t len);

// Bind this layer to a connection. device_id is copied; transport is copied too, so the caller
// may keep it on the stack. Forgets everything; since which user controls this device has on the
// broker is then unknown, every user id counts as possibly there, and the first session clears
// the retained topics of each one the store does not have.
void wb_device_init(const char *device_id, const wb_device_transport_t *transport);

// Everything a fresh session needs: the device meta, its empty error, the meta and value of
// every control, the clearing of the user controls whose user was deleted meanwhile (in the
// first session after wb_device_init(), of every id the store does not have), and last the
// subscription to /devices/<id>/controls/+/on. Called on every CONNACK, because a broker
// that lost its retained state must be told again.
void wb_device_publish_all(void);

// One pass of the periodic update: publishes the values that changed and, when the store's
// users_rev moved, the user controls that were added, renamed, toggled or deleted.
void wb_device_publish_changed(void);

// Handle one message from a /devices/<id>/controls/<c>/on topic. Returns ESP_OK when the
// command was accepted and applied, ESP_ERR_NOT_FOUND when the topic is not a command topic of
// this device (or names a read-only control, or a user that does not exist), and
// ESP_ERR_INVALID_ARG when the payload is not "0" or "1". The strings need not be
// null-terminated: both lengths are explicit, which is how esp-mqtt hands them over.
esp_err_t wb_device_handle_command(const char *topic, int topic_len, const char *payload, int payload_len);

// Forget what has been published, so the next wb_device_publish_all() is a full republish. Which
// user controls exist on the broker is kept, so that one can still clear a deleted user's.
void wb_device_reset(void);
