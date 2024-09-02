
#include "http_server.h"

#include <esp_http_server.h>

#include "cJSON.h"
#include "esp_log.h"
#include "lwip/ip4_addr.h"
#include "setting_items.h"
#include "ssdp.h"

#define REQ_RECV_BUF_SIZE 1024

static const char *TAG = "http_server";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");

static esp_err_t ssdp_schema_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/xml");
    const char *response = get_ssdp_schema_str();
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_end - favicon_ico_start);
    return ESP_OK;
}

static cJSON *receive_json(httpd_req_t *req)
{
    char buf[REQ_RECV_BUF_SIZE];
    int received = 0;

    received = httpd_req_recv(req, buf, req->content_len);
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

static esp_err_t settings_get_handler(httpd_req_t *req)
{
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
    cJSON *req_json = receive_json(req);
    if (req_json == NULL) {
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    cJSON *resp_json = cJSON_CreateObject();
    void *value = NULL;
    int items_num = setting_items_get_keys(NULL);
    const char *keys[items_num];
    setting_items_get_keys(keys);
    for (int i = 0; i < items_num; i++) {
        if (cJSON_HasObjectItem(req_json, keys[i])) {
            cJSON *item = cJSON_GetObjectItem(req_json, keys[i]);
            if (item->type == cJSON_String) {
                value = (char *)item->valuestring;
            } else if (item->type == cJSON_Number) {
                value = (int *)&item->valueint;
            } else if (item->type == cJSON_False) {
                int val = 0;
                value = (int *)&val;
            } else if (item->type == cJSON_True) {
                int val = 1;
                value = (int *)&val;
            } else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Uncknown type json item");
            }

            if (setting_items_save(keys[i], value) == 0) {
                ESP_LOGI(TAG, "%s saved", keys[i]);
                cJSON_AddTrueToObject(resp_json, keys[i]);
            } else {
                ESP_LOGI(TAG, "%s failed to save", keys[i]);
                cJSON_AddFalseToObject(resp_json, keys[i]);
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
static const httpd_uri_t index_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t favicon_get = {
    .uri = "/favicon.ico",
    .method = HTTP_GET,
    .handler = favicon_get_handler,
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

    if (httpd_start(&http_server, &httpd_config) == ESP_OK) {
        httpd_register_uri_handler(http_server, &ssdp_schema);
        httpd_register_uri_handler(http_server, &index_get);
        httpd_register_uri_handler(http_server, &favicon_get);
        httpd_register_uri_handler(http_server, &settings_get);
        httpd_register_uri_handler(http_server, &settings_post);
    }
    if (http_server == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = ssdp_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ssdp: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Starting ssdp service");
    err = ssdp_start(ssdp_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ssdp: %s", esp_err_to_name(err));
        ssdp_stop();
    }
    return ESP_OK;
}
