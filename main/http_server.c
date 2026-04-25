#include "http_server.h"
#include "auth.h"
#include "wifi_scan.h"
#include "settings_manager.h"
#include "info_handlers.h"
#include "cmd_handler.h"
#include "ota_handler.h"
#include "wb_test.h"
#include "bridge/sniffer.h"
#include "bridge/cache_multimaster.h"
#include "bridge/port_manager.h"
#include "coverage_dump.h"
#include "template_handler.h"

#include <esp_http_server.h>
#include <sys/param.h>
#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "setting_items.h"
#include "sys_info.h"

#define MAX_URI_HANDLERS                    40          // Headroom for all endpoints (incl. per-port mode/send/cache handlers)
#define STACK_SIZE                          (1024 * 6)
#define MAX_OPEN_SOCKETS                    12          // Increased to allow simultaneous connections from at least 2-3 devices

#define WEB_PORT_FALLBACK                   HTTP_SERVER_DEFAULT_PORT    // Used if unable to read from settings

// Buffer size is chosen to be larger than the HTTP header size

static const char *TAG = "http_server";

extern const uint8_t favicon_start[] asm("_binary_favicon_webp_start");
extern const uint8_t favicon_end[] asm("_binary_favicon_webp_end");

extern const uint8_t index_css_start[] asm("_binary_index_css_gz_start");
extern const uint8_t index_css_end[] asm("_binary_index_css_gz_end");

extern const uint8_t index_js_start[] asm("_binary_index_js_gz_start");
extern const uint8_t index_js_end[] asm("_binary_index_js_gz_end");

extern const uint8_t index_html_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_gz_end");

// Roboto Latin subset — embedded RAW from frontend/dist/ (woff2 is already compressed)
extern const uint8_t roboto_latin_start[] asm("_binary_roboto_latin_wght_normal_woff2_start");
extern const uint8_t roboto_latin_end[]   asm("_binary_roboto_latin_wght_normal_woff2_end");

// Roboto Cyrillic subset — embedded RAW from frontend/dist/ (woff2 is already compressed)
extern const uint8_t roboto_cyrillic_start[] asm("_binary_roboto_cyrillic_wght_normal_woff2_start");
extern const uint8_t roboto_cyrillic_end[]   asm("_binary_roboto_cyrillic_wght_normal_woff2_end");

// Roboto Cyrillic-ext subset — embedded RAW from frontend/dist/ (covers Kazakh, Ukrainian extended)
extern const uint8_t roboto_cyrillic_ext_start[] asm("_binary_roboto_cyrillic_ext_wght_normal_woff2_start");
extern const uint8_t roboto_cyrillic_ext_end[]   asm("_binary_roboto_cyrillic_ext_wght_normal_woff2_end");



static const httpd_config_t httpd_default_config = HTTPD_DEFAULT_CONFIG();

static httpd_handle_t http_server = NULL;
static httpd_config_t httpd_current_config = {0};

// Per-asset ETag strings derived from the firmware build identity, precomputed
// once in http_server_init(). 96 bytes comfortably fits the git-describe string
// (bounded by FIRMWARE_GIT_INFO_LEN, ~50 chars) plus the surrounding quotes and
// the per-asset suffix.
#define ETAG_BUF_SIZE   96
static char s_etag_html[ETAG_BUF_SIZE] = {0};
static char s_etag_js[ETAG_BUF_SIZE]   = {0};
static char s_etag_css[ETAG_BUF_SIZE]  = {0};

// Serve a gzip-compressed shell asset with ETag-based revalidation.
// If the client's If-None-Match matches this asset's ETag, reply 304 Not Modified
// with an empty body; otherwise reply 200 with the gzip body. Cache-Control:
// no-cache tells the browser to store the asset but always revalidate it, so a
// new firmware build (new ETag) is always picked up.
static esp_err_t serve_cached_asset(httpd_req_t *req, const char *ctype,
                                    const uint8_t *start, const uint8_t *end,
                                    const char *etag)
{
    char inm[ETAG_BUF_SIZE] = {0};
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK &&
        strcmp(inm, etag) == 0) {
        httpd_resp_set_status(req, "304 Not Modified");
        httpd_resp_set_hdr(req, "ETag", etag);
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    httpd_resp_set_type(req, ctype);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "ETag", etag);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, (const char *)start, end - start);
    return ESP_OK;
}


static esp_err_t index_html_get_handler(httpd_req_t *req)
{
    return serve_cached_asset(req, "text/html", index_html_start, index_html_end, s_etag_html);
}

static esp_err_t index_css_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);
    return serve_cached_asset(req, "text/css", index_css_start, index_css_end, s_etag_css);
}

static esp_err_t index_js_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", __func__);
    return serve_cached_asset(req, "application/javascript", index_js_start, index_js_end, s_etag_js);
}

static esp_err_t roboto_latin_get_handler(httpd_req_t *req)
{
    // woff2 is already compressed and embedded raw, so no Content-Encoding here.
    httpd_resp_set_type(req, "font/woff2");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
    httpd_resp_send(req, (const char *)roboto_latin_start, roboto_latin_end - roboto_latin_start);
    return ESP_OK;
}

static esp_err_t roboto_cyrillic_get_handler(httpd_req_t *req)
{
    // woff2 is already compressed and embedded raw, so no Content-Encoding here.
    httpd_resp_set_type(req, "font/woff2");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
    httpd_resp_send(req, (const char *)roboto_cyrillic_start, roboto_cyrillic_end - roboto_cyrillic_start);
    return ESP_OK;
}

static esp_err_t roboto_cyrillic_ext_get_handler(httpd_req_t *req)
{
    // woff2 is already compressed and embedded raw, so no Content-Encoding here.
    httpd_resp_set_type(req, "font/woff2");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
    httpd_resp_send(req, (const char *)roboto_cyrillic_ext_start, roboto_cyrillic_ext_end - roboto_cyrillic_ext_start);
    return ESP_OK;
}


static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    // webp is already compressed and embedded raw, so no Content-Encoding here.
    httpd_resp_set_type(req, "image/webp");
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
static const httpd_uri_t roboto_latin_get = {
    .uri       = "/roboto-latin-wght-normal.woff2",
    .method    = HTTP_GET,
    .handler   = roboto_latin_get_handler,
    .user_ctx  = NULL,
};
static const httpd_uri_t roboto_cyrillic_get = {
    .uri       = "/roboto-cyrillic-wght-normal.woff2",
    .method    = HTTP_GET,
    .handler   = roboto_cyrillic_get_handler,
    .user_ctx  = NULL,
};
static const httpd_uri_t roboto_cyrillic_ext_get = {
    .uri       = "/roboto-cyrillic-ext-wght-normal.woff2",
    .method    = HTTP_GET,
    .handler   = roboto_cyrillic_ext_get_handler,
    .user_ctx  = NULL,
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
static const httpd_uri_t wb_test_get = {
    .uri = "/wb_test",
    .method = HTTP_GET,
    .handler = wb_test_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t wb_test_post = {
    .uri = "/wb_test",
    .method = HTTP_POST,
    .handler = wb_test_post_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t hostname_get = {
    .uri = "/hostname",
    .method = HTTP_GET,
    .handler = hostname_get_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t device_template_post = {
    .uri = "/device-template",
    .method = HTTP_POST,
    .handler = template_upload_post_handler,
    .user_ctx = NULL,
};
static const httpd_uri_t device_template_get = {
    .uri = "/device-template",
    .method = HTTP_GET,
    .handler = template_get_handler,
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
    return http_server_init_port(get_web_port_setting());
}


esp_err_t http_server_init_port(uint16_t port)
{
    memcpy(&httpd_current_config, &httpd_default_config, sizeof(httpd_current_config));
    httpd_current_config.max_uri_handlers = MAX_URI_HANDLERS;
    httpd_current_config.stack_size = STACK_SIZE;
    httpd_current_config.max_open_sockets = MAX_OPEN_SOCKETS;
    httpd_current_config.server_port = port;

    // Precompute per-asset ETags from the firmware build identity. sys_info is
    // populated by sys_info_init(), which runs before http_server_init(). The
    // per-asset suffix keeps each URL's ETag unique; fall back gracefully if the
    // git-describe string is empty.
    const char *build_id = sys_info.firmware_git_info;
    if (build_id[0] == '\0') {
        build_id = sys_info.firmware_ver;
    }
    if (build_id[0] == '\0') {
        build_id = "dev";
    }
    snprintf(s_etag_html, sizeof(s_etag_html), "\"%s-html\"", build_id);
    snprintf(s_etag_js,   sizeof(s_etag_js),   "\"%s-js\"",   build_id);
    snprintf(s_etag_css,  sizeof(s_etag_css),  "\"%s-css\"",  build_id);

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
        httpd_register_uri_handler(http_server, &roboto_latin_get);
        httpd_register_uri_handler(http_server, &roboto_cyrillic_get);
        httpd_register_uri_handler(http_server, &roboto_cyrillic_ext_get);

        httpd_register_uri_handler(http_server, &update_post);
        httpd_register_uri_handler(http_server, &info_get);
        httpd_register_uri_handler(http_server, &settings_get);
        httpd_register_uri_handler(http_server, &settings_post);
        httpd_register_uri_handler(http_server, &cmd_post);
        httpd_register_uri_handler(http_server, &wifi_scan_start_post);
        httpd_register_uri_handler(http_server, &wifi_scan_results_get);
        httpd_register_uri_handler(http_server, &ap_clients_get);
        httpd_register_uri_handler(http_server, &uptime_get);
        httpd_register_uri_handler(http_server, &wb_test_get);
        httpd_register_uri_handler(http_server, &wb_test_post);
        httpd_register_uri_handler(http_server, &hostname_get);
        sniffer_register_handlers(http_server);
        cache_multimaster_register_handlers(http_server);
        port_manager_register_handlers(http_server);
#ifdef COVERAGE_BUILD
        coverage_dump_register_handlers(http_server);
#endif
        httpd_register_uri_handler(http_server, &device_template_post);
        httpd_register_uri_handler(http_server, &device_template_get);
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


uint16_t http_server_get_port(void)
{
    if (http_server == NULL) {
        return 0;   // not listening: nothing to hand over, nothing to roll back to
    }
    return httpd_current_config.server_port;
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
