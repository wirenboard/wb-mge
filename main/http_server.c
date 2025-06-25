#include "http_server.h"

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
#include "rs485_control.h"
#include "bridge.h"
#include "esp_netif.h"

// Размер буфера выбран таким образом, чтобы он был больше, чем размер заголовка HTTP
#define REQ_RECV_BUF_SIZE       (CONFIG_HTTPD_MAX_REQ_HDR_LEN * 2)
// Длина строки с максимальным числом uint32 (10 цифр + 1 символ для '\0')
#define UINT32_STR_MAX_LEN      11
// Максимальная длина cookie с идентификатором сессии (session_id=<u32_id>)
#define COOKIE_MAX_LEN          (11 + UINT32_STR_MAX_LEN)
// При превышении этого количества сессий, самая старая сессия будет удалена
#define MAX_SESSIONS            10

#define CMD_NAME_MAX_LEN        32
#define REBOOT_DELAY_MS         1000
#define REBOOT_TASK_STACK_SIZE  2048
#define REBOOT_TASK_PRIORITY    5

typedef struct {
    uint32_t session_ids[MAX_SESSIONS];
    int current_index;
} session_buffer_t;

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

static session_buffer_t session_buffer = {
    .current_index = 0,
};

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

static void add_session_id(session_buffer_t *buffer, uint32_t session_id)
{
    ESP_LOGI(TAG, "%s", __func__);

    buffer->session_ids[buffer->current_index] = session_id;
    buffer->current_index = (buffer->current_index + 1) % MAX_SESSIONS;
}

uint32_t strtou(const char *u32_str)
{
    if (u32_str == NULL) {
        ESP_LOGE(TAG, "%s: String is NULL", __func__);
        return 0;
    }
    if (strnlen(u32_str, (UINT32_STR_MAX_LEN + 1)) > UINT32_STR_MAX_LEN) {
        ESP_LOGE(TAG, "%s: String is too long", __func__);
        return 0;
    }

    char *endptr;
    errno = 0;  // Сбросить errno перед вызовом strtoul
    uint64_t value = strtoul(u32_str, &endptr, 10);

    if ((errno == ERANGE) || (value > UINT32_MAX)) {
        ESP_LOGE(TAG, "%s: Overflow occurred", __func__);
        return 0;
    }
    if (endptr == u32_str) {
        ESP_LOGE(TAG, "%s: No digits were found", __func__);
        return 0;
    }
    if (*endptr != '\0') {
        ESP_LOGE(TAG, "%s: Further characters after number: %s", __func__, endptr);
        return 0;
    }

    return (uint32_t)value;
}

static uint32_t get_session_id_from_cookie(httpd_req_t *req)
{
    char session_id_str[COOKIE_MAX_LEN];
    size_t buf_len = sizeof(session_id_str);
    if (httpd_req_get_cookie_val(req, "session_id", session_id_str, &buf_len) == ESP_OK) {
        ESP_LOGI(TAG, "%s: %s", __func__, session_id_str);
        return strtou(session_id_str);
    }
    return 0;  // Возвращает 0, если не удалось получить session_id
}

static uint32_t authorization(char *login_req, char *pass_req)
{
    ESP_LOGI(TAG, "%s", __func__);

    char login[SETTING_ITEM_MAX_STR_LEN] = {0};
    char pass[SETTING_ITEM_MAX_STR_LEN] = {0};

    if ((setting_items_read_raw("login", login, SETTING_ITEM_TYPE_STR) != 0) ||
        (setting_items_read_raw("pass", pass, SETTING_ITEM_TYPE_STR) != 0))
    {
        ESP_LOGE(TAG, "Failed to read login or pass from storage");
        return 0;
    }
    if ((strncmp(login_req, login, SETTING_ITEM_MAX_STR_LEN) != 0) ||
        (strncmp(pass_req, pass, SETTING_ITEM_MAX_STR_LEN) != 0))
    {
        ESP_LOGW(TAG, "Invalid login or password");
        return 0;
    }

    uint32_t session_id = esp_random();
    if (session_id == 0) {
        session_id = esp_random();  // Повторная попытка генерации session_id
    }
    add_session_id(&session_buffer, session_id);

    return session_id;
}

static inline bool session_id_is_valid(uint32_t session_id)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (session_buffer.session_ids[i] == session_id) {
            return true;
        }
    }
    return false;
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
    ESP_LOGI(TAG, "%s", __func__);

    uint32_t session_id = get_session_id_from_cookie(req);
    if (session_id != 0) {
        if (session_id_is_valid(session_id)) {
            return true;
        } else {
            ESP_LOGW(TAG, "Session ID %lu is not valid", session_id);
        }
    } else {
        ESP_LOGW(TAG, "Session ID cookie not found");
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, NULL, 0);

    return false;
}

static inline void find_and_remove_session_id(uint32_t session_id)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (session_buffer.session_ids[i] == session_id) {
            session_buffer.session_ids[i] = 0;
            ESP_LOGI(TAG, "Session ID %lu removed", session_id);
            break;
        }
    }
}

static esp_err_t logout(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    uint32_t session_id = get_session_id_from_cookie(req);
    if (session_id != 0) {
        find_and_remove_session_id(session_id);
    } else {
        ESP_LOGW(TAG, "Session ID cookie not found");
        return ESP_FAIL;
    }

    return ESP_OK;
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
    cJSON *resp_json = cJSON_CreateObject();
    bool result = false;

    if (cJSON_HasObjectItem(req_json, "login") && cJSON_HasObjectItem(req_json, "pass")) {
        cJSON *login_req = cJSON_GetObjectItem(req_json, "login");
        cJSON *pass_req = cJSON_GetObjectItem(req_json, "pass");

        if ((login_req->type == cJSON_String) && (pass_req->type == cJSON_String)) {
            uint32_t session_id = authorization(login_req->valuestring, pass_req->valuestring);
            if (session_id != 0) {
                result = set_cookie_session_id(req, session_id, cookie_header);
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

    if (logout(req) != ESP_OK) {
        return ESP_FAIL;
    }

    cJSON *resp_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp_json, "logout", true);
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
    cJSON_AddBoolToObject(resp_json, "update", true);
    resp_and_free_json(req, NULL, resp_json);

    reboot_device();

    return ESP_OK;
}

static inline void add_setting_item_to_json(cJSON *json, const char *key)
{
    setting_item_type_t type = setting_items_get_type_in_json(key);

    switch (type) {
        case SETTING_ITEM_TYPE_NUM: {
            uint32_t value = 0;
            if (setting_items_read(key, &value) == 0) {
                cJSON_AddNumberToObject(json, key, value);
            }
            break;
        }
        case SETTING_ITEM_TYPE_STR: {
            char value[SETTING_ITEM_MAX_STR_LEN] = {0};
            if (setting_items_read(key, value) == 0) {
                cJSON_AddStringToObject(json, key, value);
            }
            break;
        }
        case SETTING_ITEM_TYPE_BOOL: {
            uint8_t value = 0;
            if (setting_items_read(key, &value) == 0) {
                cJSON_AddBoolToObject(json, key, value);
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "Unknown setting item type for key: %s", key);
            break;
    }
}

static esp_err_t info_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != true) {
        return ESP_FAIL;
    }

    cJSON *resp_json = cJSON_CreateObject();

    cJSON_AddStringToObject(resp_json, "device_name", sys_info.device_name);
    cJSON_AddStringToObject(resp_json, "firmware", FIRMWARE_VERSION);
    cJSON_AddStringToObject(resp_json, "hardware", sys_info.hardware_ver);
    cJSON_AddNumberToObject(resp_json, "serial_num", sys_info.device_serial_num);

    cJSON_AddBoolToObject(resp_json, "con_eth", sys_info.eth_is_connected);
    cJSON_AddStringToObject(resp_json, "eth_ip", sys_info.eth_ip);
    cJSON_AddStringToObject(resp_json, "eth_mask", sys_info.eth_mask);
    cJSON_AddStringToObject(resp_json, "eth_gw", sys_info.eth_gw);
    cJSON_AddStringToObject(resp_json, "eth_mac", sys_info.eth_mac);

    cJSON_AddBoolToObject(resp_json, "con_sta", sys_info.wifi_sta_is_connected);
    cJSON_AddStringToObject(resp_json, "sta_ip", sys_info.wifi_sta_ip);
    cJSON_AddStringToObject(resp_json, "sta_mask", sys_info.wifi_sta_mask);
    cJSON_AddStringToObject(resp_json, "sta_gw", sys_info.wifi_sta_gw);

    cJSON_AddNumberToObject(resp_json, "con_ap", sys_info.wifi_ap_connections_count);

    cJSON_AddNumberToObject(resp_json, "wifi_ap_channel", WIFI_CHAN_AP);
    cJSON_AddStringToObject(resp_json, "wifi_sta_mac", sys_info.wifi_sta_mac);
    cJSON_AddStringToObject(resp_json, "wifi_ap_mac", sys_info.wifi_ap_mac);

    cJSON_AddNumberToObject(resp_json, "server1_connections_count", tcp_server_active_connections(TCP_SERVER_1));
    cJSON_AddNumberToObject(resp_json, "server2_connections_count", tcp_server_active_connections(TCP_SERVER_2));

    cJSON_AddBoolToObject(resp_json, "rs485_1_is_busy", sys_info.rs485_1_is_busy);
    cJSON_AddBoolToObject(resp_json, "rs485_2_is_busy", sys_info.rs485_2_is_busy);

    // only for Modbus TCP
    cJSON_AddNumberToObject(resp_json, "rs485_1_error_percentage", sys_info.rs485_1_error_percentage);
    cJSON_AddNumberToObject(resp_json, "rs485_2_error_percentage", sys_info.rs485_2_error_percentage);

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
    cJSON_AddItemToObject(resp_json, "uptime", uptime_obj);

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

    int items_num = setting_items_get_keys(NULL);
    const char *keys[items_num];
    setting_items_get_keys(keys);

    for (int i = 0; i < items_num; i++) {
        add_setting_item_to_json(resp_json, keys[i]);
    }

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

// обновляет состояние подтяжек, питания и терминаторов RS485 в соответствии с текущими настройками
static void update_rs485_control(void)
{
    ESP_LOGI(TAG, "%s", __func__);

    bool pullup_1_enabled = false;
    bool pullup_2_enabled = false;
    bool term_1_enabled = false;
    bool term_2_enabled = false;
    bool vout_enabled = false;

    setting_items_read_raw(KEY_485_FAIL_SAFE_1, &pullup_1_enabled, SETTING_ITEM_TYPE_BOOL);
    setting_items_read_raw(KEY_485_FAIL_SAFE_2, &pullup_2_enabled, SETTING_ITEM_TYPE_BOOL);
    setting_items_read_raw(KEY_485_TERM_1, &term_1_enabled, SETTING_ITEM_TYPE_BOOL);
    setting_items_read_raw(KEY_485_TERM_2, &term_2_enabled, SETTING_ITEM_TYPE_BOOL);
    setting_items_read_raw(KEY_485_VOUT, &vout_enabled, SETTING_ITEM_TYPE_BOOL);

    rs485_pupd_on_off(RS485_1, pullup_1_enabled);
    rs485_pupd_on_off(RS485_2, pullup_2_enabled);
    rs485_term_on_off(RS485_1, term_1_enabled);
    rs485_term_on_off(RS485_2, term_2_enabled);
    rs485_bus_vout_on_off(vout_enabled);

    ESP_LOGI(TAG, "RS485 control updated");
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

    int items_num = setting_items_get_keys(NULL);
    const char *keys[items_num];
    setting_items_get_keys(keys);

    for (int i = 0; i < items_num; i++) {
        if (cJSON_HasObjectItem(req_json, keys[i])) {
            cJSON *item = cJSON_GetObjectItem(req_json, keys[i]);
            if (process_json_item(req, item, keys[i], resp_json) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to process item: %s", keys[i]);
            }
        }
    }

    update_rs485_control();

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
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t wifi_scan_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != true) {
        return ESP_OK;
    }

    cJSON *resp_json = cJSON_CreateArray();
    const int config_wifi_scan_max_ap = 20;
    wifi_ap_record_t ap_records[config_wifi_scan_max_ap];
    uint16_t ap_count = 0;

    if (esp_wifi_scan_get_ap_records(&ap_count, ap_records) == ESP_OK) {
        for (int i = 0; i < ap_count; i++) {
            cJSON *ap_json = cJSON_CreateObject();
            cJSON_AddStringToObject(ap_json, "ssid", (const char *)ap_records[i].ssid);
            cJSON_AddNumberToObject(ap_json, "rssi", ap_records[i].rssi);
            cJSON_AddStringToObject(ap_json, "bssid", (const char *)ap_records[i].bssid);
            cJSON_AddNumberToObject(ap_json, "channel", ap_records[i].primary);

            cJSON_AddItemToArray(resp_json, ap_json);
        }
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get Wi-Fi scan results");
        cJSON_Delete(resp_json);
        return ESP_FAIL;
    }

    // Send JSON response
    char *json_str = cJSON_Print(resp_json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(resp_json);

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
static const httpd_uri_t wifi_scan_get = {
    .uri = "/wifi_scan",
    .method = HTTP_GET,
    .handler = wifi_scan_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t ap_clients_get = {
    .uri = "/ap_clients",
    .method = HTTP_GET,
    .handler = ap_clients_get_handler,
    .user_ctx = NULL,
};

esp_err_t http_server_init(ssdp_config_t *ssdp_config)
{
    static httpd_handle_t http_server = NULL;
    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
    httpd_config.max_uri_handlers = 30;  // TODO: Подобрать значение к релизу
    httpd_config.stack_size = 1024 * 6;  // TODO: Проверить размер используемой памяти

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
        httpd_register_uri_handler(http_server, &wifi_scan_get);
        httpd_register_uri_handler(http_server, &ap_clients_get);
    }

    if (http_server == NULL) {
        return ESP_FAIL;
    }

    return ESP_OK;
}
