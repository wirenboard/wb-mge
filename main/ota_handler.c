#include "ota_handler.h"
#include "json_utils.h"
#include "auth.h"
#include "cmd_handler.h"

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <sys/param.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ota_handler";

typedef struct {
    bool success;
    int bytes_written;
    const char *error_message;
} ota_result_t;

esp_err_t ota_handler_init(void)
{
    ESP_LOGI(TAG, "OTA handler initialized");
    return ESP_OK;
}

static esp_err_t ota_validate_content_type(httpd_req_t *req)
{
    char content_type[64];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) == ESP_OK) {
        if (strstr(content_type, "application/octet-stream") == NULL) {
            ESP_LOGW(TAG, "Invalid content type for OTA update: %s", content_type);
            return json_utils_send_error(req, "Invalid content type. Expected: application/octet-stream");
        }
    } else {
        ESP_LOGW(TAG, "No Content-Type header found, proceeding anyway");
    }
    return ESP_OK;
}

static esp_err_t ota_begin_update(const esp_partition_t **ota_partition, esp_ota_handle_t *ota_handle)
{
    *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (*ota_partition == NULL) {
        ESP_LOGE(TAG, "OTA partition not found");
        return ESP_FAIL;
    }

    if (esp_ota_begin(*ota_partition, OTA_SIZE_UNKNOWN, ota_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to begin OTA update");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update started, target partition: %s", (*ota_partition)->label);
    return ESP_OK;
}

static esp_err_t ota_receive_and_write(httpd_req_t *req, esp_ota_handle_t ota_handle, int *total_received)
{
    if ((req == NULL) || (total_received == NULL)) {
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(JSON_UTILS_REQ_RECV_BUF_SIZE);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate OTA buffer");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    *total_received = 0;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, JSON_UTILS_REQ_RECV_BUF_SIZE));

        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {  // Timeout Error: Just retry
            continue;

        } else if (recv_len <= 0) {  // Serious Error: Abort OTA
            ESP_LOGE(TAG, "Network error during OTA upload, received: %d", recv_len);
            free(buf);
            return ESP_FAIL;
        }

        // Successful Upload: Flash firmware chunk
        if (esp_ota_write(ota_handle, (const void *)buf, recv_len) != ESP_OK) {
            ESP_LOGE(TAG, "Flash write error at offset %d", *total_received);
            free(buf);
            return ESP_FAIL;
        }

        remaining -= recv_len;
        *total_received += recv_len;

        // Log progress every 10% or every 64KB, whichever is larger
        int progress_threshold = MAX(req->content_len / 10, 65536);
        if (*total_received % progress_threshold == 0 || remaining == 0) {
            int progress_percent = (*total_received * 100) / req->content_len;
            ESP_LOGI(TAG, "OTA Update progress: %d%% (%d/%d bytes)",
                     progress_percent, *total_received, req->content_len);
        }
    }

    free(buf);
    return ESP_OK;
}

static esp_err_t ota_finalize_update(esp_ota_handle_t ota_handle, const esp_partition_t *ota_partition)
{
    ESP_LOGI(TAG, "OTA upload completed, validating firmware...");

    if (esp_ota_end(ota_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Firmware validation failed");
        return ESP_FAIL;
    }

    if (esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update successful, rebooting in 1 second...");
    return ESP_OK;
}

static cJSON *ota_create_success_response(int bytes_written)
{
    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create success response JSON");
        return NULL;
    }

    cJSON_AddBoolToObject(response_json, "success", true);
    cJSON_AddStringToObject(response_json, "message", "Firmware updated successfully, rebooting...");
    cJSON_AddNumberToObject(response_json, "bytes_written", bytes_written);

    return response_json;
}

static esp_err_t ota_update_from_http(httpd_req_t *req, ota_result_t *result)
{
    if ((req == NULL) || (result == NULL)) {
        return ESP_FAIL;
    }

    // Initialize result
    result->success = false;
    result->bytes_written = 0;
    result->error_message = NULL;

    ESP_LOGI(TAG, "OTA Update request received, size: %d bytes", req->content_len);

    // Validate content type
    if (ota_validate_content_type(req) != ESP_OK) {
        result->error_message = "Invalid content type";
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    const esp_partition_t *ota_partition;

    // Begin OTA update
    if (ota_begin_update(&ota_partition, &ota_handle) != ESP_OK) {
        result->error_message = "Failed to begin OTA update";
        return ESP_FAIL;
    }

    // Receive and write firmware data
    if (ota_receive_and_write(req, ota_handle, &result->bytes_written) != ESP_OK) {
        result->error_message = "Network timeout during upload";
        esp_ota_abort(ota_handle);
        return ESP_FAIL;
    }

    // Finalize update
    if (ota_finalize_update(ota_handle, ota_partition) != ESP_OK) {
        result->error_message = "Firmware validation failed";
        return ESP_FAIL;
    }

    result->success = true;
    return ESP_OK;
}

esp_err_t ota_update_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA update POST request received");

    // Check authentication
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    // Perform OTA update
    ota_result_t result;
    esp_err_t update_result = ota_update_from_http(req, &result);

    if (update_result != ESP_OK) {
        // Send error response
        const char *error_msg = result.error_message ? result.error_message : "OTA update failed";
        return json_utils_send_error(req, error_msg);
    }

    // Send success response
    cJSON *response_json = ota_create_success_response(result.bytes_written);
    if (response_json == NULL) {
        return json_utils_send_error(req, "Failed to create response");
    }

    json_utils_send_response(req, NULL, response_json);

    // Reboot device after successful update
    cmd_reboot_device();

    return ESP_OK;
}
