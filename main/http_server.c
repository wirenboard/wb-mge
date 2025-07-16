#include "http_server.h"
#include "auth_session.h"
#include "json_utils.h"
#include "wifi_scan.h"
#include "settings_manager.h"
#include "auth_handlers.h"
#include "info_handlers.h"

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
#include "update_rs485_mio_gpio_states.h"
#include "bridge.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Размер буфера выбран таким образом, чтобы он был больше, чем размер заголовка HTTP
// Note: REQ_RECV_BUF_SIZE moved to json_utils.h as JSON_UTILS_REQ_RECV_BUF_SIZE
// Note: Authentication constants moved to auth_handlers.h

#define CMD_NAME_MAX_LEN        32
#define REBOOT_DELAY_MS         1000
#define REBOOT_TASK_STACK_SIZE  2048
#define REBOOT_TASK_PRIORITY    5

// Note: WiFi scan constants moved to wifi_scan.h
#define BSSID_STR_SIZE          18

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

// Note: WiFi scan mutex and state moved to wifi_scan.c

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
    if (req->content_len > JSON_UTILS_REQ_RECV_BUF_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too large");
        return false;
    }
    return true;
}

static bool check_auth(httpd_req_t *req)
{
    return auth_middleware_check(req);
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
    json_utils_send_response(req, req_json, resp_json);
}

static cJSON *receive_json(httpd_req_t *req)
{
    return json_utils_receive_json(req);
}

static esp_err_t update_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);

    if (check_auth(req) != true) {
        return ESP_OK;
    }

    char *buf = (char *)malloc(JSON_UTILS_REQ_RECV_BUF_SIZE);
    esp_ota_handle_t ota_handle;
    int remaining = req->content_len;

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA Begin Error");
        free(buf);
        return ESP_FAIL;
    }

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, JSON_UTILS_REQ_RECV_BUF_SIZE));

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

static const httpd_uri_t auth_post = {
    .uri = "/auth",
    .method = HTTP_POST,
    .handler = auth_login_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t session_get = {
    .uri = "/session",
    .method = HTTP_GET,
    .handler = auth_session_check_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t logout_post = {
    .uri = "/logout",
    .method = HTTP_POST,
    .handler = auth_logout_handler,
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

    // Initialize WiFi scan module
    if (wifi_scan_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi scan module");
        return ESP_FAIL;
    }

    // Initialize settings manager
    if (settings_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize settings manager");
        return ESP_FAIL;
    }

    // Initialize authentication handlers
    if (auth_handlers_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize authentication handlers");
        return ESP_FAIL;
    }

    // Initialize info handlers
    if (info_handlers_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize info handlers");
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
