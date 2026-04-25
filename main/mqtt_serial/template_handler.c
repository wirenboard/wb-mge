#include "template_handler.h"
#include "template.h"
#include "auth.h"
#include "json_utils.h"

#include "esp_spiffs.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "template_handler";

/* Embedded default template */
extern const uint8_t device_template_start[] asm("_binary_device_template_json_start");
extern const uint8_t device_template_end[]   asm("_binary_device_template_json_end");

esp_err_t template_handler_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = "spiffs",
        .max_files              = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS mounted");
    }
    return ret;
}

esp_err_t template_handler_load(char **buf, size_t *len)
{
    /* Try SPIFFS file first */
    FILE *f = fopen(TEMPLATE_SPIFFS_PATH, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        if (sz > 0 && sz < TEMPLATE_MAX_SIZE) {
            char *data = malloc((size_t)sz + 1);
            if (data) {
                size_t n = fread(data, 1, (size_t)sz, f);
                fclose(f);
                data[n] = '\0';
                *buf = data;
                *len = n;
                ESP_LOGI(TAG, "Loaded template from SPIFFS (%zu bytes)", n);
                return ESP_OK;
            }
        }
        fclose(f);
    }

    /* Fallback: embedded default */
    size_t tmpl_len = (size_t)(device_template_end - device_template_start);
    char *data = malloc(tmpl_len + 1);
    if (!data) return ESP_ERR_NO_MEM;
    memcpy(data, device_template_start, tmpl_len);
    data[tmpl_len] = '\0';
    *buf = data;
    *len = tmpl_len;
    ESP_LOGI(TAG, "Loaded embedded default template (%zu bytes)", tmpl_len);
    return ESP_OK;
}

/* POST /api/device-template  — upload a new JSON template */
esp_err_t template_upload_post_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) return ESP_OK;

    int content_len = req->content_len;
    if (content_len <= 0 || content_len >= TEMPLATE_MAX_SIZE) {
        return json_utils_send_error(req, "Invalid content length");
    }

    char *buf = malloc((size_t)content_len + 1);
    if (!buf) return json_utils_send_error(req, "OOM");

    int received = 0;
    while (received < content_len) {
        int r = httpd_req_recv(req, buf + received, content_len - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { free(buf); return json_utils_send_error(req, "Receive error"); }
        received += r;
    }
    buf[received] = '\0';

    /* Validate JSON is parseable as a template */
    wb_template_t tmpl;
    if (wb_template_parse_str(buf, &tmpl) != 0) {
        free(buf);
        return json_utils_send_error(req, "Invalid template JSON");
    }
    ESP_LOGI(TAG, "Uploading template: '%s' (%d channels)", tmpl.device_name, tmpl.num_channels);
    wb_template_free(&tmpl);

    /* Save to SPIFFS */
    FILE *f = fopen(TEMPLATE_SPIFFS_PATH, "w");
    if (!f) {
        free(buf);
        return json_utils_send_error(req, "Failed to open file for writing");
    }
    size_t written = fwrite(buf, 1, (size_t)received, f);
    fclose(f);
    free(buf);

    if ((int)written != received) {
        return json_utils_send_error(req, "Write error");
    }

    ESP_LOGI(TAG, "Template saved to SPIFFS (%zu bytes)", written);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddNumberToObject(resp, "bytes", (double)written);
    json_utils_send_response(req, NULL, resp);
    return ESP_OK;
}

/* GET /api/device-template — download current template */
esp_err_t template_get_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) return ESP_OK;

    char *buf = NULL;
    size_t len = 0;
    esp_err_t err = template_handler_load(&buf, &len);
    if (err != ESP_OK || !buf) {
        return json_utils_send_error(req, "Failed to load template");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, (ssize_t)len);
    free(buf);
    return ESP_OK;
}

/* DELETE /api/device-template — remove custom template, revert to embedded */
esp_err_t template_delete_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) return ESP_OK;

    struct stat st;
    if (stat(TEMPLATE_SPIFFS_PATH, &st) == 0) {
        remove(TEMPLATE_SPIFFS_PATH);
        ESP_LOGI(TAG, "Custom template deleted");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    json_utils_send_response(req, NULL, resp);
    return ESP_OK;
}
