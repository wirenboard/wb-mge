
#include <esp_http_server.h>
#include "http_server.h"
#include "cJSON.h"
#include "esp_log.h"
#include "ssdp.h"

static const char *TAG = "http_server";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");

static esp_err_t parse_json_number(cJSON *json, const char *key, int *value, httpd_req_t *req)
{
    cJSON *item = cJSON_GetObjectItem(json, key);
    if (cJSON_IsNumber(item)) {
        *value = item->valueint;
        return ESP_OK;
    } else {
        char msg[50];
        sprintf(msg, "Failed to parse \"%s\"", key);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
        return ESP_FAIL;
    }
}

static esp_err_t parse_json_string(cJSON *json, const char *key, char **value, httpd_req_t *req)
{
    cJSON *item = cJSON_GetObjectItem(json, key);
    if (cJSON_IsString(item)) {
        *value = item->valuestring;
        return ESP_OK;
    } else {
        char msg[50];
        sprintf(msg, "Failed to parse \"%s\"", key);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
        return ESP_FAIL;
    }
}

static esp_err_t ssdp_schema_get_handler(httpd_req_t* req)
{
  httpd_resp_set_type(req, "text/xml");
  const char* response = get_ssdp_schema_str();
  httpd_resp_send(req, response, strlen(response));
  return ESP_OK;
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

esp_err_t favicon_get_handler(httpd_req_t *req)
{
	httpd_resp_send(req, (const char *) favicon_ico_start, favicon_ico_end - favicon_ico_start);
	return ESP_OK;
}

static esp_err_t uart_settings_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "baud_rate", 9600);
    cJSON_AddNumberToObject(root, "data_size", 8);
    cJSON_AddNumberToObject(root, "stop_bits", 2);
    cJSON_AddStringToObject(root, "parity", "none");
    const char *sys_info = cJSON_Print(root);
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t uart_settings_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    char buf[200];
    int received = 0;

    received = httpd_req_recv(req, buf, total_len);
    buf[received] = '\0';
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }

    cJSON *req_json = cJSON_Parse(buf);
    if (req_json == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to parse JSON");
        return ESP_FAIL;
    }

    int baudrate;
    if (parse_json_number(req_json, "baudrate", &baudrate, req) != ESP_OK) {
        cJSON_Delete(req_json);
        return ESP_FAIL;
    }

    int datasize;
    if (parse_json_number(req_json, "datasize", &datasize, req) != ESP_OK) {
        cJSON_Delete(req_json);
        return ESP_FAIL;
    }

    char *parity;
    if (parse_json_string(req_json, "parity", &parity, req) != ESP_OK) {
        cJSON_Delete(req_json);
        return ESP_FAIL;
    }

    // ESP_LOGI("REST_TAG", "baudrate = %d, parity = %s, bytesize = %d", baudrate, parity, datasize);
    cJSON_Delete(req_json);

    httpd_resp_set_type(req, "application/json");
    cJSON *resp_json = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_json, "save", "success");
    const char *resp_json_str = cJSON_Print(resp_json);
    httpd_resp_sendstr(req, resp_json_str);
    free((void *)resp_json_str);
    cJSON_Delete(resp_json);

    return ESP_OK;
}

static const httpd_uri_t ssdp_schema = {.uri = "/description.xml", .method = HTTP_GET, .handler = ssdp_schema_get_handler};
static const httpd_uri_t index_get = {.uri = "/", .method = HTTP_GET, .handler = index_get_handler, .user_ctx = NULL};
static const httpd_uri_t favicon_get = {.uri = "/favicon.ico", .method   = HTTP_GET, .handler  = favicon_get_handler, .user_ctx = NULL};
static const httpd_uri_t uart_settings_get = {.uri = "/uart_settings", .method = HTTP_GET, .handler = uart_settings_get_handler, .user_ctx = NULL};
static const httpd_uri_t uart_settings_post = {.uri = "/uart_settings", .method = HTTP_POST, .handler = uart_settings_post_handler, .user_ctx = NULL};

esp_err_t http_server_init(ssdp_config_t* ssdp_config)
{
    static httpd_handle_t http_server = NULL;
    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&http_server, &httpd_config) == ESP_OK) {
        httpd_register_uri_handler(http_server, &ssdp_schema);
        httpd_register_uri_handler(http_server, &index_get);
        httpd_register_uri_handler(http_server, &favicon_get);
        httpd_register_uri_handler(http_server, &uart_settings_get);
        httpd_register_uri_handler(http_server, &uart_settings_post);
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
