#include "info_handlers.h"
#include "json_utils.h"
#include "auth.h"
#include "bridge.h"
#include "wifi_apsta.h"
#include "config.h"
#include "sys_info.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_netif.h>
#include <string.h>

static const char *TAG = "info_handlers";

esp_err_t info_handlers_init(void)
{
    ESP_LOGI(TAG, "Info handlers initialized");
    return ESP_OK;
}

static void add_info_to_json_group(cJSON *group, const char *json_key, const void *value, size_t size)
{
    if (size == sizeof(int)) {
        cJSON_AddNumberToObject(group, json_key, *(const int *)value);
    } else if ((size == sizeof(bool)) || (size == sizeof(uint8_t))) {
        cJSON_AddBoolToObject(group, json_key, *(const bool *)value);
    } else {
        cJSON_AddStringToObject(group, json_key, (const char *)value);
    }
}

static esp_err_t info_build_device_json(cJSON **device_json)
{
    if (device_json == NULL) {
        return ESP_FAIL;
    }

    *device_json = cJSON_CreateObject();
    if (*device_json == NULL) {
        ESP_LOGE(TAG, "Failed to create device JSON object");
        return ESP_FAIL;
    }

    add_info_to_json_group(*device_json, "device_name", sys_info.device_name, sizeof(sys_info.device_name));
    add_info_to_json_group(*device_json, "firmware", FIRMWARE_VERSION, sizeof(FIRMWARE_VERSION));
    add_info_to_json_group(*device_json, "hardware", sys_info.hardware_ver, sizeof(sys_info.hardware_ver));
    add_info_to_json_group(*device_json, "serial_num", &sys_info.device_serial_num, sizeof(sys_info.device_serial_num));

    return ESP_OK;
}

static esp_err_t info_build_network_json(cJSON **network_json)
{
    if (network_json == NULL) {
        return ESP_FAIL;
    }

    *network_json = cJSON_CreateObject();
    if (*network_json == NULL) {
        ESP_LOGE(TAG, "Failed to create network JSON object");
        return ESP_FAIL;
    }

    // Ethernet group
    cJSON *ethernet = cJSON_CreateObject();
    if (ethernet == NULL) {
        ESP_LOGE(TAG, "Failed to create Ethernet JSON object");
        cJSON_Delete(*network_json);
        return ESP_FAIL;
    }

    add_info_to_json_group(ethernet, "con_eth", &sys_info.eth_is_connected, sizeof(sys_info.eth_is_connected));
    add_info_to_json_group(ethernet, "ip", sys_info.eth_ip, sizeof(sys_info.eth_ip));
    add_info_to_json_group(ethernet, "mask", sys_info.eth_mask, sizeof(sys_info.eth_mask));
    add_info_to_json_group(ethernet, "gw", sys_info.eth_gw, sizeof(sys_info.eth_gw));
    add_info_to_json_group(ethernet, "mac", sys_info.eth_mac, sizeof(sys_info.eth_mac));
    cJSON_AddItemToObject(*network_json, "ethernet", ethernet);

    // WiFi group
    cJSON *wifi = cJSON_CreateObject();
    if (wifi == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi JSON object");
        cJSON_Delete(*network_json);
        return ESP_FAIL;
    }

    add_info_to_json_group(wifi, "con_sta", &sys_info.wifi_sta_is_connected, sizeof(sys_info.wifi_sta_is_connected));
    add_info_to_json_group(wifi, "sta_ip", sys_info.wifi_sta_ip, sizeof(sys_info.wifi_sta_ip));
    add_info_to_json_group(wifi, "sta_mask", sys_info.wifi_sta_mask, sizeof(sys_info.wifi_sta_mask));
    add_info_to_json_group(wifi, "sta_gw", sys_info.wifi_sta_gw, sizeof(sys_info.wifi_sta_gw));
    add_info_to_json_group(wifi, "con_ap", &sys_info.wifi_ap_connections_count, sizeof(sys_info.wifi_ap_connections_count));

    // Add WiFi STA RSSI
    if (sys_info.wifi_sta_is_connected) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            cJSON_AddNumberToObject(wifi, "sta_rssi", ap_info.rssi);
        } else {
            cJSON_AddNumberToObject(wifi, "sta_rssi", 0);
        }
    } else {
        cJSON_AddNumberToObject(wifi, "sta_rssi", 0);
    }

    add_info_to_json_group(wifi, "ap_channel", &(int){WIFI_CHAN_AP}, sizeof(int));
    add_info_to_json_group(wifi, "sta_mac", sys_info.wifi_sta_mac, sizeof(sys_info.wifi_sta_mac));
    add_info_to_json_group(wifi, "ap_mac", sys_info.wifi_ap_mac, sizeof(sys_info.wifi_ap_mac));
    cJSON_AddItemToObject(*network_json, "wifi", wifi);

    return ESP_OK;
}

static cJSON *create_rs485_port_json(int port_num)
{
    cJSON *rs485_port = cJSON_CreateObject();
    if (rs485_port == NULL) {
        ESP_LOGE(TAG, "Failed to create RS485_%d JSON object", port_num);
        return NULL;
    }

    if (port_num == 1) {
        cJSON_AddBoolToObject(rs485_port, "is_busy", sys_info.rs485_1_is_busy);
        cJSON_AddNumberToObject(rs485_port, "error_percentage", sys_info.rs485_1_error_percentage);
        cJSON_AddNumberToObject(rs485_port, "server_connections_count", tcp_server_active_connections(TCP_SERVER_1));
    } else if (port_num == 2) {
        cJSON_AddBoolToObject(rs485_port, "is_busy", sys_info.rs485_2_is_busy);
        cJSON_AddNumberToObject(rs485_port, "error_percentage", sys_info.rs485_2_error_percentage);
        cJSON_AddNumberToObject(rs485_port, "server_connections_count", tcp_server_active_connections(TCP_SERVER_2));
    }

    return rs485_port;
}

static esp_err_t info_build_rs485_json(cJSON **rs485_json)
{
    if (rs485_json == NULL) {
        return ESP_FAIL;
    }

    *rs485_json = cJSON_CreateObject();
    if (*rs485_json == NULL) {
        ESP_LOGE(TAG, "Failed to create RS485 JSON object");
        return ESP_FAIL;
    }

    // Create RS485_1 and RS485_2 objects
    for (int port = 1; port <= 2; port++) {
        cJSON *rs485_port = create_rs485_port_json(port);
        if (rs485_port == NULL) {
            cJSON_Delete(*rs485_json);
            return ESP_FAIL;
        }

        char port_name[16];
        snprintf(port_name, sizeof(port_name), "rs485_%d", port);
        cJSON_AddItemToObject(*rs485_json, port_name, rs485_port);
    }

    return ESP_OK;
}

static esp_err_t info_build_uptime_json(cJSON **uptime_json)
{
    if (uptime_json == NULL) {
        return ESP_FAIL;
    }

    *uptime_json = cJSON_CreateObject();
    if (*uptime_json == NULL) {
        ESP_LOGE(TAG, "Failed to create uptime JSON object");
        return ESP_FAIL;
    }

    uint32_t uptime = esp_timer_get_time() / 1000000;  // Convert microseconds to seconds
    int days = uptime / (24 * 3600);
    uptime %= (24 * 3600);
    int hours = uptime / 3600;
    uptime %= 3600;
    int minutes = uptime / 60;
    int seconds = uptime % 60;

    cJSON_AddNumberToObject(*uptime_json, "days", days);
    cJSON_AddNumberToObject(*uptime_json, "hours", hours);
    cJSON_AddNumberToObject(*uptime_json, "minutes", minutes);
    cJSON_AddNumberToObject(*uptime_json, "seconds", seconds);

    return ESP_OK;
}

static esp_err_t info_build_ap_clients_json(cJSON **clients_json)
{
    if (clients_json == NULL) {
        return ESP_FAIL;
    }

    *clients_json = cJSON_CreateArray();
    if (*clients_json == NULL) {
        ESP_LOGE(TAG, "Failed to create AP clients JSON array");
        return ESP_FAIL;
    }

    wifi_sta_list_t sta_list;

    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK && sta_list.num > 0) {
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap_netif == NULL) {
            ESP_LOGW(TAG, "Failed to get AP netif handle");
            // Still proceed with MAC and RSSI info
        }

        esp_netif_pair_mac_ip_t *mac_ip_pairs = malloc(sta_list.num * sizeof(esp_netif_pair_mac_ip_t));
        if (mac_ip_pairs == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for MAC-IP pairs");
            cJSON_Delete(*clients_json);
            return ESP_FAIL;
        }

        memset(mac_ip_pairs, 0, sta_list.num * sizeof(esp_netif_pair_mac_ip_t));
        for (int i = 0; i < sta_list.num; ++i) {
            memcpy(mac_ip_pairs[i].mac, sta_list.sta[i].mac, 6);
        }

        bool got_ips = false;
        if (ap_netif != NULL) {
            got_ips = (esp_netif_dhcps_get_clients_by_mac(ap_netif, sta_list.num, mac_ip_pairs) == ESP_OK);
        }

        for (int i = 0; i < sta_list.num; ++i) {
            wifi_sta_info_t *sta = &sta_list.sta[i];
            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     sta->mac[0], sta->mac[1], sta->mac[2],
                     sta->mac[3], sta->mac[4], sta->mac[5]);

            cJSON *client = cJSON_CreateObject();
            if (client == NULL) {
                ESP_LOGE(TAG, "Failed to create client JSON object");
                free(mac_ip_pairs);
                cJSON_Delete(*clients_json);
                return ESP_FAIL;
            }

            cJSON_AddStringToObject(client, "mac", mac_str);
            cJSON_AddNumberToObject(client, "rssi", sta->rssi);

            char ip_str[16] = "0.0.0.0";
            if (got_ips) {
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&mac_ip_pairs[i].ip));
            }
            cJSON_AddStringToObject(client, "ip", ip_str);

            cJSON_AddItemToArray(*clients_json, client);
        }

        free(mac_ip_pairs);
    }

    return ESP_OK;
}

static esp_err_t update_info_from_json(cJSON *req_json, const char *key, void *dest, int type)
{
    if (cJSON_HasObjectItem(req_json, key)) {
        cJSON *item = cJSON_GetObjectItem(req_json, key);
        if (item->type == type) {
            if (type == cJSON_String) {
                strncpy((char *)dest, item->valuestring, SYS_INFO_MAX_STR_LEN);
            } else if (type == cJSON_Number) {
                *(int *)dest = item->valueint;
            } else {
                ESP_LOGW(TAG, "Unknown type json item");
                return ESP_FAIL;
            }
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static esp_err_t info_update_from_json(cJSON *request_json)
{
    if (request_json == NULL) {
        return ESP_FAIL;
    }

    esp_err_t result = ESP_OK;

    if (update_info_from_json(request_json, "device_name", sys_info.device_name, cJSON_String) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update device_name");
        result = ESP_FAIL;
    }

    if (update_info_from_json(request_json, "hardware", sys_info.hardware_ver, cJSON_String) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update hardware");
        result = ESP_FAIL;
    }

    if (update_info_from_json(request_json, "serial_num", &sys_info.device_serial_num, cJSON_Number) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update serial_num");
        result = ESP_FAIL;
    }

    return result;
}

esp_err_t info_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "System info GET request");

    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        return ESP_FAIL;
    }

    // Build device info
    cJSON *device_json = NULL;
    if (info_build_device_json(&device_json) == ESP_OK && device_json != NULL) {
        cJSON *device_name = cJSON_DetachItemFromObject(device_json, "device_name");
        cJSON *firmware = cJSON_DetachItemFromObject(device_json, "firmware");
        cJSON *hardware = cJSON_DetachItemFromObject(device_json, "hardware");
        cJSON *serial_num = cJSON_DetachItemFromObject(device_json, "serial_num");

        if (device_name) {
            cJSON_AddItemToObject(response_json, "device_name", device_name);
        }
        if (firmware) {
            cJSON_AddItemToObject(response_json, "firmware", firmware);
        }
        if (hardware) {
            cJSON_AddItemToObject(response_json, "hardware", hardware);
        }
        if (serial_num) {
            cJSON_AddItemToObject(response_json, "serial_num", serial_num);
        }

        cJSON_Delete(device_json);
    }

    // Build network info
    cJSON *network_json = NULL;
    if (info_build_network_json(&network_json) == ESP_OK && network_json != NULL) {
        cJSON *ethernet = cJSON_DetachItemFromObject(network_json, "ethernet");
        cJSON *wifi = cJSON_DetachItemFromObject(network_json, "wifi");

        if (ethernet) {
            cJSON_AddItemToObject(response_json, "ethernet", ethernet);
        }
        if (wifi) {
            cJSON_AddItemToObject(response_json, "wifi", wifi);
        }

        cJSON_Delete(network_json);
    }

    // Build RS485 info
    cJSON *rs485_json = NULL;
    if (info_build_rs485_json(&rs485_json) == ESP_OK && rs485_json != NULL) {
        cJSON *rs485_1 = cJSON_DetachItemFromObject(rs485_json, "rs485_1");
        cJSON *rs485_2 = cJSON_DetachItemFromObject(rs485_json, "rs485_2");

        if (rs485_1) {
            cJSON_AddItemToObject(response_json, "rs485_1", rs485_1);
        }
        if (rs485_2) {
            cJSON_AddItemToObject(response_json, "rs485_2", rs485_2);
        }

        cJSON_Delete(rs485_json);
    }

    json_utils_send_response(req, NULL, response_json);
    return ESP_OK;
}

esp_err_t info_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "System info POST request");

    if (!auth_middleware_check(req)) {
        return ESP_FAIL;
    }

    cJSON *request_json = json_utils_receive_json(req);
    if (request_json == NULL) {
        return ESP_FAIL;
    }

    esp_err_t result = info_update_from_json(request_json);

    if (result != ESP_OK) {
        json_utils_cleanup(request_json, NULL);
        return json_utils_send_error(req, "Failed to update system info");
    }

    json_utils_cleanup(request_json, NULL);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t uptime_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Uptime GET request");

    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *uptime_json = NULL;
    esp_err_t result = info_build_uptime_json(&uptime_json);

    if ((result != ESP_OK) || (uptime_json == NULL)) {
        ESP_LOGE(TAG, "Failed to build uptime JSON");
        return json_utils_send_error(req, "Failed to get uptime");
    }

    char *json_str = cJSON_Print(uptime_json);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to print uptime JSON");
        cJSON_Delete(uptime_json);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(uptime_json);

    return ESP_OK;
}

esp_err_t ap_clients_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "AP clients GET request");

    if (!auth_middleware_check(req)) {
        return ESP_FAIL;
    }

    cJSON *clients_json = NULL;
    esp_err_t result = info_build_ap_clients_json(&clients_json);

    if ((result != ESP_OK) || (clients_json == NULL)) {
        ESP_LOGE(TAG, "Failed to build AP clients JSON");
        return json_utils_send_error(req, "Failed to get AP clients");
    }

    char *json_str = cJSON_Print(clients_json);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to print AP clients JSON");
        cJSON_Delete(clients_json);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(clients_json);

    return ESP_OK;
}
