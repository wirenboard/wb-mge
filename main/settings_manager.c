#include "settings_manager.h"
#include "setting_items.h"
#include "json_utils.h"
#include "auth.h"
#include "array_size.h"
#include "settings_update.h"
#include "settings_save_timer.h"
#include "bridge/cache_modbus_server.h"

#include <esp_log.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "settings_manager";

#define SETTING_KEY_BUF_SIZE 64

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
};

static const setting_mapping_t rs485_bridge_mappings[] = {
    {"mode", "bridge_mode"},
    {"port", "bridge_port"},
    {"ip", "bridge_ip"},
    {"modbus", "bridge_modbus"},
};

static esp_err_t add_rs485_settings_to_json(cJSON *parent);

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

// Helper function to save JSON value using automatic type detection
static bool save_setting_from_json(cJSON *item, const char *setting_key) {
    setting_item_type_t type = setting_items_get_type(setting_key);

    switch (type) {
    case SETTING_ITEM_TYPE_STRING:
        if (!cJSON_IsString(item)) {
            ESP_LOGE(TAG, "Expected string for setting %s", setting_key);
            return false;
        }
        return setting_items_save(setting_key, item->valuestring) == ESP_OK;

    case SETTING_ITEM_TYPE_BOOL:
        if (!cJSON_IsBool(item)) {
            ESP_LOGE(TAG, "Expected boolean for setting %s", setting_key);
            return false;
        }
        return setting_items_save_bool(setting_key, cJSON_IsTrue(item)) == ESP_OK;

    case SETTING_ITEM_TYPE_INT:
        if (!cJSON_IsNumber(item)) {
            ESP_LOGE(TAG, "Expected number for setting %s", setting_key);
            return false;
        }
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
            }

            save_setting_from_json(item, setting_key);
        }
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

    // Add WiFi settings group
    if (add_group_to_json(*response_json, "wifi", wifi_mappings, ARRAY_SIZE(wifi_mappings)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add WiFi settings to JSON");
        cJSON_Delete(*response_json);
        return ESP_FAIL;
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

                save_setting_from_json(item, key_buf);
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

                        save_setting_from_json(item, key_buf);
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

    // Save current cache server port before applying new settings.
    // Used later to detect a real port change and to enable rollback on init failure.
    int old_cache_port = cache_modbus_server_get_port();

    // Process top-level settings
    for (size_t i = 0; i < ARRAY_SIZE(top_level_mappings); i++) {
        const setting_mapping_t *mapping = &top_level_mappings[i];
        if (cJSON_HasObjectItem(request_json, mapping->json_key)) {
            cJSON *item = cJSON_GetObjectItem(request_json, mapping->json_key);
            save_setting_from_json(item, mapping->setting_key);
        }
    }

    // Process WiFi settings group
    if (cJSON_HasObjectItem(request_json, "wifi")) {
        cJSON *wifi_json = cJSON_GetObjectItem(request_json, "wifi");
        if (cJSON_IsObject(wifi_json)) {
            save_group_settings(wifi_json, wifi_mappings, ARRAY_SIZE(wifi_mappings), NULL);
        }
    }

    // Process Ethernet settings group
    if (cJSON_HasObjectItem(request_json, "ethernet")) {
        cJSON *eth_json = cJSON_GetObjectItem(request_json, "ethernet");
        if (cJSON_IsObject(eth_json)) {
            save_group_settings(eth_json, ethernet_mappings, ARRAY_SIZE(ethernet_mappings), NULL);
        }
    }

    process_rs485_settings(request_json);

    // If cache_modbus_port was in the request, check whether the validated NVS value
    // differs from the port active before this request and restart the server if needed.
    if (cJSON_HasObjectItem(request_json, "cache_modbus_port")) {
        int new_port = setting_items_read_int(KEY_CACHE_MODBUS_PORT);
        if (new_port > 0 && new_port != old_cache_port) {
            esp_err_t ret = cache_modbus_server_deinit();
            if (ret != ESP_OK) {
                // Log only — continue so that settings_update() still runs for other settings.
                ESP_LOGE(TAG, "cache_modbus_server_deinit failed: %s", esp_err_to_name(ret));
            } else {
                ret = cache_modbus_server_init(new_port);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "cache_modbus_server_init(%d) failed: %s", new_port, esp_err_to_name(ret));
                    // Attempt rollback to the previous port.
                    if (old_cache_port > 0) {
                        esp_err_t rb = cache_modbus_server_init(old_cache_port);
                        if (rb != ESP_OK) {
                            ESP_LOGE(TAG, "Rollback to port %d also failed: %s", old_cache_port, esp_err_to_name(rb));
                        }
                    }
                }
            }
        }
    }

    settings_update();

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
        return ESP_FAIL;
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
