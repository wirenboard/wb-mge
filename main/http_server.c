#include "http_server.h"

#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <sys/param.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "setting_items.h"
#include "ssdp.h"

// Размер буфера выбран таким образом, чтобы он был больше, чем размер заголовка HTTP
#define REQ_RECV_BUF_SIZE       (CONFIG_HTTPD_MAX_REQ_HDR_LEN * 2)
// Длина строки с максимальным числом uint32 (10 цифр + 1 символ для '\0')
#define UINT32_STR_MAX_LEN      11
// Максимальная длина cookie с идентификатором сессии (session_id=<u32_id>)
#define COOKIE_MAX_LEN          (11 + UINT32_STR_MAX_LEN)
// При превышении этого количества сессий, самая старая сессия будет удалена
#define MAX_SESSIONS            10

typedef struct {
    uint32_t session_ids[MAX_SESSIONS];
    int current_index;
} session_buffer_t;

static const char *TAG = "http_server";

extern const uint8_t favicon_start[] asm("_binary_favicon_ico_start");
extern const uint8_t favicon_end[] asm("_binary_favicon_ico_end");

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

static session_buffer_t session_buffer = {
    .current_index = 0,
};

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

    if (errno == ERANGE || value > UINT32_MAX) {
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
    }

    return ESP_OK;
}

static esp_err_t ssdp_schema_get_handler(httpd_req_t *req)
{
    return ESP_OK;
}

static esp_err_t index_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
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
    char buf[REQ_RECV_BUF_SIZE];
    int received = 0;

    received = httpd_req_recv(req, buf, sizeof(buf));
    buf[received] = '\0';
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return NULL;
    }

    cJSON *req_json = cJSON_Parse(buf);
    if (req_json == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to parse JSON");
        return NULL;
    }

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

    char buf[REQ_RECV_BUF_SIZE];
    esp_ota_handle_t ota_handle;
    int remaining = req->content_len;

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {  // Timeout Error: Just retry
            continue;

        } else if (recv_len <= 0) {  // Serious Error: Abort OTA
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
            return ESP_FAIL;
        }

        // Successful Upload: Flash firmware chunk
        if (esp_ota_write(ota_handle, (const void *)buf, recv_len) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
            return ESP_FAIL;
        }

        remaining -= recv_len;
    }

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
    esp_restart();

    return ESP_OK;
}

static inline void add_setting_item_to_json(cJSON *json, const char *key)
{
    setting_item_type_t type = setting_items_get_type_in_json(key);

    switch (type) {
        case SETTING_ITEM_TYPE_NUM: {
            uint32_t value = 0;
            setting_items_read(key, &value);
            cJSON_AddNumberToObject(json, key, value);
            break;
        }
        case SETTING_ITEM_TYPE_STR: {
            char value[SETTING_ITEM_MAX_STR_LEN] = {0};
            setting_items_read(key, value);
            cJSON_AddStringToObject(json, key, value);
            break;
        }
        case SETTING_ITEM_TYPE_BOOL: {
            uint8_t value = 0;
            setting_items_read(key, &value);
            cJSON_AddBoolToObject(json, key, value);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unknown setting item type for key: %s", key);
            break;
    }
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
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unknown type json item");
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

    resp_and_free_json(req, req_json, resp_json);

    return ESP_OK;
}

static const httpd_uri_t ssdp_schema = {
    .uri = "/description.xml",
    .method = HTTP_GET,
    .handler = ssdp_schema_get_handler,
    .user_ctx = NULL,
};
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
    .uri = "/favicon.ico",
    .method = HTTP_GET,
    .handler = favicon_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t update_post = {
    .uri = "/update",
    .method = HTTP_POST,
    .handler = update_post_handler,
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

esp_err_t http_server_init(ssdp_config_t *ssdp_config)
{
    static httpd_handle_t http_server = NULL;
    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
    httpd_config.max_uri_handlers = 9; // Количество URI обработчиков

    if (httpd_start(&http_server, &httpd_config) == ESP_OK) {
        httpd_register_uri_handler(http_server, &ssdp_schema);
        httpd_register_uri_handler(http_server, &auth_post);
        httpd_register_uri_handler(http_server, &session_get);
        httpd_register_uri_handler(http_server, &logout_post);
        httpd_register_uri_handler(http_server, &index_get);
        httpd_register_uri_handler(http_server, &favicon_get);
        httpd_register_uri_handler(http_server, &update_post);
        httpd_register_uri_handler(http_server, &settings_get);
        httpd_register_uri_handler(http_server, &settings_post);
    }
    if (http_server == NULL) {
        return ESP_FAIL;
    }

    return ESP_OK;
}
