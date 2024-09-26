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
// Максимальная длина cookie с идентификатором сессии включая нулевой символ
#define COOKIE_MAX_LEN          22
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

uint32_t safe_strtoul(const char *str)
{
    char *endptr;
    errno = 0;  // Сбросить errno перед вызовом strtoul
    uint32_t value = strtoul(str, &endptr, 10);

    if ((errno == ERANGE) && (value == UINT_MAX)) {
        ESP_LOGE(TAG, "Overflow occurred");
        return UINT_MAX;
    }
    if (endptr == str) {
        ESP_LOGE(TAG, "No digits were found");
        return 0;
    }
    if (*endptr != '\0') {
        ESP_LOGE(TAG, "Further characters after number: %s", endptr);
        return 0;
    }

    return value;
}

static uint32_t get_session_id(httpd_req_t *req) {
    char session_id_str[COOKIE_MAX_LEN];
    size_t buf_len = sizeof(session_id_str);
    if (httpd_req_get_cookie_val(req, "session_id", session_id_str, &buf_len) == ESP_OK) {
        ESP_LOGI(TAG, "Session ID: %s", session_id_str);
        return safe_strtoul(session_id_str);
    }
    return 0;  // Возвращаем 0, если не удалось получить session_id
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

static esp_err_t check_auth(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    uint32_t session_id = get_session_id(req);
    if (session_id != 0) {
        for (int i = 0; i < MAX_SESSIONS; i++) {
            if (session_buffer.session_ids[i] == session_id) {
                ESP_LOGI(TAG, "Session ID %lu is valid", session_id);
                return ESP_OK;
            }
        }
        ESP_LOGW(TAG, "Session ID %lu is not valid", session_id);
    } else {
        ESP_LOGW(TAG, "Session ID cookie not found");
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t logout(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    uint32_t session_id = get_session_id(req);
    if (session_id != 0) {
        // Найти и удалить session_id из буфера
        for (int i = 0; i < MAX_SESSIONS; i++) {
            if (session_buffer.session_ids[i] == session_id) {
                session_buffer.session_ids[i] = 0;  // Удаление session_id
                ESP_LOGI(TAG, "Session ID %lu removed from buffer", session_id);
                break;
            }
        }
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

static esp_err_t auth_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    cJSON *req_json = receive_json(req);
    if (req_json == NULL) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    cJSON *resp_json = cJSON_CreateObject();
    bool result = false;

    if (cJSON_HasObjectItem(req_json, "login") && cJSON_HasObjectItem(req_json, "pass")) {
        cJSON *login_req = cJSON_GetObjectItem(req_json, "login");
        cJSON *pass_req = cJSON_GetObjectItem(req_json, "pass");

        if ((login_req->type == cJSON_String) && (pass_req->type == cJSON_String)) {
            uint32_t session_id = authorization(login_req->valuestring, pass_req->valuestring);
            if (session_id != 0) {
                char cookie_header[COOKIE_MAX_LEN];
                snprintf(cookie_header, sizeof(cookie_header), "session_id=%lu", session_id);
                ESP_LOGI(TAG, "Session ID: %s", cookie_header);
                if (httpd_resp_set_hdr(req, "Set-Cookie", cookie_header) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to set cookie header");
                } else {
                    result = true;
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
    const char *resp_json_str = cJSON_Print(resp_json);
    httpd_resp_sendstr(req, resp_json_str);

    free((void *)resp_json_str);
    cJSON_Delete(resp_json);
    cJSON_Delete(req_json);

    if (result == false) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Проверка авторизации
static esp_err_t session_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != ESP_OK) {
        return ESP_FAIL;
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

    httpd_resp_set_type(req, "application/json");
    cJSON *resp_json = cJSON_CreateObject();

    cJSON_AddBoolToObject(resp_json, "logout", true);
    const char *resp_json_str = cJSON_Print(resp_json);
    httpd_resp_sendstr(req, resp_json_str);

    free((void *)resp_json_str);
    cJSON_Delete(resp_json);

    return ESP_OK;
}

static esp_err_t update_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != ESP_OK) {
        return ESP_FAIL;
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

    httpd_resp_set_type(req, "application/json");
    cJSON *resp_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp_json, "update", true);
    const char *resp_json_str = cJSON_Print(resp_json);
    httpd_resp_sendstr(req, resp_json_str);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != ESP_OK) {
        return ESP_FAIL;
    }

    cJSON *resp_json = cJSON_CreateObject();
    httpd_resp_set_type(req, "application/json");

    int items_num = setting_items_get_keys(NULL);
    const char *keys[items_num];
    setting_items_get_keys(keys);
    for (int i = 0; i < items_num; i++) {
        setting_item_type_t type = setting_items_get_type_in_json(keys[i]);
        if (type == SETTING_ITEM_TYPE_NUM) {
            uint32_t value = 0;
            setting_items_read(keys[i], &value);
            cJSON_AddNumberToObject(resp_json, keys[i], value);
        } else if (type == SETTING_ITEM_TYPE_STR) {
            char value[SETTING_ITEM_MAX_STR_LEN] = {0};
            setting_items_read(keys[i], value);
            cJSON_AddStringToObject(resp_json, keys[i], value);
        } else if (type == SETTING_ITEM_TYPE_BOOL) {
            uint8_t value = 0;
            setting_items_read(keys[i], &value);
            cJSON_AddBoolToObject(resp_json, keys[i], value);
        }
    }
    const char *settings = cJSON_Print(resp_json);
    httpd_resp_sendstr(req, settings);
    free((void *)settings);
    cJSON_Delete(resp_json);

    return ESP_OK;
}

static esp_err_t settings_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != ESP_OK) {
        return ESP_FAIL;
    }

    cJSON *req_json = receive_json(req);
    if (req_json == NULL) {
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    cJSON *resp_json = cJSON_CreateObject();
    int items_num = setting_items_get_keys(NULL);
    const char *keys[items_num];
    setting_items_get_keys(keys);
    for (int i = 0; i < items_num; i++) {
        if (cJSON_HasObjectItem(req_json, keys[i])) {
            cJSON *item = cJSON_GetObjectItem(req_json, keys[i]);
            void *value = NULL;
            static int false_val = 0;
            static int true_val = 1;
            if (item->type == cJSON_String) {
                value = (char *)item->valuestring;
            } else if (item->type == cJSON_Number) {
                value = (int *)&item->valueint;
            } else if (item->type == cJSON_False) {
                value = &false_val;
            } else if (item->type == cJSON_True) {
                value = &true_val;
            } else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unknown type json item");
            }

            if (setting_items_save(keys[i], value) == 0) {
                ESP_LOGI(TAG, "%s saved", keys[i]);
                cJSON_AddBoolToObject(resp_json, keys[i], true);
            } else {
                ESP_LOGI(TAG, "%s failed to save", keys[i]);
                cJSON_AddBoolToObject(resp_json, keys[i], false);
            }
        }
    }
    cJSON_Delete(req_json);
    const char *resp_json_str = cJSON_Print(resp_json);
    httpd_resp_sendstr(req, resp_json_str);
    free((void *)resp_json_str);
    cJSON_Delete(resp_json);

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
