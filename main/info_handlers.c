#include "info_handlers.h"
#include "json_utils.h"
#include "auth.h"
#include "setting_items.h"
#include "bridge.h"
#include "wifi_apsta.h"
#include "config.h"
#include "sys_info.h"
#include "voltage_monitor.h"
#include "config_button.h"
#include "copy_protection.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_netif.h>
#include <string.h>

static const char *TAG = "info_handlers";

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

    cJSON_AddBoolToObject(ethernet, "con_eth", sys_info.eth_is_connected);
    cJSON_AddStringToObject(ethernet, "ip", sys_info.eth_ip);
    cJSON_AddStringToObject(ethernet, "mask", sys_info.eth_mask);
    cJSON_AddStringToObject(ethernet, "gw", sys_info.eth_gw);
    cJSON_AddStringToObject(ethernet, "mac", sys_info.eth_mac);
    cJSON_AddItemToObject(*network_json, "ethernet", ethernet);

    // WiFi group
    cJSON *wifi = cJSON_CreateObject();
    if (wifi == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi JSON object");
        cJSON_Delete(*network_json);
        return ESP_FAIL;
    }

    cJSON_AddBoolToObject(wifi, "enabled", sys_info.wifi_enabled);
    cJSON_AddStringToObject(wifi, "mode", sys_info.wifi_mode);

    cJSON_AddBoolToObject(wifi, "con_sta", sys_info.wifi_sta_is_connected);
    cJSON_AddStringToObject(wifi, "con_sta_ssid", sys_info.wifi_sta_con_ssid);
    cJSON_AddStringToObject(wifi, "sta_ip", sys_info.wifi_sta_ip);
    cJSON_AddStringToObject(wifi, "sta_mask", sys_info.wifi_sta_mask);
    cJSON_AddStringToObject(wifi, "sta_gw", sys_info.wifi_sta_gw);

    cJSON_AddNumberToObject(wifi, "con_ap", sys_info.wifi_ap_connections_count);
    cJSON_AddStringToObject(wifi, "ap_ip", sys_info.wifi_ap_ip);
    cJSON_AddStringToObject(wifi, "ap_mask", sys_info.wifi_ap_mask);
    cJSON_AddStringToObject(wifi, "ap_gw", sys_info.wifi_ap_gw);

    // Add WiFi STA RSSI
    if (sys_info.wifi_sta_is_connected) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            cJSON_AddNumberToObject(wifi, "sta_rssi", ap_info.rssi);
        } else {
            cJSON_AddNumberToObject(wifi, "sta_rssi", -128);
        }
    } else {
        cJSON_AddNumberToObject(wifi, "sta_rssi", -128);
    }

    cJSON_AddNumberToObject(wifi, "ap_channel", WIFI_CHAN_AP);
    cJSON_AddStringToObject(wifi, "sta_mac", sys_info.wifi_sta_mac);
    cJSON_AddStringToObject(wifi, "ap_mac", sys_info.wifi_ap_mac);
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
        cJSON_AddBoolToObject(rs485_port, "is_busy", sys_info.rs485_is_busy[0]);
        cJSON_AddNumberToObject(rs485_port, "error_percentage", sys_info.rs485_error_percentage[0]);
        cJSON_AddNumberToObject(rs485_port, "server_connections_count", tcp_server_active_connections(TCP_SERVER_1));
    } else if (port_num == 2) {
        cJSON_AddBoolToObject(rs485_port, "is_busy", sys_info.rs485_is_busy[1]);
        cJSON_AddNumberToObject(rs485_port, "error_percentage", sys_info.rs485_error_percentage[1]);
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

static esp_err_t info_build_wb_status_json(cJSON **wb_status_json)
{
    if (wb_status_json == NULL) {
        return ESP_FAIL;
    }

    *wb_status_json = cJSON_CreateObject();
    if (*wb_status_json == NULL) {
        ESP_LOGE(TAG, "Failed to create wb_status JSON object");
        return ESP_FAIL;
    }

    #if (!QEMU_BUILD)
        copy_protection_state_t prot_state = copy_protection_get_state();
        const char* prot_state_str;
        switch (prot_state) {
            case COPY_PROT_STATE_OK:
                prot_state_str = "ok";
                break;
            case COPY_PROT_STATE_FAIL:
                prot_state_str = "fail";
                break;
            default:
            case COPY_PROT_STATE_UNKNOWN:
                prot_state_str = "unknown";
                break;
        }
    #else
        const char* prot_state_str = "unknown";
    #endif

    cJSON_AddStringToObject(*wb_status_json, "fw_status", prot_state_str);

    return ESP_OK;
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
        json_utils_send_error(req, "Failed to create response");
        return ESP_OK;
    }

    // Build device info directly
    cJSON_AddStringToObject(response_json, "device_name", sys_info.device_name);
    cJSON_AddStringToObject(response_json, "signature", sys_info.device_signature);
    cJSON_AddStringToObject(response_json, "firmware", sys_info.firmware_ver);
    cJSON_AddStringToObject(response_json, "git_info", sys_info.firmware_git_info);
    cJSON_AddNumberToObject(response_json, "serial_num", sys_info.device_serial_num);

    // Add system voltage measurement
    float system_voltage = voltage_monitor_get_sys_voltage();
    cJSON_AddNumberToObject(response_json, "system_voltage", system_voltage);

    // Add config button press count
    uint32_t button_presses = config_button_get_press_count();
    cJSON_AddNumberToObject(response_json, "config_button_presses", button_presses);

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
        json_utils_send_error(req, "Failed to get uptime");
        return ESP_OK;
    }

    json_utils_send_response(req, NULL, uptime_json);
    return ESP_OK;
}

esp_err_t ap_clients_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "AP clients GET request");

    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *clients_json = NULL;
    esp_err_t result = info_build_ap_clients_json(&clients_json);

    if ((result != ESP_OK) || (clients_json == NULL)) {
        ESP_LOGE(TAG, "Failed to build AP clients JSON");
        json_utils_send_error(req, "Failed to get AP clients");
        return ESP_OK;
    }

    json_utils_send_response(req, NULL, clients_json);
    return ESP_OK;
}

esp_err_t wb_status_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WB status GET request");

    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *wb_status_json = NULL;
    esp_err_t result = info_build_wb_status_json(&wb_status_json);

    if ((result != ESP_OK) || (wb_status_json == NULL)) {
        ESP_LOGE(TAG, "Failed to build WB status JSON");
        json_utils_send_error(req, "Failed to get WB status");
        return ESP_OK;
    }

    json_utils_send_response(req, NULL, wb_status_json);
    return ESP_OK;
}

esp_err_t hostname_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Hostname GET request");

    char hostname[SETTING_ITEM_MAX_STR_LEN] = { 0 };
    setting_items_read(KEY_HOSTNAME, hostname);

    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        json_utils_send_error(req, "Failed to create response");
        return ESP_OK;
    }
    cJSON_AddStringToObject(response_json, "hostname", hostname);
    json_utils_send_response(req, NULL, response_json);
    return ESP_OK;
}
