#include "settings_manager.h"
#include "setting_items.h"
#include "json_utils.h"
#include "auth.h"
#include "array_size.h"
#include "settings_update.h"
#include "settings_save_timer.h"

#include <esp_log.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

static const char *TAG = "settings_manager";

#define SETTING_KEY_BUF_SIZE 64
#define WARNING_MSG_BUF_SIZE 224

// Machine-readable codes of the warnings reported in the response's "warnings" array.
#define WARNING_CODE_PORT_COLLISION "port_collision"
// The cache_en change was saved but could not be applied to the running ports.
#define WARNING_CODE_CACHE_APPLY    "cache_apply_failed"
// Everything was saved to NVS, but settings_update() refused to apply any of it because the
// PREVIOUS apply was still running. Reported rather than swallowed: without it the response is
// indistinguishable from a normal save, and the user is left wondering why the device did not
// change. Distinct from cache_apply_failed, which is about one setting reaching the live ports.
#define WARNING_CODE_APPLY_BUSY     "apply_busy"

typedef struct {
    const char *json_key;
    const char *setting_key;
} setting_mapping_t;

static const setting_mapping_t top_level_mappings[] = {
    {"hostname", KEY_HOSTNAME},
    {"login", KEY_LOGIN},
    {"pass", KEY_PASS},
    {"web_port", KEY_WEB_PORT},
    {"io_bus", KEY_IO_BUS_ENABLED},
    {"vout", KEY_485_VOUT},
    {"cache_modbus_port", KEY_CACHE_MODBUS_PORT},
    {"cache_modbus_server_enabled", KEY_CACHE_MODBUS_SERVER_ENABLED},
    {"cache_value_timeout_s", KEY_CACHE_VALUE_TIMEOUT_S},
    {"update_channel", KEY_UPDATE_CHANNEL},
};

static const setting_mapping_t wifi_mappings[] = {
    {"mode", KEY_WIFI_MODE},
    {"ap_auth", KEY_WIFI_AUTH_AP},
    {"sta_auth", KEY_WIFI_AUTH_STA},
    {"ap_ssid", KEY_AP_SSID},
    {"ap_pass", KEY_AP_PASS},
    {"sta_ssid", KEY_STA_SSID},
    {"sta_pass", KEY_STA_PASS},
    {"ap_ip_static", KEY_AP_IP_STATIC},
    {"ap_mask_static", KEY_AP_MASK_STATIC},
    {"ap_gw_static", KEY_AP_GW_STATIC},
    {"sta_dhcpc", KEY_STA_DHCPC},
    {"sta_ip_static", KEY_STA_IP_STATIC},
    {"sta_mask_static", KEY_STA_MASK_STATIC},
    {"sta_gw_static", KEY_STA_GW_STATIC},
};

static const setting_mapping_t ethernet_mappings[] = {
    {"ip_static", KEY_ETH_IP_STATIC},
    {"mask_static", KEY_ETH_MASK_STATIC},
    {"gw_static", KEY_ETH_GW_STATIC},
    {"dhcpc", KEY_ETH_DHCPC},
};

static const setting_mapping_t rs485_base_mappings[] = {
    {"baudrate", "baudrate"},
    {"stopbits", "stopbits"},
    {"parity", "parity"},
    {"databits", "databits"},
    {"term", "485_term"},
    {"fail_safe", "485_fail_safe"},
    {"tx_disabled", "485_tx_dis"},
    // -> NVS port_mode_N (STRING, validate_port_mode).
    //
    // Note the deliberate asymmetry with POST /ports/N/mode, which is refused with 409
    // while the clock_out factory test runs, whereas port_mode via POST /settings is
    // accepted (200) even then. It is not an inconsistency:
    //   - POST /ports/N/mode APPLIES the mode immediately (port_manager_set_mode ->
    //     deinit + re-init of the port). During the test that would hand the TX and DE
    //     pins back to the UART while the test is driving them — the TX pin from the
    //     LEDC, the DE pin from the level the test holds it at (port 1 HIGH, port 2 LOW,
    //     both plain GPIO) — so it must be refused.
    //   - POST /settings only WRITES NVS here; the applying step is settings_update(),
    //     and port_manager_apply_settings() is a no-op while the ports are frozen. The
    //     new mode simply takes effect when the test ends and wb_test restores both
    //     ports from NVS — which is exactly what a settings write is supposed to mean.
    // So the request that touches the live hardware is blocked, and the one that only
    // records intent is not.
    {"port_mode", "port_mode"},
    {"cache_en", "cache_en"},     // -> NVS cache_en_N  (BOOL,   validate_bool)
};

static const setting_mapping_t rs485_bridge_mappings[] = {
    {"mode", "bridge_mode"},
    {"port", "bridge_port"},
    {"ip", "bridge_ip"},
    {"modbus", "bridge_modbus"},
};

static esp_err_t add_rs485_settings_to_json(cJSON *parent);
static bool validate_setting_from_json(cJSON *item, const char *setting_key);

// Helper function to add setting to JSON using automatic type detection
static bool add_setting_to_json(cJSON *parent, const char *setting_key, const char *json_key) {
    setting_item_type_t type = setting_items_get_type(setting_key);

    switch (type) {
    case SETTING_ITEM_TYPE_STRING: {
        char value[SETTING_ITEM_MAX_STR_LEN] = { 0 };
        if (setting_items_read(setting_key, value) != ESP_OK) {
            return false;
        }
        return cJSON_AddStringToObject(parent, json_key, value) != NULL;
    }
    case SETTING_ITEM_TYPE_BOOL: {
        bool value = setting_items_read_bool(setting_key);
        return cJSON_AddBoolToObject(parent, json_key, value) != NULL;
    }
    case SETTING_ITEM_TYPE_INT: {
        int value = setting_items_read_int(setting_key);
        return cJSON_AddNumberToObject(parent, json_key, value) != NULL;
    }
    default:
        ESP_LOGE(TAG, "Unknown setting type for key: %s", setting_key);
        return false;
    }
}

// Helper function to save JSON value using automatic type detection.
// The JSON type checks, the INT range check and the value validation are exactly those of
// validate_setting_from_json(), so reuse it here instead of duplicating them: what remains
// is only the type-specific write itself.
static bool save_setting_from_json(cJSON *item, const char *setting_key) {
    if (!validate_setting_from_json(item, setting_key)) {
        return false;
    }

    setting_item_type_t type = setting_items_get_type(setting_key);

    switch (type) {
    case SETTING_ITEM_TYPE_STRING:
        return setting_items_save(setting_key, item->valuestring) == ESP_OK;

    case SETTING_ITEM_TYPE_BOOL:
        return setting_items_save_bool(setting_key, cJSON_IsTrue(item)) == ESP_OK;

    case SETTING_ITEM_TYPE_INT:
        return setting_items_save_int(setting_key, (int)item->valuedouble) == ESP_OK;

    default:
        ESP_LOGE(TAG, "Unknown setting type for key: %s", setting_key);
        return false;
    }
}

static esp_err_t add_group_to_json(cJSON *response_json, const char *group_name,
                                   const setting_mapping_t *mappings, size_t mapping_count) {
    cJSON *group_json = cJSON_CreateObject();
    if (!group_json) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < mapping_count; i++) {
        add_setting_to_json(group_json, mappings[i].setting_key, mappings[i].json_key);
    }

    cJSON_AddItemToObject(response_json, group_name, group_json);
    return ESP_OK;
}

static esp_err_t save_group_settings(cJSON *group_json, const setting_mapping_t *mappings,
                                     size_t mapping_count, const char *suffix) {
    if (!group_json || !mappings) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < mapping_count; i++) {
        cJSON *item = cJSON_GetObjectItem(group_json, mappings[i].json_key);
        if (item) {
            char setting_key[SETTING_KEY_BUF_SIZE];
            if (suffix && strlen(suffix) > 0) {
                snprintf(setting_key, sizeof(setting_key), "%s_%s", mappings[i].setting_key, suffix);
            } else {
                strncpy(setting_key, mappings[i].setting_key, sizeof(setting_key) - 1);
                setting_key[sizeof(setting_key) - 1] = '\0';
            }

            // Return early on NVS write failure so the caller can report success:false
            // instead of silently writing a partial set of settings.
            if (!save_setting_from_json(item, setting_key)) {
                ESP_LOGE(TAG, "Failed to save setting '%s'", setting_key);
                return ESP_FAIL;
            }
        }
    }

    return ESP_OK;
}

// Phase 1 validation functions — check types and run validators without writing to NVS.

// Validate a single JSON item against the expected type and validator for setting_key.
// For INT type: converts the numeric value to a string and passes it to setting_items_validate().
// For BOOL type: converts to "true"/"false" and passes it to setting_items_validate().
// For STRING type: passes item->valuestring directly.
// Returns true if the value is valid, false otherwise.
static bool validate_setting_from_json(cJSON *item, const char *setting_key)
{
    setting_item_type_t type = setting_items_get_type(setting_key);
    char str_value[SETTING_ITEM_MAX_STR_LEN];

    switch (type) {
    case SETTING_ITEM_TYPE_STRING:
        if (!cJSON_IsString(item)) {
            ESP_LOGE(TAG, "Validation failed: expected string for setting '%s'", setting_key);
            return false;
        }
        return setting_items_validate(setting_key, item->valuestring) == ESP_OK;

    case SETTING_ITEM_TYPE_BOOL:
        if (!cJSON_IsBool(item)) {
            ESP_LOGE(TAG, "Validation failed: expected boolean for setting '%s'", setting_key);
            return false;
        }
        return setting_items_validate(setting_key, cJSON_IsTrue(item) ? "true" : "false") == ESP_OK;

    case SETTING_ITEM_TYPE_INT:
        if (!cJSON_IsNumber(item)) {
            ESP_LOGE(TAG, "Validation failed: expected number for setting '%s'", setting_key);
            return false;
        }
        // Casting a double outside [INT_MIN, INT_MAX] to int is undefined behaviour;
        // reject the value before the cast.
        if ((item->valuedouble < (double)INT_MIN) || (item->valuedouble > (double)INT_MAX)) {
            ESP_LOGE(TAG, "Validation failed: integer value out of range for setting '%s': %f",
                     setting_key, item->valuedouble);
            return false;
        }
        snprintf(str_value, sizeof(str_value), "%d", (int)item->valuedouble);
        return setting_items_validate(setting_key, str_value) == ESP_OK;

    default:
        ESP_LOGE(TAG, "Validation failed: unknown type for setting '%s'", setting_key);
        return false;
    }
}

// Validate all fields in a JSON group object against the provided mappings.
// suffix is appended to each setting key (with "_" separator) when non-empty.
// Returns false on the first invalid field.
static bool validate_group_settings(cJSON *group_json, const setting_mapping_t *mappings,
                                    size_t mapping_count, const char *suffix)
{
    if (!group_json || !mappings) {
        return true; // Nothing to validate — missing group means "leave unchanged"
    }

    if (!cJSON_IsObject(group_json)) {
        ESP_LOGW(TAG, "Validation: group must be a JSON object");
        return false;
    }

    for (size_t i = 0; i < mapping_count; i++) {
        cJSON *item = cJSON_GetObjectItem(group_json, mappings[i].json_key);
        if (item) {
            char setting_key[SETTING_KEY_BUF_SIZE];
            if (suffix && strlen(suffix) > 0) {
                snprintf(setting_key, sizeof(setting_key), "%s_%s", mappings[i].setting_key, suffix);
            } else {
                strncpy(setting_key, mappings[i].setting_key, sizeof(setting_key) - 1);
                setting_key[sizeof(setting_key) - 1] = '\0';
            }

            if (!validate_setting_from_json(item, setting_key)) {
                return false;
            }
        }
    }

    return true;
}

// Validate top-level settings in the request JSON.
// Returns false on the first invalid field.
static bool validate_top_level_settings(cJSON *request_json)
{
    for (size_t i = 0; i < ARRAY_SIZE(top_level_mappings); i++) {
        const setting_mapping_t *mapping = &top_level_mappings[i];
        if (cJSON_HasObjectItem(request_json, mapping->json_key)) {
            cJSON *item = cJSON_GetObjectItem(request_json, mapping->json_key);
            if (!validate_setting_from_json(item, mapping->setting_key)) {
                return false;
            }
        }
    }
    return true;
}

// "Effective" value of a setting = the value carried by this request if present, otherwise the
// current NVS value. Using it lets the cross-field checks below cover a request that changes only
// one side of a colliding pair. parent may be NULL (the group is absent from the request) — the
// NVS value is then used.
//
// The parent == NULL case returns early instead of feeding a NULL item to cJSON_Is*(): those
// calls do handle NULL, but the static analyser cannot see that through the opaque declaration
// and reports the ternary's NULL branch as a null dereference of ->valueint / ->valuestring.

static int effective_int(cJSON *parent, const char *json_key, const char *nvs_key)
{
    if (parent == NULL) {
        return setting_items_read_int(nvs_key);
    }
    cJSON *item = cJSON_GetObjectItem(parent, json_key);
    return cJSON_IsNumber(item) ? item->valueint : setting_items_read_int(nvs_key);
}

static bool effective_bool(cJSON *parent, const char *json_key, const char *nvs_key)
{
    if (parent == NULL) {
        return setting_items_read_bool(nvs_key);
    }
    cJSON *item = cJSON_GetObjectItem(parent, json_key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : setting_items_read_bool(nvs_key);
}

static bool nvs_str_equals(const char *nvs_key, const char *expected)
{
    char value[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (setting_items_read(nvs_key, value) != ESP_OK) {
        return false;
    }
    return strncmp(value, expected, SETTING_ITEM_MAX_STR_LEN) == 0;
}

static bool effective_str_equals(cJSON *parent, const char *json_key, const char *nvs_key,
                                 const char *expected)
{
    if (parent == NULL) {
        return nvs_str_equals(nvs_key, expected);
    }
    cJSON *item = cJSON_GetObjectItem(parent, json_key);
    if (cJSON_IsString(item)) {
        return strncmp(item->valuestring, expected, SETTING_ITEM_MAX_STR_LEN) == 0;
    }
    return nvs_str_equals(nvs_key, expected);
}

// Return parent's child object, or NULL if absent / not an object. parent may be NULL.
static cJSON *get_object_or_null(cJSON *parent, const char *key)
{
    cJSON *obj = parent ? cJSON_GetObjectItem(parent, key) : NULL;
    return (obj && cJSON_IsObject(obj)) ? obj : NULL;
}

// True when the request carries this key (parent may be NULL — group absent from the request).
static bool json_has(cJSON *parent, const char *key)
{
    return (parent != NULL) && cJSON_HasObjectItem(parent, key);
}

// Append one {code, message} object to the warnings array. warnings may be NULL (the caller does
// not collect warnings), and an allocation failure is silently ignored: a warning is advisory, so
// losing it must never turn an otherwise valid request into a failure.
static void add_warning(cJSON *warnings, const char *code, const char *message)
{
    if (warnings == NULL) {
        return;
    }

    cJSON *entry = cJSON_CreateObject();
    if (entry == NULL) {
        return;
    }

    if ((cJSON_AddStringToObject(entry, "code", code) == NULL) ||
        (cJSON_AddStringToObject(entry, "message", message) == NULL)) {
        cJSON_Delete(entry);
        return;
    }

    cJSON_AddItemToArray(warnings, entry);
}

// Hand the collected warnings to the response, or drop them when there is nothing to report.
// "warnings" is an OPTIONAL addition: it is present only when there is something to say, so a
// clean request still gets exactly the response it got before and the API contract does not
// change for clients that never look at it.
//
// Called on EVERY exit path of settings_process_request_json(), including the ones that answer
// success:false — a warning describes the resulting configuration, not this request's verdict,
// so it is reported whether the request was accepted or rejected. Takes ownership of `warnings`
// either way, and tolerates a NULL one (cJSON_CreateArray() came up empty-handed).
static void attach_warnings(cJSON *response_json, cJSON *warnings)
{
    if (cJSON_GetArraySize(warnings) > 0) {
        cJSON_AddItemToObject(response_json, "warnings", warnings);
    } else {
        cJSON_Delete(warnings);
    }
}

// Cross-field validation: no two TCP services may listen on the same port. Allowing it leaves one
// of them unable to bind (listen() -> EADDRINUSE errno 112) and, under repeated re-init without a
// reboot, a stuck listen socket that permanently occupies the port.
//
// Only ports that are actually bound LOCALLY take part in the check:
//   web_port          — always (the config web server always listens);
//   cache_modbus_port — only when the cache Modbus server is enabled;
//   bridge_port_N     — only when port_mode_N == tcp_bridge AND bridge_mode_N == server; in client
//                       mode the port belongs to the REMOTE peer, so nothing is bound locally and
//                       a "collision" with it is harmless.
// Every pair of them is compared, which is what the old check was missing: it only compared
// cache_modbus_port against the two bridge ports, so bridge_port_1 == bridge_port_2, and anything
// colliding with web_port (default 80), went straight through.
//
// Only collisions this request has a hand in are rejected. A listener counts as "touched" when the
// request carries any of the fields that define it — its port, or the fields that make it a local
// listener at all. A collision between two UNTOUCHED listeners is inherited from the saved
// configuration (older firmware validated fewer pairs, so such devices exist) and must NOT fail the
// request: it would make EVERY subsequent POST fail, including one that only changes the Wi-Fi
// password, and the device could never be repaired over the REST API field by field. The factory
// defaults (80/502/503/504) do not collide, so a fresh device is never in that state.
//
// An accepted inherited collision still leaves one of the two listeners unable to bind, so it is
// also appended to the warnings array (when the caller supplies one) and travels back to the client
// in the response — otherwise the dead port would only ever be visible in the firmware log.
static bool validate_port_collisions(cJSON *request_json, cJSON *warnings)
{
    static const char *const rs485_names[] = {"rs485_1", "rs485_2"};
    static const char *const port_mode_keys[] = {KEY_PORT_MODE1, KEY_PORT_MODE2};
    static const char *const bridge_mode_keys[] = {KEY_BRIDGE_MODE1, KEY_BRIDGE_MODE2};
    static const char *const bridge_port_keys[] = {KEY_BRIDGE_PORT1, KEY_BRIDGE_PORT2};

    typedef struct {
        const char *name;    // human-readable source of the port, used in the log
        int         port;
        bool        touched; // this request carries one of the fields that define this listener
    } listener_t;

    // web_port + cache_modbus_port + one bridge gateway per RS-485 port.
    listener_t listeners[2 + ARRAY_SIZE(rs485_names)];
    size_t count = 0;

    listeners[count].name = "web_port";
    listeners[count].port = effective_int(request_json, "web_port", KEY_WEB_PORT);
    listeners[count].touched = json_has(request_json, "web_port");
    count++;

    if (effective_bool(request_json, "cache_modbus_server_enabled", KEY_CACHE_MODBUS_SERVER_ENABLED)) {
        listeners[count].name = "cache_modbus_port";
        listeners[count].port = effective_int(request_json, "cache_modbus_port", KEY_CACHE_MODBUS_PORT);
        // Enabling the server is what makes it a listener, so that counts as touching it too.
        listeners[count].touched = json_has(request_json, "cache_modbus_port") ||
                                   json_has(request_json, "cache_modbus_server_enabled");
        count++;
    }

    for (size_t i = 0; i < ARRAY_SIZE(rs485_names); i++) {
        cJSON *rs485 = get_object_or_null(request_json, rs485_names[i]);
        cJSON *bridge = get_object_or_null(rs485, "bridge");

        if (!effective_str_equals(rs485, "port_mode", port_mode_keys[i], PORT_MODE_TCP_BRIDGE_STR)) {
            continue;   // port is disabled / passive / repeater — no TCP gateway
        }
        if (!effective_str_equals(bridge, "mode", bridge_mode_keys[i], BRIDGE_MODE_SERVER_STR)) {
            continue;   // client mode — the port is remote, nothing is bound locally
        }

        listeners[count].name = rs485_names[i];
        listeners[count].port = effective_int(bridge, "port", bridge_port_keys[i]);
        // port_mode / bridge mode turn the gateway into a local listener, so they count as
        // touching it as well; the other bridge/serial fields (baudrate, ip, ...) do not.
        listeners[count].touched = json_has(bridge, "port") ||
                                   json_has(rs485, "port_mode") ||
                                   json_has(bridge, "mode");
        count++;
    }

    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (listeners[i].port != listeners[j].port) {
                continue;
            }
            if (!listeners[i].touched && !listeners[j].touched) {
                ESP_LOGW(TAG, "Validation: %s (%d) already collides with %s (%d) in the saved "
                              "configuration; this request does not change either — accepting it",
                         listeners[i].name, listeners[i].port,
                         listeners[j].name, listeners[j].port);
                // Worded as a plain statement of fact about the saved configuration: the same
                // warning is attached whether the request as a whole ends up accepted or rejected
                // by a later check.
                char message[WARNING_MSG_BUF_SIZE];
                snprintf(message, sizeof(message),
                         "%s and %s are both configured on TCP port %d in the saved configuration; "
                         "one of them will fail to bind and stay down until the conflict is resolved",
                         listeners[i].name, listeners[j].name, listeners[i].port);
                add_warning(warnings, WARNING_CODE_PORT_COLLISION, message);
                continue;
            }
            ESP_LOGW(TAG, "Validation: %s (%d) collides with %s (%d)",
                     listeners[i].name, listeners[i].port,
                     listeners[j].name, listeners[j].port);
            return false;
        }
    }

    return true;
}

// Public wrapper around validate_port_collisions() for POST /ports/{n}/mode: report whether
// switching one RS-485 port to a new transport mode would introduce a new local TCP listener
// collision, without touching NVS. The REST handler calls this before applying the mode so the
// conflict is rejected up front (409) instead of surfacing later as a bind() EADDRINUSE and a
// rollback.
esp_err_t settings_manager_check_port_mode_collision(unsigned port_index, const char *new_port_mode)
{
    static const char *const rs485_names[] = {"rs485_1", "rs485_2"};

    // Out-of-range index or missing mode: nothing this call can model, so nothing to reject. The
    // REST handler already validates the index via URI registration; this is just a safety net.
    if ((port_index >= ARRAY_SIZE(rs485_names)) || (new_port_mode == NULL)) {
        return ESP_OK;
    }

    // Build a minimal request that carries ONLY this port's new mode, e.g.
    // {"rs485_1":{"port_mode":"tcp_bridge"}}. validate_port_collisions() reads every other listener
    // (the other port, web_port, the cache Modbus server) from NVS through its effective_* helpers,
    // so this single field fully models the post-switch listener set. For a non-tcp_bridge mode the
    // rs485 loop's port_mode == tcp_bridge test is false, so this port contributes no listener and
    // the result is ESP_OK.
    cJSON *request_json = cJSON_CreateObject();
    cJSON *rs485 = cJSON_CreateObject();
    if ((request_json == NULL) || (rs485 == NULL)) {
        // Allocation failure: fail open (skip the pre-check), exactly as POST /settings drops its
        // warnings on OOM. The later bind() stays as the backstop, so no valid switch is wrongly
        // rejected. rs485 is not attached yet, so freeing both here cannot double-free.
        cJSON_Delete(request_json);
        cJSON_Delete(rs485);
        return ESP_OK;
    }
    // Same fail-open contract on OOM: if the port_mode field cannot be added, rs485 would carry no
    // mode and validate_port_collisions() would silently evaluate the OLD saved mode instead of the
    // requested one — wrong either way (a false 409 when switching AWAY from tcp_bridge, or a missed
    // collision). rs485 is not attached to request_json yet, so free both separately here.
    if (cJSON_AddStringToObject(rs485, "port_mode", new_port_mode) == NULL) {
        cJSON_Delete(request_json);
        cJSON_Delete(rs485);
        return ESP_OK;
    }
    cJSON_AddItemToObject(request_json, rs485_names[port_index], rs485);

    // warnings == NULL is safe: add_warning() returns early on NULL and validate_port_collisions()
    // only ever touches the array through add_warning(). A pre-existing collision that does NOT
    // involve this port is inherited, not newly introduced, so validate_port_collisions() still
    // returns true (only a dropped warning) and the mode switch is correctly allowed.
    bool no_collision = validate_port_collisions(request_json, NULL);

    cJSON_Delete(request_json);
    return no_collision ? ESP_OK : ESP_ERR_INVALID_STATE;
}

// Validate all RS485 port settings (base fields + bridge subgroup) in the request JSON.
// Returns false on the first invalid field.
static bool validate_rs485_settings(cJSON *request_json)
{
    const char *rs485_json_names[] = {"rs485_1", "rs485_2"};
    const char *rs485_suffix[] = {"1", "2"};
    char key_buf[SETTING_KEY_BUF_SIZE];

    for (int port = 0; port < 2; ++port) {
        if (!cJSON_HasObjectItem(request_json, rs485_json_names[port])) {
            continue;
        }

        cJSON *rs485 = cJSON_GetObjectItem(request_json, rs485_json_names[port]);
        if (!cJSON_IsObject(rs485)) {
            ESP_LOGW(TAG, "Validation: %s must be an object", rs485_json_names[port]);
            return false;
        }

        // Validate regular RS485 base fields
        for (size_t i = 0; i < ARRAY_SIZE(rs485_base_mappings); i++) {
            const setting_mapping_t *mapping = &rs485_base_mappings[i];

            if (cJSON_HasObjectItem(rs485, mapping->json_key)) {
                cJSON *item = cJSON_GetObjectItem(rs485, mapping->json_key);
                snprintf(key_buf, sizeof(key_buf), "%s_%s", mapping->setting_key, rs485_suffix[port]);

                if (!validate_setting_from_json(item, key_buf)) {
                    return false;
                }
            }
        }

        // Validate bridge subgroup
        if (cJSON_HasObjectItem(rs485, "bridge")) {
            cJSON *bridge = cJSON_GetObjectItem(rs485, "bridge");
            if (!cJSON_IsObject(bridge)) {
                ESP_LOGW(TAG, "Validation: bridge in %s must be an object", rs485_json_names[port]);
                return false;
            }

            for (size_t i = 0; i < ARRAY_SIZE(rs485_bridge_mappings); i++) {
                const setting_mapping_t *mapping = &rs485_bridge_mappings[i];

                if (cJSON_HasObjectItem(bridge, mapping->json_key)) {
                    cJSON *item = cJSON_GetObjectItem(bridge, mapping->json_key);
                    snprintf(key_buf, sizeof(key_buf), "%s_%s", mapping->setting_key, rs485_suffix[port]);

                    if (!validate_setting_from_json(item, key_buf)) {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

// Add the wifi_perm_disable flag to the response. On failure the response JSON is freed,
// so the caller only has to propagate the error.
static esp_err_t add_wifi_perm_disable_flag(cJSON *response_json, bool value)
{
    if (!cJSON_AddBoolToObject(response_json, "wifi_perm_disable", value)) {
        ESP_LOGE(TAG, "Failed to add wifi_perm_disable flag to JSON");
        cJSON_Delete(response_json);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t settings_build_response_json(cJSON **response_json)
{
    if (response_json == NULL) {
        return ESP_FAIL;
    }

    *response_json = cJSON_CreateObject();
    if (*response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        return ESP_FAIL;
    }

    // Add top-level settings
    for (size_t i = 0; i < ARRAY_SIZE(top_level_mappings); i++) {
        add_setting_to_json(*response_json, top_level_mappings[i].setting_key,
                           top_level_mappings[i].json_key);
    }

    // Report the wifi_perm_disable flag, then add the WiFi settings group - the group is
    // omitted when WiFi is permanently disabled, leaving only the flag.
    bool wifi_perm_disabled = setting_items_read_bool(KEY_WIFI_PERM_DISABLE);
    if (add_wifi_perm_disable_flag(*response_json, wifi_perm_disabled) != ESP_OK) {
        return ESP_FAIL;
    }
    if (!wifi_perm_disabled) {
        if (add_group_to_json(*response_json, "wifi", wifi_mappings, ARRAY_SIZE(wifi_mappings)) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add WiFi settings to JSON");
            cJSON_Delete(*response_json);
            return ESP_FAIL;
        }
    }

    // Add Ethernet settings group
    if (add_group_to_json(*response_json, "ethernet", ethernet_mappings,
                         ARRAY_SIZE(ethernet_mappings)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add Ethernet settings to JSON");
        cJSON_Delete(*response_json);
        return ESP_FAIL;
    }

    // Add RS485 settings (special case with port suffixes)
    if (add_rs485_settings_to_json(*response_json) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add RS485 settings to JSON");
        cJSON_Delete(*response_json);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t add_rs485_settings_to_json(cJSON *parent)
{
    char key_buf[SETTING_KEY_BUF_SIZE];

    for (int port = 1; port <= 2; ++port) {
        // Create RS485 port object
        cJSON *rs485_port = cJSON_CreateObject();
        if (rs485_port == NULL) {
            ESP_LOGE(TAG, "Failed to create RS485_%d JSON object", port);
            return ESP_FAIL;
        }

        // Add regular RS485 fields using mappings
        for (size_t i = 0; i < ARRAY_SIZE(rs485_base_mappings); i++) {
            const setting_mapping_t *mapping = &rs485_base_mappings[i];
            snprintf(key_buf, sizeof(key_buf), "%s_%d", mapping->setting_key, port);
            add_setting_to_json(rs485_port, key_buf, mapping->json_key);
        }

        // Add bridge subgroup
        cJSON *bridge = cJSON_CreateObject();
        if (bridge == NULL) {
            ESP_LOGE(TAG, "Failed to create bridge JSON object for RS485_%d", port);
            cJSON_Delete(rs485_port);
            return ESP_FAIL;
        }

        for (size_t i = 0; i < ARRAY_SIZE(rs485_bridge_mappings); i++) {
            const setting_mapping_t *mapping = &rs485_bridge_mappings[i];
            snprintf(key_buf, sizeof(key_buf), "%s_%d", mapping->setting_key, port);
            add_setting_to_json(bridge, key_buf, mapping->json_key);
        }
        cJSON_AddItemToObject(rs485_port, "bridge", bridge);

        // Add to main response
        snprintf(key_buf, sizeof(key_buf), "rs485_%d", port);
        cJSON_AddItemToObject(parent, key_buf, rs485_port);
    }

    return ESP_OK;
}

static esp_err_t process_rs485_settings(cJSON *request_json)
{
    const char *rs485_json_names[] = {"rs485_1", "rs485_2"};
    const char *rs485_suffix[] = {"1", "2"};
    char key_buf[SETTING_KEY_BUF_SIZE];

    for (int port = 0; port < 2; ++port) {
        if (!cJSON_HasObjectItem(request_json, rs485_json_names[port])) {
            continue;
        }

        cJSON *rs485 = cJSON_GetObjectItem(request_json, rs485_json_names[port]);
        if (!cJSON_IsObject(rs485)) {
            ESP_LOGW(TAG, "%s must be an object", rs485_json_names[port]);
            continue;
        }

        // Process regular RS485 fields using mappings
        for (size_t i = 0; i < ARRAY_SIZE(rs485_base_mappings); i++) {
            const setting_mapping_t *mapping = &rs485_base_mappings[i];

            if (cJSON_HasObjectItem(rs485, mapping->json_key)) {
                cJSON *item = cJSON_GetObjectItem(rs485, mapping->json_key);

                // Create setting key with port suffix
                snprintf(key_buf, sizeof(key_buf), "%s_%s", mapping->setting_key, rs485_suffix[port]);

                // Return early on NVS write failure so the caller can report success:false.
                if (!save_setting_from_json(item, key_buf)) {
                    ESP_LOGE(TAG, "Failed to save RS485 setting '%s'", key_buf);
                    return ESP_FAIL;
                }
            }
        }

        // Handle bridge subgroup
        if (cJSON_HasObjectItem(rs485, "bridge")) {
            cJSON *bridge = cJSON_GetObjectItem(rs485, "bridge");
            if (cJSON_IsObject(bridge)) {
                for (size_t i = 0; i < ARRAY_SIZE(rs485_bridge_mappings); i++) {
                    const setting_mapping_t *mapping = &rs485_bridge_mappings[i];

                    if (cJSON_HasObjectItem(bridge, mapping->json_key)) {
                        cJSON *item = cJSON_GetObjectItem(bridge, mapping->json_key);

                        // Create setting key with port suffix
                        snprintf(key_buf, sizeof(key_buf), "%s_%s", mapping->setting_key, rs485_suffix[port]);

                        // Return early on NVS write failure so the caller can report success:false.
                        if (!save_setting_from_json(item, key_buf)) {
                            ESP_LOGE(TAG, "Failed to save RS485 bridge setting '%s'", key_buf);
                            return ESP_FAIL;
                        }
                    }
                }
            }
        }
    }

    return ESP_OK;
}

esp_err_t settings_process_request_json(cJSON *request_json, cJSON **response_json)
{
    if ((request_json == NULL) || (response_json == NULL)) {
        return ESP_FAIL;
    }

    *response_json = cJSON_CreateObject();
    if (*response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        return ESP_FAIL;
    }

    // --- Phase 0: determine wifi_perm_disable intent (do NOT write to NVS yet) ---
    bool wifi_perm_disabled = setting_items_read_bool(KEY_WIFI_PERM_DISABLE);
    bool should_set_wifi_perm_disable = false;
    if (cJSON_HasObjectItem(request_json, "wifi_perm_disable")) {
        cJSON *perm_dis_item = cJSON_GetObjectItem(request_json, "wifi_perm_disable");
        if (cJSON_IsBool(perm_dis_item)) {
            if (cJSON_IsTrue(perm_dis_item)) {
                // Activate permanent disable
                should_set_wifi_perm_disable = true;
                wifi_perm_disabled = true; // used in Phase 1 validation logic only
            } else {
#if QEMU_BUILD
                // In QEMU builds, allow clearing the flag for test fixture teardown.
                // On real hardware this path is not compiled, so the API cannot clear
                // the flag — but it is not irreversible: a factory reset
                // (setting_items_set_defaults) or an NVS erase still resets it to false.
                should_set_wifi_perm_disable = true;
                wifi_perm_disabled = false;
#endif
                // On hardware: false is silently ignored — the API cannot clear the flag
                //              (factory reset / NVS erase still do)
            }
        }
    }

    /* Phase 1: validate all fields before writing anything */
    // Collects the non-fatal findings of the request: the inherited port collisions that are
    // accepted rather than rejected (Phase 1), and a cache overlay that was saved but could not
    // be applied to the running ports (after settings_update(), at the bottom). They are
    // advisory, so an allocation failure here just means no warnings are reported, not a failed
    // request. Attached to the response by attach_warnings() on every exit path below — it must
    // outlive the validation, because the last contributor to it runs after the writes.
    cJSON *warnings = cJSON_CreateArray();

    // When wifi is permanently disabled, skip WiFi group validation entirely
    cJSON *wifi_group_for_validation = wifi_perm_disabled
        ? NULL
        : cJSON_GetObjectItem(request_json, "wifi");
    // && instead of the negated || chain: validate_port_collisions() must still run when an
    // earlier check has passed, because it is the one that collects the warnings.
    bool settings_valid =
        validate_top_level_settings(request_json) &&
        validate_group_settings(wifi_group_for_validation, wifi_mappings,
                                ARRAY_SIZE(wifi_mappings), NULL) &&
        validate_group_settings(cJSON_GetObjectItem(request_json, "ethernet"), ethernet_mappings,
                                ARRAY_SIZE(ethernet_mappings), NULL) &&
        validate_rs485_settings(request_json) &&
        validate_port_collisions(request_json, warnings);

    if (!settings_valid) {
        ESP_LOGE(TAG, "Settings validation failed — rejecting request");
        attach_warnings(*response_json, warnings);
        cJSON_AddBoolToObject(*response_json, "success", false);
        cJSON_AddStringToObject(*response_json, "error", "Invalid settings value");
        return ESP_OK; // Return OK so HTTP layer sends the error JSON
    }
    /* Phase 2: all fields valid — apply */

    // Write wifi_perm_disable to NVS and only consider the flag active after the write succeeds.
    // Without this guard, a failed write would leave the in-memory flag set while NVS still says
    // Wi-Fi is enabled, causing subsequent WiFi group processing to be skipped silently.
    if (should_set_wifi_perm_disable) {
        const char *perm_val = wifi_perm_disabled ? "true" : "false";
        esp_err_t ret = setting_items_save(KEY_WIFI_PERM_DISABLE, perm_val);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save wifi_perm_disable: %s", esp_err_to_name(ret));
            attach_warnings(*response_json, warnings);
            cJSON_AddBoolToObject(*response_json, "success", false);
            cJSON_AddStringToObject(*response_json, "error", "Failed to save wifi_perm_disable");
            return ESP_OK; // Return OK so HTTP layer sends the error JSON
        }
        // In-memory flag updated only after confirmed NVS write
        ESP_LOGI(TAG, "WiFi permanently disabled flag set to %s via API request", perm_val);
    }

    // Process top-level settings
    for (size_t i = 0; i < ARRAY_SIZE(top_level_mappings); i++) {
        const setting_mapping_t *mapping = &top_level_mappings[i];
        if (cJSON_HasObjectItem(request_json, mapping->json_key)) {
            cJSON *item = cJSON_GetObjectItem(request_json, mapping->json_key);
            // Return early on NVS write failure so the client receives success:false
            // instead of a partial-write acknowledged as success.
            if (!save_setting_from_json(item, mapping->setting_key)) {
                ESP_LOGE(TAG, "Failed to save top-level setting '%s'", mapping->setting_key);
                attach_warnings(*response_json, warnings);
                cJSON_AddBoolToObject(*response_json, "success", false);
                cJSON_AddStringToObject(*response_json, "error", "Failed to save setting");
                return ESP_OK; // Return OK so HTTP layer sends the error JSON
            }
        }
    }

    // Process WiFi settings group — skip entirely when permanently disabled
    if (!wifi_perm_disabled && cJSON_HasObjectItem(request_json, "wifi")) {
        cJSON *wifi_json = cJSON_GetObjectItem(request_json, "wifi");
        if (cJSON_IsObject(wifi_json)) {
            // Return early on any NVS write failure; do not report success:true for partial writes.
            if (save_group_settings(wifi_json, wifi_mappings, ARRAY_SIZE(wifi_mappings), NULL) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save WiFi settings");
                attach_warnings(*response_json, warnings);
                cJSON_AddBoolToObject(*response_json, "success", false);
                cJSON_AddStringToObject(*response_json, "error", "Failed to save WiFi settings");
                return ESP_OK; // Return OK so HTTP layer sends the error JSON
            }
        }
    }

    // Process Ethernet settings group
    if (cJSON_HasObjectItem(request_json, "ethernet")) {
        cJSON *eth_json = cJSON_GetObjectItem(request_json, "ethernet");
        if (cJSON_IsObject(eth_json)) {
            // Return early on any NVS write failure; do not report success:true for partial writes.
            if (save_group_settings(eth_json, ethernet_mappings, ARRAY_SIZE(ethernet_mappings), NULL) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save Ethernet settings");
                attach_warnings(*response_json, warnings);
                cJSON_AddBoolToObject(*response_json, "success", false);
                cJSON_AddStringToObject(*response_json, "error", "Failed to save Ethernet settings");
                return ESP_OK; // Return OK so HTTP layer sends the error JSON
            }
        }
    }

    // Return early on any NVS write failure inside RS485 processing.
    if (process_rs485_settings(request_json) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save RS485 settings");
        attach_warnings(*response_json, warnings);
        cJSON_AddBoolToObject(*response_json, "success", false);
        cJSON_AddStringToObject(*response_json, "error", "Failed to save RS485 settings");
        return ESP_OK; // Return OK so HTTP layer sends the error JSON
    }

    // Applying the new settings to the live subsystems — including starting, stopping and moving
    // the cache Modbus TCP server — is settings_update()'s job: it compares each subsystem's
    // running state against NVS and reconciles the ones that actually changed. Doing it here, in
    // the HTTP handler, meant the cache server was restarted BEFORE settings_update() re-applied
    // the RS-485 ports and the web server, so no port could be handed over between them.
    //
    // One step of it is synchronous and reports back: moving the runtime cache overlay onto the
    // port rs485_N.cache_en now names. Everything else settings_update() does is either applied
    // by the async task (long after this response is sent) or retried by the next settings write,
    // so this is the one failure the client can be told about while it is still listening.
    esp_err_t cache_apply_err = ESP_OK;
    esp_err_t apply_err = settings_update_with_status(&cache_apply_err);

    // settings_update_with_status() gives up rather than block forever when a previous apply is
    // still running (see its join). That path used to be unreachable because the wait had no
    // bound — and an unbounded wait here is an unresponsive DEVICE, since esp_http_server runs
    // one worker for every request. Now it returns, and the client is told what happened.
    if (apply_err == ESP_ERR_TIMEOUT) {
        add_warning(warnings, WARNING_CODE_APPLY_BUSY,
                    "Settings were saved but not applied: the previous settings update is still "
                    "running. They take effect on the next save or after a restart");
    }

    // success stays TRUE: the settings WERE saved, and a reboot will apply the overlay from NVS.
    // What failed is only the attempt to move it on the running device, which is precisely what a
    // warning is for — the same shape as an inherited port collision above.
    if (cache_apply_err != ESP_OK) {
        char message[WARNING_MSG_BUF_SIZE];
        snprintf(message, sizeof(message),
                 "Caching was saved but could not be applied to the running ports (%s); the cache "
                 "keeps working as it did until the setting is saved again or the device restarts",
                 esp_err_to_name(cache_apply_err));
        add_warning(warnings, WARNING_CODE_CACHE_APPLY, message);
    }

    attach_warnings(*response_json, warnings);

    // Add success flag
    cJSON_AddBoolToObject(*response_json, "success", true);

    return ESP_OK;
}

esp_err_t settings_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Settings GET request");

    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *response_json = NULL;
    esp_err_t result = settings_build_response_json(&response_json);

    if ((result != ESP_OK) || (response_json == NULL)) {
        ESP_LOGE(TAG, "Failed to build settings response");
        return json_utils_send_error(req, "Failed to build settings response");
    }

    json_utils_send_response(req, NULL, response_json);
    return ESP_OK;
}

esp_err_t settings_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Settings POST request");

    settings_save_timer_auto_init();

    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *request_json = json_utils_receive_json(req);
    if (request_json == NULL) {
        return json_utils_send_error(req, "Invalid settings request JSON");
    }

    settings_save_timer_wait();

    cJSON *response_json = NULL;
    esp_err_t result = settings_process_request_json(request_json, &response_json);

    if ((result != ESP_OK) || (response_json == NULL)) {
        ESP_LOGE(TAG, "Failed to process settings request");
        json_utils_cleanup(request_json, NULL);
        return json_utils_send_error(req, "Failed to process settings request");
    }

    json_utils_send_response(req, request_json, response_json);
    return ESP_OK;
}
