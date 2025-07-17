#include "settings_manager.h"
#include "json_utils.h"
#include "auth.h"
#include "update_rs485_mio_gpio_states.h"
#include <esp_log.h>
#include <string.h>

static const char *TAG = "settings_manager";

// Forward declarations
typedef struct setting_mapping_s setting_mapping_t;
typedef struct setting_group_s setting_group_t;

// Validation function pointer type
typedef bool (*setting_validator_t)(const void *value);

struct setting_mapping_s {
    const char *json_key;           // Key in JSON request/response
    const char *setting_key;        // Key in settings storage
    setting_item_type_t type;       // Type of the setting
    setting_validator_t validator;  // Optional validation function
    bool required;                  // Whether this setting is required
};

struct setting_group_s {
    const char *group_name;         // Name of the group (e.g., "wifi", "ethernet")
    const setting_mapping_t *mappings; // Array of setting mappings
    size_t mapping_count;           // Number of mappings in the array
    bool is_top_level;              // Whether this group is at top level
};

bool settings_validate_hostname(const char *hostname)
{
    if (!hostname) {
        return false;
    }

    size_t len = strlen(hostname);

    // Check length (1-63 characters)
    if (len == 0 || len > 63) {
        return false;
    }

    // Cannot start or end with hyphen
    if (hostname[0] == '-' || hostname[len-1] == '-') {
        return false;
    }

    // Check valid characters
    for (size_t i = 0; i < len; i++) {
        char c = hostname[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }

    return true;
}

bool settings_validate_port(int port)
{
    return (port >= 1 && port <= 65535);
}

static bool validate_hostname_wrapper(const void *value) {
    return settings_validate_hostname((const char *)value);
}

static bool validate_port_wrapper(const void *value) {
    return settings_validate_port(*(const int *)value);
}

// Setting mappings for different groups
static const setting_mapping_t top_level_mappings[] = {
    {"hostname", "hostname", SETTING_ITEM_TYPE_STR, validate_hostname_wrapper, false},
    {"login", "login", SETTING_ITEM_TYPE_STR, NULL, false},
    {"web_port", "web_port", SETTING_ITEM_TYPE_NUM, validate_port_wrapper, false},
    {"io_bus", "io_bus", SETTING_ITEM_TYPE_BOOL, NULL, false},
    {"vout", "vout", SETTING_ITEM_TYPE_BOOL, NULL, false},
};

static const setting_mapping_t wifi_mappings[] = {
    {"mode", "wifi_mode", SETTING_ITEM_TYPE_STR, NULL, false},
    {"ap_auth", "ap_auth", SETTING_ITEM_TYPE_STR, NULL, false},
    {"sta_auth", "sta_auth", SETTING_ITEM_TYPE_STR, NULL, false},
    {"ap_ip_static", "ap_ip_static", SETTING_ITEM_TYPE_STR, NULL, false},
    {"ap_mask_static", "ap_mask_static", SETTING_ITEM_TYPE_STR, NULL, false},
    {"ap_gw_static", "ap_gw_static", SETTING_ITEM_TYPE_STR, NULL, false},
    {"ap_ssid", "ap_ssid", SETTING_ITEM_TYPE_STR, NULL, false},
    {"ap_pass", "ap_pass", SETTING_ITEM_TYPE_STR, NULL, false},
    {"sta_ssid", "sta_ssid", SETTING_ITEM_TYPE_STR, NULL, false},
    {"sta_pass", "sta_pass", SETTING_ITEM_TYPE_STR, NULL, false},
};

static const setting_mapping_t ethernet_mappings[] = {
    {"ip_static", "eth_ip_static", SETTING_ITEM_TYPE_STR, NULL, false},
    {"mask_static", "eth_mask_static", SETTING_ITEM_TYPE_STR, NULL, false},
    {"gw_static", "eth_gw_static", SETTING_ITEM_TYPE_STR, NULL, false},
    {"dhcpc", "eth_dhcpc", SETTING_ITEM_TYPE_BOOL, NULL, false},
};

static const setting_mapping_t rs485_base_mappings[] = {
    {"term", "term", SETTING_ITEM_TYPE_BOOL, NULL, false},
    {"fail_safe", "fail_safe", SETTING_ITEM_TYPE_BOOL, NULL, false},
    {"baudrate", "baudrate", SETTING_ITEM_TYPE_NUM, NULL, false},
    {"stopbits", "stopbits", SETTING_ITEM_TYPE_STR, NULL, false},
    {"parity", "parity", SETTING_ITEM_TYPE_STR, NULL, false},
    {"databits", "databits", SETTING_ITEM_TYPE_STR, NULL, false},
};

static const setting_mapping_t rs485_bridge_mappings[] = {
    {"mode", "bridge_mode", SETTING_ITEM_TYPE_STR, NULL, false},
    {"port", "bridge_port", SETTING_ITEM_TYPE_NUM, NULL, false},
    {"ip", "bridge_ip", SETTING_ITEM_TYPE_STR, NULL, false},
    {"modbus", "bridge_mb", SETTING_ITEM_TYPE_BOOL, NULL, false},
};

// Setting groups
static const setting_group_t setting_groups[] = {
    {"top_level", top_level_mappings, sizeof(top_level_mappings)/sizeof(top_level_mappings[0]), true},
    {"wifi", wifi_mappings, sizeof(wifi_mappings)/sizeof(wifi_mappings[0]), false},
    {"ethernet", ethernet_mappings, sizeof(ethernet_mappings)/sizeof(ethernet_mappings[0]), false},
};

const setting_group_t *settings_get_groups(void) {
    return setting_groups;
}

size_t settings_get_group_count(void) {
    return sizeof(setting_groups) / sizeof(setting_groups[0]);
}

esp_err_t settings_manager_init(void)
{
    ESP_LOGI(TAG, "Settings manager initialized");
    return ESP_OK;
}

static void add_setting_to_json(cJSON *json, const char *key, const char *json_key)
{
    setting_item_type_t type = setting_items_get_type_in_json(key);
    if (type == SETTING_ITEM_TYPE_STR) {
        char val[SETTING_ITEM_MAX_STR_LEN] = {0};
        if (setting_items_read(key, val) == 0)
            cJSON_AddStringToObject(json, json_key, val);
    } else if (type == SETTING_ITEM_TYPE_NUM) {
        int val = 0;
        if (setting_items_read(key, &val) == 0)
            cJSON_AddNumberToObject(json, json_key, val);
    } else if (type == SETTING_ITEM_TYPE_BOOL) {
        uint8_t val = 0;
        if (setting_items_read(key, &val) == 0)
            cJSON_AddBoolToObject(json, json_key, val);
    }
}

static esp_err_t add_group_to_json(cJSON *parent, const setting_group_t *group)
{
    cJSON *group_json = NULL;

    if (group->is_top_level) {
        group_json = parent;
    } else {
        group_json = cJSON_CreateObject();
        if (group_json == NULL) {
            ESP_LOGE(TAG, "Failed to create JSON object for group %s", group->group_name);
            return ESP_FAIL;
        }
        cJSON_AddItemToObject(parent, group->group_name, group_json);
    }

    // Add all mapped settings
    for (size_t i = 0; i < group->mapping_count; i++) {
        const setting_mapping_t *mapping = &group->mappings[i];
        add_setting_to_json(group_json, mapping->setting_key, mapping->json_key);
    }

    return ESP_OK;
}

static esp_err_t add_rs485_settings_to_json(cJSON *parent)
{
    char key_buf[32];

    for (int port = 1; port <= 2; ++port) {
        // Create RS485 port object
        cJSON *rs485_port = cJSON_CreateObject();
        if (rs485_port == NULL) {
            ESP_LOGE(TAG, "Failed to create RS485_%d JSON object", port);
            return ESP_FAIL;
        }

        // Add regular RS485 fields using mappings
        for (size_t i = 0; i < sizeof(rs485_base_mappings)/sizeof(rs485_base_mappings[0]); i++) {
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

        for (size_t i = 0; i < sizeof(rs485_bridge_mappings)/sizeof(rs485_bridge_mappings[0]); i++) {
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

    // Process standard setting groups using mappings
    const setting_group_t *groups = settings_get_groups();
    size_t group_count = settings_get_group_count();

    for (size_t i = 0; i < group_count; i++) {
        if (add_group_to_json(*response_json, &groups[i]) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add group %s to JSON", groups[i].group_name);
            cJSON_Delete(*response_json);
            return ESP_FAIL;
        }
    }

    // Handle RS485 settings (special case with port suffixes)
    if (add_rs485_settings_to_json(*response_json) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add RS485 settings to JSON");
        cJSON_Delete(*response_json);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t save_setting_value(const setting_mapping_t *mapping, cJSON *item, cJSON *response_json)
{
    bool success = false;

    // Type validation
    if (mapping->type == SETTING_ITEM_TYPE_STR && !cJSON_IsString(item)) {
        ESP_LOGW(TAG, "%s must be a string", mapping->json_key);
        goto fail;
    }
    if (mapping->type == SETTING_ITEM_TYPE_NUM && !cJSON_IsNumber(item)) {
        ESP_LOGW(TAG, "%s must be a number", mapping->json_key);
        goto fail;
    }

    // Extract value based on type
    void *value = NULL;
    int int_val;
    bool bool_val;

    switch (mapping->type) {
        case SETTING_ITEM_TYPE_STR:
            value = (void *)item->valuestring;
            break;
        case SETTING_ITEM_TYPE_NUM:
            int_val = item->valueint;
            value = &int_val;
            break;
        case SETTING_ITEM_TYPE_BOOL:
            bool_val = cJSON_IsTrue(item);
            value = &bool_val;
            break;
        default:
            ESP_LOGW(TAG, "Unknown type for %s", mapping->json_key);
            goto fail;
    }

    // Validate value if validator exists
    if (mapping->validator && !mapping->validator(value)) {
        ESP_LOGW(TAG, "Validation failed for %s", mapping->json_key);
        goto fail;
    }

    // Save setting
    if (setting_items_save(mapping->setting_key, value) == 0) {
        ESP_LOGI(TAG, "[%s] saved successfully", mapping->setting_key);
        success = true;
    } else {
        ESP_LOGW(TAG, "[%s] failed to save", mapping->setting_key);
    }

fail:
    if (response_json) {
        cJSON_AddBoolToObject(response_json, mapping->json_key, success);
    }
    return success ? ESP_OK : ESP_FAIL;
}

static esp_err_t process_group_settings(cJSON *request_json, cJSON *response_json, const setting_group_t *group)
{
    cJSON *group_json = NULL;

    if (group->is_top_level) {
        group_json = request_json;
    } else {
        if (!cJSON_HasObjectItem(request_json, group->group_name)) {
            return ESP_OK; // Group not present in request
        }
        group_json = cJSON_GetObjectItem(request_json, group->group_name);
        if (!cJSON_IsObject(group_json)) {
            ESP_LOGW(TAG, "%s must be an object", group->group_name);
            return ESP_OK;
        }
    }

    // Process each mapping in the group
    for (size_t i = 0; i < group->mapping_count; i++) {
        const setting_mapping_t *mapping = &group->mappings[i];

        if (cJSON_HasObjectItem(group_json, mapping->json_key)) {
            cJSON *item = cJSON_GetObjectItem(group_json, mapping->json_key);
            save_setting_value(mapping, item, response_json);
        } else if (mapping->required) {
            ESP_LOGW(TAG, "Required setting %s not found", mapping->json_key);
            if (response_json) {
                cJSON_AddBoolToObject(response_json, mapping->json_key, false);
            }
        }
    }

    return ESP_OK;
}

static esp_err_t process_rs485_settings(cJSON *request_json, cJSON *response_json)
{
    const char *rs485_json_names[] = {"rs485_1", "rs485_2"};
    const char *rs485_suffix[] = {"_1", "_2"};
    char key_buf[32];

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
        for (size_t i = 0; i < sizeof(rs485_base_mappings)/sizeof(rs485_base_mappings[0]); i++) {
            const setting_mapping_t *mapping = &rs485_base_mappings[i];

            if (cJSON_HasObjectItem(rs485, mapping->json_key)) {
                cJSON *item = cJSON_GetObjectItem(rs485, mapping->json_key);

                // Create setting key with port suffix
                snprintf(key_buf, sizeof(key_buf), "%s%s", mapping->setting_key, rs485_suffix[port]);

                // Create temporary mapping with port-specific setting key
                setting_mapping_t temp_mapping = *mapping;
                temp_mapping.setting_key = key_buf;

                save_setting_value(&temp_mapping, item, NULL);
            }
        }

        // Handle bridge subgroup
        if (cJSON_HasObjectItem(rs485, "bridge")) {
            cJSON *bridge = cJSON_GetObjectItem(rs485, "bridge");
            if (cJSON_IsObject(bridge)) {
                for (size_t i = 0; i < sizeof(rs485_bridge_mappings)/sizeof(rs485_bridge_mappings[0]); i++) {
                    const setting_mapping_t *mapping = &rs485_bridge_mappings[i];

                    if (cJSON_HasObjectItem(bridge, mapping->json_key)) {
                        cJSON *item = cJSON_GetObjectItem(bridge, mapping->json_key);

                        // Create setting key with port suffix
                        snprintf(key_buf, sizeof(key_buf), "%s%s", mapping->setting_key, rs485_suffix[port]);

                        // Create temporary mapping with port-specific setting key
                        setting_mapping_t temp_mapping = *mapping;
                        temp_mapping.setting_key = key_buf;

                        save_setting_value(&temp_mapping, item, NULL);
                    }
                }
            }
        }
    }

    return ESP_OK;
}

esp_err_t settings_process_request_json(cJSON *request_json, cJSON **response_json)
{
    if (request_json == NULL || response_json == NULL) {
        return ESP_FAIL;
    }

    *response_json = cJSON_CreateObject();
    if (*response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        return ESP_FAIL;
    }

    // Process standard setting groups using mappings
    const setting_group_t *groups = settings_get_groups();
    size_t group_count = settings_get_group_count();

    for (size_t i = 0; i < group_count; i++) {
        process_group_settings(request_json, *response_json, &groups[i]);
    }

    // Process RS485 settings (special case with port suffixes)
    process_rs485_settings(request_json, *response_json);

    // Update control systems
    update_rs485_control();
    update_io_bus_control();

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

    if (result != ESP_OK || response_json == NULL) {
        ESP_LOGE(TAG, "Failed to build settings response");
        return json_utils_send_error(req, "Failed to build settings response");
    }

    json_utils_send_response(req, NULL, response_json);
    return ESP_OK;
}

esp_err_t settings_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Settings POST request");

    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *request_json = json_utils_receive_json(req);
    if (request_json == NULL) {
        return ESP_FAIL;
    }

    cJSON *response_json = NULL;
    esp_err_t result = settings_process_request_json(request_json, &response_json);

    if (result != ESP_OK || response_json == NULL) {
        ESP_LOGE(TAG, "Failed to process settings request");
        json_utils_cleanup(request_json, NULL);
        return json_utils_send_error(req, "Failed to process settings request");
    }

    json_utils_send_response(req, request_json, response_json);
    return ESP_OK;
}