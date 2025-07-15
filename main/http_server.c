#include "http_server.h"
#include "auth_session.h"

#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <sys/param.h>

#include "array_size.h"
#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "lwip/ip4_addr.h"
#include "setting_items.h"
#include "ssdp.h"
#include "sys_info.h"
#include "wifi_apsta.h"
#include "update_rs485_mio_gpio_states.h"
#include "bridge.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"

// Размер буфера выбран таким образом, чтобы он был больше, чем размер заголовка HTTP
#define REQ_RECV_BUF_SIZE       (CONFIG_HTTPD_MAX_REQ_HDR_LEN * 2)
// Длина строки с максимальным числом uint32 (10 цифр + 1 символ для '\0')
#define UINT32_STR_MAX_LEN      11
// Максимальная длина cookie с идентификатором сессии (session_id=<u32_id>)
#define COOKIE_MAX_LEN          (11 + UINT32_STR_MAX_LEN)

#define CMD_NAME_MAX_LEN        32
#define REBOOT_DELAY_MS         1000
#define REBOOT_TASK_STACK_SIZE  2048
#define REBOOT_TASK_PRIORITY    5

typedef struct {
    int cmd_code;
    const char *cmd_name;
} cmd_t;

enum {
    CMD_REBOOT,
    CMD_SET_DEFAULT_SETTINGS,
    CMD_WRITE_FACTORY_DATA,
};

static const char *TAG = "http_server";

extern const uint8_t favicon_start[] asm("_binary_favicon_webp_gz_start");
extern const uint8_t favicon_end[] asm("_binary_favicon_webp_gz_end");

extern const uint8_t index_css_start[] asm("_binary_index_css_gz_start");
extern const uint8_t index_css_end[] asm("_binary_index_css_gz_end");

extern const uint8_t index_js_start[] asm("_binary_index_js_gz_start");
extern const uint8_t index_js_end[] asm("_binary_index_js_gz_end");

extern const uint8_t index_html_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_gz_end");

static const cmd_t cmds[] = {
    {CMD_REBOOT, "reboot"},
    {CMD_SET_DEFAULT_SETTINGS, "set_default_settings"},
    {CMD_WRITE_FACTORY_DATA, "write_factory_data"},
    {-1, NULL},
};

static void reboot_task(void *pvParameters)
{
    ESP_LOGI(TAG, "%s", __func__);

    vTaskDelay(pdMS_TO_TICKS(REBOOT_DELAY_MS));
    esp_restart();
}

static void reboot_device(void)
{
    xTaskCreate(reboot_task, "reboot_task", REBOOT_TASK_STACK_SIZE, NULL, REBOOT_TASK_PRIORITY, NULL);
}

static bool check_req_content_len(httpd_req_t *req)
{
    if (req->content_len > REQ_RECV_BUF_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too large");
        return false;
    }
    return true;
}

static bool check_auth(httpd_req_t *req)
{
    return auth_check_request(req);
}

static esp_err_t index_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

static esp_err_t index_css_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_send(req, (const char *)index_css_start, index_css_end - index_css_start);
    return ESP_OK;
}

static esp_err_t index_js_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_send(req, (const char *)index_js_start, index_js_end - index_js_start);
    return ESP_OK;
}

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_send(req, (const char *)favicon_start, favicon_end - favicon_start);
    return ESP_OK;
}

static void resp_and_free_json(httpd_req_t *req, cJSON *req_json, cJSON *resp_json)
{
    const char *json_str = cJSON_Print(resp_json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free((void *)json_str);
    cJSON_Delete(resp_json);

    if (req_json != NULL) {
        cJSON_Delete(req_json);
    }
}

static cJSON *receive_json(httpd_req_t *req)
{
    char *buf = (char *)malloc(REQ_RECV_BUF_SIZE);
    int received = 0;

    received = httpd_req_recv(req, buf, REQ_RECV_BUF_SIZE);
    buf[received] = '\0';
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        free(buf);
        return NULL;
    }

    cJSON *req_json = cJSON_Parse(buf);
    if (req_json == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to parse JSON");
        free(buf);
        return NULL;
    }
    free(buf);

    return req_json;
}

static inline bool set_cookie_session_id(httpd_req_t *req, uint32_t session_id, char *cookie_header)
{
    snprintf(cookie_header, COOKIE_MAX_LEN, "session_id=%lu", session_id);
    ESP_LOGI(TAG, "Cookie header: %s", cookie_header);
    if (httpd_resp_set_hdr(req, "Set-Cookie", cookie_header) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set cookie header");
        return false;
    }
    return true;
}

static esp_err_t auth_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_req_content_len(req) != true) {
        return ESP_FAIL;
    }

    cJSON *req_json = receive_json(req);
    if (req_json == NULL) {
        return ESP_FAIL;
    }

    char cookie_header[COOKIE_MAX_LEN] = {0};
    char remember_cookie[REMEMBER_COOKIE_BUF_SIZE] = {0};
    cJSON *resp_json = cJSON_CreateObject();
    bool result = false;

    if (cJSON_HasObjectItem(req_json, "login") && cJSON_HasObjectItem(req_json, "pass")) {
        cJSON *login_req = cJSON_GetObjectItem(req_json, "login");
        cJSON *pass_req = cJSON_GetObjectItem(req_json, "pass");
        cJSON *remember_me = cJSON_GetObjectItem(req_json, "remember_me");

        if ((login_req->type == cJSON_String) && (pass_req->type == cJSON_String)) {
            uint32_t session_id = auth_create_session(login_req->valuestring, pass_req->valuestring);
            if (session_id != 0) {
                result = set_cookie_session_id(req, session_id, cookie_header);
                
                // Handle remember me token
                if (remember_me && cJSON_IsTrue(remember_me)) {
                    uint32_t remember_session_id = auth_create_remember_token();
                    if (remember_session_id != 0) {
                        // Get the token from the auth module
                        const char *token_str = auth_get_current_remember_token();
                        if (token_str != NULL) {
                            int written = snprintf(remember_cookie, sizeof(remember_cookie), 
                                    "remember_token=%s; Max-Age=%d; HttpOnly", 
                                    token_str, REMEMBER_TOKEN_LIFETIME_SEC);
                            if (written > 0 && written < sizeof(remember_cookie)) {
                                httpd_resp_set_hdr(req, "Set-Cookie", remember_cookie);
                                ESP_LOGI(TAG, "Remember token set in cookie");
                            } else {
                                ESP_LOGE(TAG, "Remember cookie buffer too small");
                            }
                        }
                    }
                }
            } else {
                cJSON_AddStringToObject(resp_json, "error", "Invalid login or password");
            }
        } else {
            cJSON_AddStringToObject(resp_json, "error", "Invalid login or password type");
        }
    } else {
        cJSON_AddStringToObject(resp_json, "error", "No login or password in request");
    }

    cJSON_AddBoolToObject(resp_json, "auth", result);
    resp_and_free_json(req, req_json, resp_json);

    return ESP_OK;
}

// Проверка авторизации
static esp_err_t session_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != true) {
        return ESP_OK;
    }
    // Отправить пустой ответ с кодом 200 если пользователь авторизован
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t logout_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    uint32_t session_id = auth_get_session_from_cookie(req);
    if (session_id != 0) {
        auth_remove_session(session_id);
    } else {
        ESP_LOGW(TAG, "Session ID cookie not found");
        return ESP_FAIL;
    }

    auth_clear_remember_token();

    cJSON *resp_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp_json, "success", true);
    resp_and_free_json(req, NULL, resp_json);

    return ESP_OK;
}

static esp_err_t update_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != true) {
        return ESP_OK;
    }

    char *buf = (char *)malloc(REQ_RECV_BUF_SIZE);
    esp_ota_handle_t ota_handle;
    int remaining = req->content_len;

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA Begin Error");
        free(buf);
        return ESP_FAIL;
    }

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, REQ_RECV_BUF_SIZE));

        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {  // Timeout Error: Just retry
            continue;

        } else if (recv_len <= 0) {  // Serious Error: Abort OTA
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
            free(buf);
            return ESP_FAIL;
        }

        // Successful Upload: Flash firmware chunk
        if (esp_ota_write(ota_handle, (const void *)buf, recv_len) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
            free(buf);
            return ESP_FAIL;
        }

        remaining -= recv_len;
    }
    free(buf);

    // Validate and switch to new OTA image and reboot
    if ((esp_ota_end(ota_handle) != ESP_OK) ||
        (esp_ota_set_boot_partition(ota_partition) != ESP_OK))
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation / Activation Error");
        return ESP_FAIL;
    }

    cJSON *resp_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp_json, "success", true);
    resp_and_free_json(req, NULL, resp_json);

    reboot_device();

    return ESP_OK;
}

static void add_setting_to_json(cJSON *json, const char *key, const char *json_key) {
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

static bool validate_hostname(const char *hostname) {
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

static void add_info_to_json_group(cJSON *group, const char *json_key, const void *value, size_t size) {
    if (size == sizeof(int)) {
        cJSON_AddNumberToObject(group, json_key, *(const int *)value);
    } else if (size == sizeof(bool) || size == sizeof(uint8_t)) {
        cJSON_AddBoolToObject(group, json_key, *(const bool *)value);
    } else {
        cJSON_AddStringToObject(group, json_key, (const char *)value);
    }
}

static esp_err_t info_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != true) {
        return ESP_FAIL;
    }

    cJSON *resp_json = cJSON_CreateObject();

    add_info_to_json_group(resp_json, "device_name", sys_info.device_name, sizeof(sys_info.device_name));
    add_info_to_json_group(resp_json, "firmware", FIRMWARE_VERSION, sizeof(FIRMWARE_VERSION));
    add_info_to_json_group(resp_json, "hardware", sys_info.hardware_ver, sizeof(sys_info.hardware_ver));
    add_info_to_json_group(resp_json, "serial_num", &sys_info.device_serial_num, sizeof(sys_info.device_serial_num));

    // Ethernet group
    cJSON *ethernet = cJSON_CreateObject();
    add_info_to_json_group(ethernet, "con_eth", &sys_info.eth_is_connected, sizeof(sys_info.eth_is_connected));
    add_info_to_json_group(ethernet, "ip", sys_info.eth_ip, sizeof(sys_info.eth_ip));
    add_info_to_json_group(ethernet, "mask", sys_info.eth_mask, sizeof(sys_info.eth_mask));
    add_info_to_json_group(ethernet, "gw", sys_info.eth_gw, sizeof(sys_info.eth_gw));
    add_info_to_json_group(ethernet, "mac", sys_info.eth_mac, sizeof(sys_info.eth_mac));
    cJSON_AddItemToObject(resp_json, "ethernet", ethernet);

    // WiFi group
    cJSON *wifi = cJSON_CreateObject();
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
    cJSON_AddItemToObject(resp_json, "wifi", wifi);

    // RS485_1 status
    cJSON *rs485_1 = cJSON_CreateObject();
    cJSON_AddBoolToObject(rs485_1, "is_busy", sys_info.rs485_1_is_busy);
    cJSON_AddNumberToObject(rs485_1, "error_percentage", sys_info.rs485_1_error_percentage);
    cJSON_AddNumberToObject(rs485_1, "server_connections_count", tcp_server_active_connections(TCP_SERVER_1));
    cJSON_AddItemToObject(resp_json, "rs485_1", rs485_1);

    // RS485_2 status
    cJSON *rs485_2 = cJSON_CreateObject();
    cJSON_AddBoolToObject(rs485_2, "is_busy", sys_info.rs485_2_is_busy);
    cJSON_AddNumberToObject(rs485_2, "error_percentage", sys_info.rs485_2_error_percentage);
    cJSON_AddNumberToObject(rs485_2, "server_connections_count", tcp_server_active_connections(TCP_SERVER_2));
    cJSON_AddItemToObject(resp_json, "rs485_2", rs485_2);

    resp_and_free_json(req, NULL, resp_json);

    return ESP_OK;
}

static inline esp_err_t update_info_from_json(cJSON *req_json, const char *key, void *dest, int type)
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

static esp_err_t info_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != true) {
        return ESP_FAIL;
    }

    cJSON *req_json = receive_json(req);
    if (req_json == NULL) {
        return ESP_FAIL;
    }

    if (update_info_from_json(req_json, "device_name", sys_info.device_name, cJSON_String) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to update device_name");
    }
    if (update_info_from_json(req_json, "hardware", sys_info.hardware_ver, cJSON_String) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to update hardware");
    }
    if (update_info_from_json(req_json, "serial_num", &sys_info.device_serial_num, cJSON_Number) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to update serial_num");
    }

    cJSON_Delete(req_json);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != true) {
        return ESP_OK;
    }

    cJSON *resp_json = cJSON_CreateObject();

    // Top-level
    const char *top_keys[] = {"hostname", "login", "web_port", "io_bus", "vout"};
    for (int i = 0; i < 5; ++i) {
        add_setting_to_json(resp_json, top_keys[i], top_keys[i]);
    }

    // WiFi
    cJSON *wifi = cJSON_CreateObject();
    const char *wifi_keys[] = {"wifi_mode", "ap_auth", "sta_auth", "ap_ip_static", "ap_mask_static", "ap_gw_static", "ap_ssid", "ap_pass", "sta_ssid", "sta_pass"};
    const char *wifi_json_keys[] = {"mode", "ap_auth", "sta_auth", "ap_ip_static", "ap_mask_static", "ap_gw_static", "ap_ssid", "ap_pass", "sta_ssid", "sta_pass"};
    for (int i = 0; i < 10; ++i) {
        add_setting_to_json(wifi, wifi_keys[i], wifi_json_keys[i]);
    }
    cJSON_AddItemToObject(resp_json, "wifi", wifi);

    // Ethernet
    cJSON *ethernet = cJSON_CreateObject();
    const char *eth_keys[] = {"eth_ip_static", "eth_mask_static", "eth_gw_static", "eth_dhcpc"};
    const char *eth_json_keys[] = {"ip_static", "mask_static", "gw_static", "dhcpc"};
    for (int i = 0; i < 4; ++i) {
        add_setting_to_json(ethernet, eth_keys[i], eth_json_keys[i]);
    }
    cJSON_AddItemToObject(resp_json, "ethernet", ethernet);

    // RS485_1
    cJSON *rs485_1 = cJSON_CreateObject();
    const char *rs485_fields[] = {"term", "fail_safe", "baudrate", "stopbits", "parity", "databits"};
    char key_buf[32];
    for (int i = 0; i < 6; ++i) {
        snprintf(key_buf, sizeof(key_buf), "%s_1", rs485_fields[i]);
        add_setting_to_json(rs485_1, key_buf, rs485_fields[i]);
    }
    
    // Bridge subgroup for RS485_1
    cJSON *bridge_1 = cJSON_CreateObject();
    const char *bridge_fields[] = {"bridge_mode", "bridge_port", "bridge_ip", "bridge_mb"};
    const char *bridge_json_keys[] = {"mode", "port", "ip", "modbus"};
    for (int i = 0; i < 4; ++i) {
        snprintf(key_buf, sizeof(key_buf), "%s_1", bridge_fields[i]);
        add_setting_to_json(bridge_1, key_buf, bridge_json_keys[i]);
    }
    cJSON_AddItemToObject(rs485_1, "bridge", bridge_1);
    cJSON_AddItemToObject(resp_json, "rs485_1", rs485_1);

    // RS485_2
    cJSON *rs485_2 = cJSON_CreateObject();
    for (int i = 0; i < 6; ++i) {
        snprintf(key_buf, sizeof(key_buf), "%s_2", rs485_fields[i]);
        add_setting_to_json(rs485_2, key_buf, rs485_fields[i]);
    }
    
    // Bridge subgroup for RS485_2
    cJSON *bridge_2 = cJSON_CreateObject();
    for (int i = 0; i < 4; ++i) {
        snprintf(key_buf, sizeof(key_buf), "%s_2", bridge_fields[i]);
        add_setting_to_json(bridge_2, key_buf, bridge_json_keys[i]);
    }
    cJSON_AddItemToObject(rs485_2, "bridge", bridge_2);
    cJSON_AddItemToObject(resp_json, "rs485_2", rs485_2);

    resp_and_free_json(req, NULL, resp_json);
    return ESP_OK;
}

static inline esp_err_t process_json_item(httpd_req_t *req, cJSON *item, const char *key, cJSON *resp_json)
{
    void *value = NULL;
    static const int false_val = 0;
    static const int true_val = 1;

    switch (item->type) {
        case cJSON_String:
            value = (char *)item->valuestring;
            break;
        case cJSON_Number:
            value = (int *)&item->valueint;
            break;
        case cJSON_False:
            value = (void *)&false_val;
            break;
        case cJSON_True:
            value = (void *)&true_val;
            break;
        default:
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown type json item");
            return ESP_FAIL;
    }

    if (setting_items_save(key, value) == 0) {
        ESP_LOGI(TAG, "[%s] saved", key);
        cJSON_AddBoolToObject(resp_json, key, true);
    } else {
        ESP_LOGW(TAG, "[%s] failed to save", key);
        cJSON_AddBoolToObject(resp_json, key, false);
    }

    return ESP_OK;
}

static esp_err_t settings_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_req_content_len(req) != true) {
        return ESP_FAIL;
    }

    if (check_auth(req) != true) {
        return ESP_OK;
    }

    cJSON *req_json = receive_json(req);
    if (req_json == NULL) {
        return ESP_FAIL;
    }

    cJSON *resp_json = cJSON_CreateObject();

    // Top-level
    if (cJSON_HasObjectItem(req_json, "hostname")) {
        cJSON *item = cJSON_GetObjectItem(req_json, "hostname");
        if (cJSON_IsString(item)) {
            const char *hostname = item->valuestring;
            
            if (validate_hostname(hostname)) {
                setting_items_save("hostname", (void *)hostname);
                cJSON_AddBoolToObject(resp_json, "hostname", true);
            } else {
                cJSON_AddBoolToObject(resp_json, "hostname", false);
                ESP_LOGW(TAG, "Invalid hostname: %s", hostname);
            }
        } else {
            cJSON_AddBoolToObject(resp_json, "hostname", false);
            ESP_LOGW(TAG, "hostname must be a string");
        }
    }
    if (cJSON_HasObjectItem(req_json, "login")) {
        cJSON *item = cJSON_GetObjectItem(req_json, "login");
        setting_items_save("login", (void *)item->valuestring);
        cJSON_AddBoolToObject(resp_json, "login", true);
    }
    if (cJSON_HasObjectItem(req_json, "web_port")) {
        cJSON *item = cJSON_GetObjectItem(req_json, "web_port");
        if (cJSON_IsNumber(item)) {
            int val = item->valueint;
            if (val >= 1 && val <= 65535) {
                setting_items_save("web_port", &val);
                cJSON_AddBoolToObject(resp_json, "web_port", true);
            } else {
                cJSON_AddBoolToObject(resp_json, "web_port", false);
                ESP_LOGW(TAG, "Invalid web_port: %d (must be 1-65535)", val);
            }
        } else {
            cJSON_AddBoolToObject(resp_json, "web_port", false);
            ESP_LOGW(TAG, "web_port must be a number");
        }
    }
    if (cJSON_HasObjectItem(req_json, "io_bus")) {
        cJSON *item = cJSON_GetObjectItem(req_json, "io_bus");
        bool val = cJSON_IsTrue(item);
        setting_items_save("io_bus", &val);
        cJSON_AddBoolToObject(resp_json, "io_bus", true);
    }
    if (cJSON_HasObjectItem(req_json, "vout")) {
        cJSON *item = cJSON_GetObjectItem(req_json, "vout");
        bool val = cJSON_IsTrue(item);
        setting_items_save("vout", &val);
        cJSON_AddBoolToObject(resp_json, "vout", true);
    }

    // WiFi
    if (cJSON_HasObjectItem(req_json, "wifi")) {
        cJSON *wifi = cJSON_GetObjectItem(req_json, "wifi");
        if (cJSON_HasObjectItem(wifi, "mode")) setting_items_save("wifi_mode", (void *)cJSON_GetObjectItem(wifi, "mode")->valuestring);
        if (cJSON_HasObjectItem(wifi, "ap_auth")) setting_items_save("ap_auth", (void *)cJSON_GetObjectItem(wifi, "ap_auth")->valuestring);
        if (cJSON_HasObjectItem(wifi, "sta_auth")) setting_items_save("sta_auth", (void *)cJSON_GetObjectItem(wifi, "sta_auth")->valuestring);
        if (cJSON_HasObjectItem(wifi, "ap_ip_static")) setting_items_save("ap_ip_static", (void *)cJSON_GetObjectItem(wifi, "ap_ip_static")->valuestring);
        if (cJSON_HasObjectItem(wifi, "ap_mask_static")) setting_items_save("ap_mask_static", (void *)cJSON_GetObjectItem(wifi, "ap_mask_static")->valuestring);
        if (cJSON_HasObjectItem(wifi, "ap_gw_static")) setting_items_save("ap_gw_static", (void *)cJSON_GetObjectItem(wifi, "ap_gw_static")->valuestring);
        if (cJSON_HasObjectItem(wifi, "ap_ssid")) setting_items_save("ap_ssid", (void *)cJSON_GetObjectItem(wifi, "ap_ssid")->valuestring);
        if (cJSON_HasObjectItem(wifi, "ap_pass")) setting_items_save("ap_pass", (void *)cJSON_GetObjectItem(wifi, "ap_pass")->valuestring);
        if (cJSON_HasObjectItem(wifi, "sta_ssid")) setting_items_save("sta_ssid", (void *)cJSON_GetObjectItem(wifi, "sta_ssid")->valuestring);
        if (cJSON_HasObjectItem(wifi, "sta_pass")) setting_items_save("sta_pass", (void *)cJSON_GetObjectItem(wifi, "sta_pass")->valuestring);
    }

    // Ethernet
    if (cJSON_HasObjectItem(req_json, "ethernet")) {
        cJSON *ethernet = cJSON_GetObjectItem(req_json, "ethernet");
        if (cJSON_HasObjectItem(ethernet, "ip_static")) setting_items_save("eth_ip_static", (void *)cJSON_GetObjectItem(ethernet, "ip_static")->valuestring);
        if (cJSON_HasObjectItem(ethernet, "mask_static")) setting_items_save("eth_mask_static", (void *)cJSON_GetObjectItem(ethernet, "mask_static")->valuestring);
        if (cJSON_HasObjectItem(ethernet, "gw_static")) setting_items_save("eth_gw_static", (void *)cJSON_GetObjectItem(ethernet, "gw_static")->valuestring);
        if (cJSON_HasObjectItem(ethernet, "dhcpc")) {
            bool val = cJSON_IsTrue(cJSON_GetObjectItem(ethernet, "dhcpc"));
            setting_items_save("eth_dhcpc", &val);
        }
    }

    // RS485_1 and RS485_2
    const char *rs485_fields[] = {"term", "fail_safe", "baudrate", "stopbits", "parity", "databits"};
    const char *rs485_json_names[] = {"rs485_1", "rs485_2"};
    const char *rs485_suffix[] = {"_1", "_2"};
    for (int port = 0; port < 2; ++port) {
        if (cJSON_HasObjectItem(req_json, rs485_json_names[port])) {
            cJSON *rs485 = cJSON_GetObjectItem(req_json, rs485_json_names[port]);
            char key_buf[32];
            
            // Handle regular RS485 fields
            for (int i = 0; i < 6; ++i) {
                if (cJSON_HasObjectItem(rs485, rs485_fields[i])) {
                    cJSON *item = cJSON_GetObjectItem(rs485, rs485_fields[i]);
                    snprintf(key_buf, sizeof(key_buf), "%s%s", rs485_fields[i], rs485_suffix[port]);
                    setting_item_type_t type = setting_items_get_type_in_json(key_buf);
                    if (type == SETTING_ITEM_TYPE_STR && cJSON_IsString(item)) {
                        setting_items_save(key_buf, (void *)item->valuestring);
                    } else if (type == SETTING_ITEM_TYPE_NUM && cJSON_IsNumber(item)) {
                        int v = item->valueint;
                        setting_items_save(key_buf, &v);
                    } else if (type == SETTING_ITEM_TYPE_BOOL) {
                        bool v = cJSON_IsTrue(item);
                        setting_items_save(key_buf, &v);
                    }
                }
            }
            
            // Handle bridge subgroup
            if (cJSON_HasObjectItem(rs485, "bridge")) {
                cJSON *bridge = cJSON_GetObjectItem(rs485, "bridge");
                const char *bridge_json_keys[] = {"mode", "port", "ip", "modbus"};
                const char *bridge_setting_keys[] = {"bridge_mode", "bridge_port", "bridge_ip", "bridge_mb"};
                
                for (int i = 0; i < 4; ++i) {
                    if (cJSON_HasObjectItem(bridge, bridge_json_keys[i])) {
                        cJSON *item = cJSON_GetObjectItem(bridge, bridge_json_keys[i]);
                        snprintf(key_buf, sizeof(key_buf), "%s%s", bridge_setting_keys[i], rs485_suffix[port]);
                        setting_item_type_t type = setting_items_get_type_in_json(key_buf);
                        if (type == SETTING_ITEM_TYPE_STR && cJSON_IsString(item)) {
                            setting_items_save(key_buf, (void *)item->valuestring);
                        } else if (type == SETTING_ITEM_TYPE_NUM && cJSON_IsNumber(item)) {
                            int v = item->valueint;
                            setting_items_save(key_buf, &v);
                        } else if (type == SETTING_ITEM_TYPE_BOOL) {
                            bool v = cJSON_IsTrue(item);
                            setting_items_save(key_buf, &v);
                        }
                    }
                }
            }
        }
    }

    update_rs485_control();
    update_io_bus_control();
    // TODO: обновить настройки Wi-Fi и Ethernet без перезагрузки устройства

    cJSON_AddBoolToObject(resp_json, "success", true);
    resp_and_free_json(req, req_json, resp_json);
    return ESP_OK;
}

static int get_cmd_code(const char *cmd_str)
{
    for (int i = 0; i < ARRAY_SIZE(cmds); i++) {
        if (cmds[i].cmd_name == NULL) {
            break;
        }
        if (strncmp(cmd_str, cmds[i].cmd_name, CMD_NAME_MAX_LEN) == 0) {
            return cmds[i].cmd_code;
        }
    }
    return -1;
}

static esp_err_t execute_cmd(int cmd_code)
{
    ESP_LOGI(TAG, "%s", __func__);

    esp_err_t err = ESP_OK;

    switch (cmd_code) {
        case CMD_REBOOT:
            reboot_device();
            break;
        case CMD_SET_DEFAULT_SETTINGS:
            if (setting_items_set_defaults() != 0) {
                err = ESP_FAIL;
            }
            break;
        case CMD_WRITE_FACTORY_DATA:
            if (sys_info_write_factory_data() != ESP_OK) {
                err = ESP_FAIL;
            }
            break;
        default:
            ESP_LOGW(TAG, "Unknown command code: %d", cmd_code);
            break;
    }
    return err;
}

static esp_err_t cmd_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != true) {
        return ESP_OK;
    }

    cJSON *req_json = receive_json(req);
    if (req_json == NULL) {
        return ESP_FAIL;
    }

    if (cJSON_HasObjectItem(req_json, "cmd") != true) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No command in request");
        cJSON_Delete(req_json);
        return ESP_FAIL;
    }

    cJSON *cmd = cJSON_GetObjectItem(req_json, "cmd");
    char cmd_str[CMD_NAME_MAX_LEN] = {0};
    if (cmd->type == cJSON_String) {
        strncpy(cmd_str, cmd->valuestring, CMD_NAME_MAX_LEN);
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown command type");
        cJSON_Delete(req_json);
        return ESP_FAIL;
    }
    cJSON_Delete(req_json);

    int cmd_code = get_cmd_code(cmd_str);
    if (cmd_code == -1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown command");
        return ESP_FAIL;
    }

    if (execute_cmd(cmd_code) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to execute command");
        return ESP_FAIL;
    }
    
    cJSON *resp_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp_json, "success", true);
    resp_and_free_json(req, NULL, resp_json);

    return ESP_OK;
}

// WiFi scan state
static struct {
    bool scan_in_progress;
    bool scan_completed;
    wifi_ap_record_t ap_records[20];
    uint16_t ap_count;
    esp_err_t last_scan_result;
} wifi_scan_state = {
    .scan_in_progress = false,
    .scan_completed = false,
    .ap_count = 0,
    .last_scan_result = ESP_OK
};

static esp_err_t wifi_scan_start_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != true) {
        return ESP_OK;
    }

    cJSON *resp_json = cJSON_CreateObject();

    // Check if scan is already in progress
    if (wifi_scan_state.scan_in_progress) {
        cJSON_AddBoolToObject(resp_json, "success", false);
        cJSON_AddStringToObject(resp_json, "error", "Scan already in progress");
        resp_and_free_json(req, NULL, resp_json);
        return ESP_OK;
    }

    // Reset scan state
    wifi_scan_state.scan_in_progress = true;
    wifi_scan_state.scan_completed = false;
    wifi_scan_state.ap_count = 0;

    // Start WiFi scan (non-blocking)
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 300
            }
        }
    };

    esp_err_t scan_result = esp_wifi_scan_start(&scan_config, false); // false = non-blocking
    if (scan_result == ESP_OK) {
        cJSON_AddBoolToObject(resp_json, "success", true);
        cJSON_AddStringToObject(resp_json, "message", "Scan started");
    } else {
        wifi_scan_state.scan_in_progress = false;
        wifi_scan_state.last_scan_result = scan_result;
        cJSON_AddBoolToObject(resp_json, "success", false);
        cJSON_AddStringToObject(resp_json, "error", "Failed to start scan");
        ESP_LOGW(TAG, "WiFi scan failed to start with error: 0x%x", scan_result);
    }

    resp_and_free_json(req, NULL, resp_json);
    return ESP_OK;
}

static esp_err_t wifi_scan_results_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != true) {
        return ESP_OK;
    }

    cJSON *resp_json = cJSON_CreateObject();

    // Check scan status
    if (wifi_scan_state.scan_in_progress && !wifi_scan_state.scan_completed) {
        // Try to get results to see if scan completed
        uint16_t ap_count = 20;
        esp_err_t result = esp_wifi_scan_get_ap_records(&ap_count, wifi_scan_state.ap_records);
        
        if (result == ESP_OK) {
            wifi_scan_state.scan_completed = true;
            wifi_scan_state.scan_in_progress = false;
            wifi_scan_state.ap_count = ap_count;
            wifi_scan_state.last_scan_result = ESP_OK;
        } else if (result == ESP_ERR_WIFI_NOT_STARTED) {
            wifi_scan_state.scan_in_progress = false;
            wifi_scan_state.last_scan_result = result;
        }
    }

    // Return status and results
    cJSON_AddBoolToObject(resp_json, "scan_in_progress", wifi_scan_state.scan_in_progress);
    cJSON_AddBoolToObject(resp_json, "scan_completed", wifi_scan_state.scan_completed);

    if (wifi_scan_state.scan_completed) {
        cJSON *networks = cJSON_CreateArray();
        for (int i = 0; i < wifi_scan_state.ap_count; i++) {
            cJSON *ap_json = cJSON_CreateObject();
            cJSON_AddStringToObject(ap_json, "ssid", (const char *)wifi_scan_state.ap_records[i].ssid);
            cJSON_AddNumberToObject(ap_json, "rssi", wifi_scan_state.ap_records[i].rssi);
            
            char bssid_str[18];
            snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     wifi_scan_state.ap_records[i].bssid[0], wifi_scan_state.ap_records[i].bssid[1], 
                     wifi_scan_state.ap_records[i].bssid[2], wifi_scan_state.ap_records[i].bssid[3], 
                     wifi_scan_state.ap_records[i].bssid[4], wifi_scan_state.ap_records[i].bssid[5]);
            cJSON_AddStringToObject(ap_json, "bssid", bssid_str);
            cJSON_AddNumberToObject(ap_json, "channel", wifi_scan_state.ap_records[i].primary);

            cJSON_AddItemToArray(networks, ap_json);
        }
        cJSON_AddItemToObject(resp_json, "networks", networks);
    } else if (!wifi_scan_state.scan_in_progress && wifi_scan_state.last_scan_result != ESP_OK) {
        cJSON_AddStringToObject(resp_json, "error", "Scan failed");
    }

    resp_and_free_json(req, NULL, resp_json);
    return ESP_OK;
}

static esp_err_t ap_clients_get_handler(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_FAIL;
    }

    wifi_sta_list_t sta_list;
    cJSON *sta_array = cJSON_CreateArray();

    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        esp_netif_pair_mac_ip_t mac_ip_pairs[sta_list.num];
        memset(mac_ip_pairs, 0, sizeof(mac_ip_pairs));
        for (int i = 0; i < sta_list.num; ++i) {
            memcpy(mac_ip_pairs[i].mac, sta_list.sta[i].mac, 6);
        }
        bool got_ips = ap_netif && esp_netif_dhcps_get_clients_by_mac(ap_netif, sta_list.num, mac_ip_pairs) == ESP_OK;

        for (int i = 0; i < sta_list.num; ++i) {
            wifi_sta_info_t *sta = &sta_list.sta[i];
            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     sta->mac[0], sta->mac[1], sta->mac[2],
                     sta->mac[3], sta->mac[4], sta->mac[5]);
            cJSON *client = cJSON_CreateObject();
            cJSON_AddStringToObject(client, "mac", mac_str);
            cJSON_AddNumberToObject(client, "rssi", sta->rssi);

            char ip_str[16] = "0.0.0.0";
            if (got_ips) {
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&mac_ip_pairs[i].ip));
            }
            cJSON_AddStringToObject(client, "ip", ip_str);

            cJSON_AddItemToArray(sta_array, client);
        }
    }

    char *json_str = cJSON_Print(sta_array);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(sta_array);

    return ESP_OK;
}

static esp_err_t uptime_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != true) {
        return ESP_FAIL;
    }

    uint32_t uptime = esp_timer_get_time() / 1000000;  // Convert microseconds to seconds
    int days = uptime / (24 * 3600);
    uptime %= (24 * 3600);
    int hours = uptime / 3600;
    uptime %= 3600;
    int minutes = uptime / 60;
    int seconds = uptime % 60;

    cJSON *uptime_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(uptime_obj, "days", days);
    cJSON_AddNumberToObject(uptime_obj, "hours", hours);
    cJSON_AddNumberToObject(uptime_obj, "minutes", minutes);
    cJSON_AddNumberToObject(uptime_obj, "seconds", seconds);

    char *json_str = cJSON_Print(uptime_obj);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(uptime_obj);

    return ESP_OK;
}

static const httpd_uri_t auth_post = {
    .uri = "/auth",
    .method = HTTP_POST,
    .handler = auth_post_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t session_get = {
    .uri = "/session",
    .method = HTTP_GET,
    .handler = session_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t logout_post = {
    .uri = "/logout",
    .method = HTTP_POST,
    .handler = logout_post_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t index_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_html_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t favicon_get = {
    .uri = "/favicon.webp",
    .method = HTTP_GET,
    .handler = favicon_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t index_css_get = {
    .uri = "/index.css",
    .method = HTTP_GET,
    .handler = index_css_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t index_js_get = {
    .uri = "/index.js",
    .method = HTTP_GET,
    .handler = index_js_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t update_post = {
    .uri = "/update",
    .method = HTTP_POST,
    .handler = update_post_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t info_get = {
    .uri = "/info",
    .method = HTTP_GET,
    .handler = info_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t info_post = {
    .uri = "/info",
    .method = HTTP_POST,
    .handler = info_post_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t settings_get = {
    .uri = "/settings",
    .method = HTTP_GET,
    .handler = settings_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t settings_post = {
    .uri = "/settings",
    .method = HTTP_POST,
    .handler = settings_post_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t cmd_post = {
    .uri = "/cmd",
    .method = HTTP_POST,
    .handler = cmd_post_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t wifi_scan_start_post = {
    .uri = "/wifi_scan/start",
    .method = HTTP_POST,
    .handler = wifi_scan_start_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t wifi_scan_results_get = {
    .uri = "/wifi_scan/results",
    .method = HTTP_GET,
    .handler = wifi_scan_results_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t ap_clients_get = {
    .uri = "/ap_clients",
    .method = HTTP_GET,
    .handler = ap_clients_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t uptime_get = {
    .uri = "/uptime",
    .method = HTTP_GET,
    .handler = uptime_get_handler,
    .user_ctx = NULL,
};

esp_err_t http_server_init(ssdp_config_t *ssdp_config)
{
    static httpd_handle_t http_server = NULL;
    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
    httpd_config.max_uri_handlers = 30;  // TODO: Подобрать значение к релизу
    httpd_config.stack_size = 1024 * 6;  // TODO: Проверить размер используемой памяти

    if (setting_items_read_raw("web_port", &httpd_config.server_port, SETTING_ITEM_TYPE_NUM) != 0) {
        ESP_LOGE(TAG, "Failed to read web_port from settings");
        return ESP_FAIL;
    }

    // Initialize authentication module
    if (auth_session_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize authentication module");
        return ESP_FAIL;
    }

    if (httpd_start(&http_server, &httpd_config) == ESP_OK) {
        httpd_register_uri_handler(http_server, &auth_post);
        httpd_register_uri_handler(http_server, &session_get);
        httpd_register_uri_handler(http_server, &logout_post);

        // files
        httpd_register_uri_handler(http_server, &index_get);
        httpd_register_uri_handler(http_server, &index_css_get);
        httpd_register_uri_handler(http_server, &index_js_get);
        httpd_register_uri_handler(http_server, &favicon_get);

        httpd_register_uri_handler(http_server, &update_post);
        httpd_register_uri_handler(http_server, &info_get);
        httpd_register_uri_handler(http_server, &info_post);
        httpd_register_uri_handler(http_server, &settings_get);
        httpd_register_uri_handler(http_server, &settings_post);
        httpd_register_uri_handler(http_server, &cmd_post);
        httpd_register_uri_handler(http_server, &wifi_scan_start_post);
        httpd_register_uri_handler(http_server, &wifi_scan_results_get);
        httpd_register_uri_handler(http_server, &ap_clients_get);
        httpd_register_uri_handler(http_server, &uptime_get);
    }

    if (http_server == NULL) {
        return ESP_FAIL;
    }

    return ESP_OK;
}
