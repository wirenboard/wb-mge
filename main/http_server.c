#include "http_server.h"
#include "auth.h"
#include "wifi_scan.h"
#include "settings_manager.h"
#include "info_handlers.h"
#include "cmd_handler.h"
#include "ota_handler.h"

#include <esp_http_server.h>
#include <sys/param.h>
#include <string.h>

#include "esp_log.h"
#include "setting_items.h"

#define MAX_URI_HANDLERS                    20          // TODO: Подобрать значение к релизу
#define STACK_SIZE                          (1024 * 6)  // TODO: Проверить размер используемой памяти
#define MAX_OPEN_SOCKETS                    12          // Увеличено, чтобы можно было одновременно подключиться хотя бы с 2-3 устройств

#define WEB_PORT_FALLBACK                   80          // Используется, если не удается прочитать из настроек

// Размер буфера выбран таким образом, чтобы он был больше, чем размер заголовка HTTP

static const char *TAG = "http_server";

extern const uint8_t favicon_start[] asm("_binary_favicon_webp_gz_start");
extern const uint8_t favicon_end[] asm("_binary_favicon_webp_gz_end");

extern const uint8_t index_css_start[] asm("_binary_index_css_gz_start");
extern const uint8_t index_css_end[] asm("_binary_index_css_gz_end");

extern const uint8_t index_js_start[] asm("_binary_index_js_gz_start");
extern const uint8_t index_js_end[] asm("_binary_index_js_gz_end");

extern const uint8_t index_html_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_gz_end");


static const httpd_config_t httpd_default_config = HTTPD_DEFAULT_CONFIG();

static httpd_handle_t http_server = NULL;
static httpd_config_t httpd_current_config = {0};


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
    httpd_resp_set_type(req, "image/webp");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_send(req, (const char *)favicon_start, favicon_end - favicon_start);
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
    .handler = ota_update_post_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t info_get = {
    .uri = "/info",
    .method = HTTP_GET,
    .handler = info_get_handler,
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
static const httpd_uri_t wb_status_get = {
    .uri = "/wb_status",
    .method = HTTP_GET,
    .handler = wb_status_get_handler,
    .user_ctx = NULL,
};


static uint16_t get_web_port_setting(void)
{
    uint16_t web_port = (uint16_t)setting_items_read_int(KEY_WEB_PORT);
    if (web_port == 0) {
        web_port = WEB_PORT_FALLBACK;   // Fallback to default port
        ESP_LOGW(TAG, "Using default web port: %u", web_port);
    }
    return web_port;
}


esp_err_t http_server_init(void)
{
    memcpy(&httpd_current_config, &httpd_default_config, sizeof(httpd_current_config));
    httpd_current_config.max_uri_handlers = MAX_URI_HANDLERS;
    httpd_current_config.stack_size = STACK_SIZE;
    httpd_current_config.max_open_sockets = MAX_OPEN_SOCKETS;
    httpd_current_config.server_port = get_web_port_setting();

    if (wifi_scan_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi scan module");
        return ESP_FAIL;
    }

    if (auth_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize authentication module");
        return ESP_FAIL;
    }

    if (httpd_start(&http_server, &httpd_current_config) == ESP_OK) {
        httpd_register_uri_handler(http_server, &auth_post);
        httpd_register_uri_handler(http_server, &session_get);
        httpd_register_uri_handler(http_server, &logout_post);

        // static files
        httpd_register_uri_handler(http_server, &index_get);
        httpd_register_uri_handler(http_server, &index_css_get);
        httpd_register_uri_handler(http_server, &index_js_get);
        httpd_register_uri_handler(http_server, &favicon_get);

        httpd_register_uri_handler(http_server, &update_post);
        httpd_register_uri_handler(http_server, &info_get);
        httpd_register_uri_handler(http_server, &settings_get);
        httpd_register_uri_handler(http_server, &settings_post);
        httpd_register_uri_handler(http_server, &cmd_post);
        httpd_register_uri_handler(http_server, &wifi_scan_start_post);
        httpd_register_uri_handler(http_server, &wifi_scan_results_get);
        httpd_register_uri_handler(http_server, &ap_clients_get);
        httpd_register_uri_handler(http_server, &uptime_get);
        httpd_register_uri_handler(http_server, &wb_status_get);
    }

    if (http_server == NULL) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "HTTP server started on port: %u", httpd_current_config.server_port);
    return ESP_OK;
}


esp_err_t http_server_deinit(void)
{
    if (http_server != NULL) {
        esp_err_t ret = httpd_stop(http_server);
        if (ret == ESP_OK) {
            ESP_LOGW(TAG, "HTTP server stopped");
        } else {
            ESP_LOGW(TAG, "Failed to stop HTTP server");
        }
        http_server = NULL;
        return ret;
    }
    return ESP_OK;  // HTTP server not started -> deinitialized
}


bool http_server_check_settings_changed(void)
{
    if (http_server == NULL) {
        return false;
    }
    uint16_t new_port = get_web_port_setting();
    if (new_port != httpd_current_config.server_port) {
        return true;
    }
    return false;
}
