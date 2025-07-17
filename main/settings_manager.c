#include "settings_manager.h"
#include "setting_items.h"
#include "json_utils.h"
#include "auth.h"
#include "update_rs485_mio_gpio_states.h"

#include <esp_log.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "settings_manager";

typedef struct {
    const char *json_key;
    const char *setting_key;
    enum {
        TYPE_STRING,
        TYPE_BOOL,
        TYPE_INT
    } type;
} setting_mapping_t;

static const setting_mapping_t top_level_mappings[] = {
    {"hostname", "hostname", TYPE_STRING},
    {"login", "login", TYPE_STRING},
    {"web_port", "web_port", TYPE_INT},
    {"io_bus", "io_bus", TYPE_BOOL},
    {"vout", "vout", TYPE_BOOL},
};

static const setting_mapping_t wifi_mappings[] = {
    {"mode", "wifi_mode", TYPE_STRING},
    {"ap_auth", "ap_auth", TYPE_STRING},
    {"sta_auth", "sta_auth", TYPE_STRING},
    {"ap_ssid", "ap_ssid", TYPE_STRING},
    {"ap_pass", "ap_pass", TYPE_STRING},
    {"sta_ssid", "sta_ssid", TYPE_STRING},
    {"sta_pass", "sta_pass", TYPE_STRING},
    {"ap_ip_static", "ap_ip_static", TYPE_STRING},
    {"ap_mask_static", "ap_mask_static", TYPE_STRING},
    {"ap_gw_static", "ap_gw_static", TYPE_STRING},
};

static const setting_mapping_t ethernet_mappings[] = {
    {"ip_static", "eth_ip_static", TYPE_STRING},
    {"mask_static", "eth_mask_static", TYPE_STRING},
    {"gw_static", "eth_gw_static", TYPE_STRING},
    {"dhcpc", "eth_dhcpc", TYPE_BOOL},
};

static const setting_mapping_t rs485_base_mappings[] = {
    {"baudrate", "baudrate", TYPE_INT},
    {"stopbits", "stopbits", TYPE_STRING},
    {"parity", "parity", TYPE_STRING},
    {"databits", "databits", TYPE_STRING},
    {"term", "term", TYPE_BOOL},
    {"fail_safe", "fail_safe", TYPE_BOOL},
};

static const setting_mapping_t rs485_bridge_mappings[] = {
    {"mode", "bridge_mode", TYPE_STRING},
    {"port", "bridge_port", TYPE_INT},
    {"ip", "bridge_ip", TYPE_STRING},
    {"modbus", "bridge_mb", TYPE_BOOL},
};

static esp_err_t add_rs485_settings_to_json(cJSON *parent);

static bool add_setting_to_json(cJSON *parent, const char *setting_key, const char *json_key,
                                int type) {
    switch (type) {
        case TYPE_STRING: {
            char value[SETTING_ITEM_MAX_STR_LEN] = {0};
            if (setting_items_read(setting_key, value) != ESP_OK) {
                return false;
            }
            return cJSON_AddStringToObject(parent, json_key, value) != NULL;
        }
        case TYPE_BOOL: {
            bool value = setting_items_read_bool(setting_key);
            return cJSON_AddBoolToObject(parent, json_key, value) != NULL;
        }
        case TYPE_INT: {
            int value = setting_items_read_int(setting_key);
            return cJSON_AddNumberToObject(parent, json_key, value) != NULL;
        }
        default:
            return false;
    }
}

static bool save_setting_from_json(cJSON *item, const char *setting_key, int type) {
    switch (type) {
        case TYPE_STRING:
            if (!cJSON_IsString(item)) return false;
            return setting_items_save(setting_key, item->valuestring) == ESP_OK;

        case TYPE_BOOL:
            if (!cJSON_IsBool(item)) return false;
            return setting_items_save_bool(setting_key, cJSON_IsTrue(item)) == ESP_OK;

        case TYPE_INT:
            if (!cJSON_IsNumber(item)) return false;
            return setting_items_save_int(setting_key, (int)item->valuedouble) == ESP_OK;

        default:
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
        add_setting_to_json(group_json, mappings[i].setting_key, mappings[i].json_key,
                           mappings[i].type);
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
            char setting_key[64];
            if (suffix && strlen(suffix) > 0) {
                snprintf(setting_key, sizeof(setting_key), "%s_%s", mappings[i].setting_key, suffix);
            } else {
                strncpy(setting_key, mappings[i].setting_key, sizeof(setting_key) - 1);
            }

            save_setting_from_json(item, setting_key, mappings[i].type);
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
    for (size_t i = 0; i < sizeof(top_level_mappings) / sizeof(top_level_mappings[0]); i++) {
        add_setting_to_json(*response_json, top_level_mappings[i].setting_key,
                           top_level_mappings[i].json_key,
                           top_level_mappings[i].type);
    }

    // Add WiFi settings group
    if (add_group_to_json(*response_json, "wifi", wifi_mappings,
                         sizeof(wifi_mappings) / sizeof(wifi_mappings[0])) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add WiFi settings to JSON");
        cJSON_Delete(*response_json);
        return ESP_FAIL;
    }

    // Add Ethernet settings group
    if (add_group_to_json(*response_json, "ethernet", ethernet_mappings,
                         sizeof(ethernet_mappings) / sizeof(ethernet_mappings[0])) != ESP_OK) {
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
    char key_buf[64];

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
            add_setting_to_json(rs485_port, key_buf, mapping->json_key,
                               mapping->type);
        }

        // Add bridge subgroup
        cJSON *bridge = cJSON_CreateObject();
        if (bridge == NULL) {
            ESP_LOGE(TAG, "Failed to create bridge JSON object for RS485_%d", port);
            cJSON_Delete(rs485_port);
            return ESP_FAIL;
        }

        for (size_t i = 0; i < sizeof(rs485_bridge_mappings) / sizeof(rs485_bridge_mappings[0]); i++) {
            const setting_mapping_t *mapping = &rs485_bridge_mappings[i];
            snprintf(key_buf, sizeof(key_buf), "%s_%d", mapping->setting_key, port);
            add_setting_to_json(bridge, key_buf, mapping->json_key,
                               mapping->type);
        }
        cJSON_AddItemToObject(rs485_port, "bridge", bridge);

        // Add to main response
        snprintf(key_buf, sizeof(key_buf), "rs485_%d", port);
        cJSON_AddItemToObject(parent, key_buf, rs485_port);
    }

    return ESP_OK;
}

static esp_err_t process_rs485_settings(cJSON *request_json, cJSON *response_json)
{
    const char *rs485_json_names[] = {"rs485_1", "rs485_2"};
    const char *rs485_suffix[] = {"1", "2"};
    char key_buf[64];

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
        for (size_t i = 0; i < sizeof(rs485_base_mappings) / sizeof(rs485_base_mappings[0]); i++) {
            const setting_mapping_t *mapping = &rs485_base_mappings[i];

            if (cJSON_HasObjectItem(rs485, mapping->json_key)) {
                cJSON *item = cJSON_GetObjectItem(rs485, mapping->json_key);

                // Create setting key with port suffix
                snprintf(key_buf, sizeof(key_buf), "%s_%s", mapping->setting_key, rs485_suffix[port]);

                save_setting_from_json(item, key_buf, mapping->type);
            }
        }

        // Handle bridge subgroup
        if (cJSON_HasObjectItem(rs485, "bridge")) {
            cJSON *bridge = cJSON_GetObjectItem(rs485, "bridge");
            if (cJSON_IsObject(bridge)) {
                for (size_t i = 0; i < sizeof(rs485_bridge_mappings) / sizeof(rs485_bridge_mappings[0]); i++) {
                    const setting_mapping_t *mapping = &rs485_bridge_mappings[i];

                    if (cJSON_HasObjectItem(bridge, mapping->json_key)) {
                        cJSON *item = cJSON_GetObjectItem(bridge, mapping->json_key);

                        // Create setting key with port suffix
                        snprintf(key_buf, sizeof(key_buf), "%s_%s", mapping->setting_key, rs485_suffix[port]);

                        save_setting_from_json(item, key_buf, mapping->type);
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

    // Process top-level settings
    for (size_t i = 0; i < sizeof(top_level_mappings) / sizeof(top_level_mappings[0]); i++) {
        const setting_mapping_t *mapping = &top_level_mappings[i];
        if (cJSON_HasObjectItem(request_json, mapping->json_key)) {
            cJSON *item = cJSON_GetObjectItem(request_json, mapping->json_key);
            save_setting_from_json(item, mapping->setting_key, mapping->type);
        }
    }

    // Process WiFi settings group
    if (cJSON_HasObjectItem(request_json, "wifi")) {
        cJSON *wifi_json = cJSON_GetObjectItem(request_json, "wifi");
        if (cJSON_IsObject(wifi_json)) {
            save_group_settings(wifi_json, wifi_mappings,
                               sizeof(wifi_mappings) / sizeof(wifi_mappings[0]), NULL);
        }
    }

    // Process Ethernet settings group
    if (cJSON_HasObjectItem(request_json, "ethernet")) {
        cJSON *eth_json = cJSON_GetObjectItem(request_json, "ethernet");
        if (cJSON_IsObject(eth_json)) {
            save_group_settings(eth_json, ethernet_mappings,
                               sizeof(ethernet_mappings) / sizeof(ethernet_mappings[0]), NULL);
        }
    }

    process_rs485_settings(request_json, *response_json);

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

    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *request_json = json_utils_receive_json(req);
    if (request_json == NULL) {
        return ESP_FAIL;
    }

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
