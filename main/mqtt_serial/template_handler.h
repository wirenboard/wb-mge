#pragma once

#include "esp_err.h"
#include <esp_http_server.h>

#define TEMPLATE_SPIFFS_PATH    "/spiffs/device_template.json"
#define TEMPLATE_MAX_SIZE       (192 * 1024)  /* 192 KB — leaves SPIFFS metadata room in 256 KB partition */

/* Mount SPIFFS. Call once at startup. */
esp_err_t template_handler_init(void);

/*
 * Load device template into *buf (heap-allocated, caller must free).
 * Priority: SPIFFS file -> embedded default.
 * Returns ESP_OK and sets *buf and *len on success.
 */
esp_err_t template_handler_load(char **buf, size_t *len);

/* HTTP handlers: register on the server */
esp_err_t template_upload_post_handler(httpd_req_t *req);
esp_err_t template_get_handler(httpd_req_t *req);
esp_err_t template_delete_handler(httpd_req_t *req);
