#include "ota_handler.h"
#include "json_utils.h"
#include "auth.h"
#include "cmd_handler.h"
#include "http_server.h"

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <sys/param.h>
#include <string.h>
#include <stdlib.h>
#include "wb_app_desc.h"
#include "sys_info.h"


#define OTA_PROGRESS_LOG_STEP       (64 * 1024)

// How much complete silence from the uploading client is tolerated before the transfer is given up
// on. A client that vanishes without closing its socket (laptop suspended, phone left the AP) sends
// neither FIN nor RST, and TCP keep-alive is off, so nothing below this handler ever notices: the
// receive loop would retry forever and, because the IDF web server is single-threaded, take every
// other endpoint down with it until the device is power-cycled. 30 s is long enough to survive a
// brief Wi-Fi roam and far longer than any gap a client fast enough to upload a ~1.2 MB image would
// produce.
#define OTA_RECV_STALL_TIMEOUT_MS   30000

// Consecutive receive timeouts, arriving without a single accepted byte in between, that add up to
// OTA_RECV_STALL_TIMEOUT_MS. Derived from the configured receive window instead of hardcoded, so
// changing HTTP_RECV_WAIT_TIMEOUT_S keeps the silence budget above intact.
#define OTA_RECV_MAX_STALL_TIMEOUTS (OTA_RECV_STALL_TIMEOUT_MS / (HTTP_RECV_WAIT_TIMEOUT_S * 1000))


static const char *TAG = "ota_handler";

typedef struct {
    bool success;
    int bytes_written;
    const char *error_message;
} ota_result_t;


static esp_err_t ota_validate_fw_desc(wb_app_desc_t* ota_fw_desc)
{
    // Check for device signature from eFuse is not empty
    if (!strlen(sys_info.device_signature)) {
        ESP_LOGE(TAG, "Device signature is empty, OTA not allowed");
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Check magic work constant
    if (ota_fw_desc->magic_word != WB_APP_DESC_MAGIC_WORD) {
        ESP_LOGE(TAG, "Incorrect magic word: 0x%08" PRIX32, ota_fw_desc->magic_word);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Check signature
    char ota_signature[DEVICE_SIGNATURE_LEN + 1] = {0};
    wb_app_desc_get_str_field(ota_fw_desc->signature, DEVICE_SIGNATURE_LEN, ota_signature);
    if (strcmp(ota_signature, sys_info.device_signature) != 0) {
        ESP_LOGE(TAG, "Incorrect firmware signature: %s, expected: %s", ota_signature, sys_info.device_signature);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Print new firmware info
    ESP_LOGI(TAG, "New firmware info:");

    char ota_device_model[DEVICE_MODEL_LEN + 1] = {0};
    wb_app_desc_get_str_field(ota_fw_desc->device_model, DEVICE_MODEL_LEN, ota_device_model);
    ESP_LOGI(TAG, "Device model: %s", ota_device_model);

    char ota_fw_version[FIRMWARE_VERSION_LEN + 1] = {0};
    wb_app_desc_get_str_field(ota_fw_desc->fw_version, FIRMWARE_VERSION_LEN, ota_fw_version);
    ESP_LOGI(TAG, "Firmware version: %s", ota_fw_version);

    char ota_git_info[FIRMWARE_GIT_INFO_LEN + 1] = {0};
    wb_app_desc_get_str_field(ota_fw_desc->fw_git_info, FIRMWARE_GIT_INFO_LEN, ota_git_info);
    ESP_LOGI(TAG, "GIT info: %s", ota_git_info);

    return ESP_OK;
}


static bool ota_is_valid_content_type(const char *content_type)
{
    return (strstr(content_type, "application/octet-stream") != NULL) ||
           (strstr(content_type, "application/macbinary") != NULL);
}


static esp_err_t ota_validate_content_type(httpd_req_t *req)
{
    char content_type[64];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) == ESP_OK) {
        if (!ota_is_valid_content_type(content_type)) {
            ESP_LOGW(TAG, "Invalid content type for OTA update: %s", content_type);
            return ESP_FAIL;  // Caller chain: ota_update_from_http → ota_update_post_handler sends the error response
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

    ESP_LOGI(TAG, "Starting OTA update...");
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
    int progress_threshold = OTA_PROGRESS_LOG_STEP;
    bool app_desc_validated = false;
    int stall_timeouts = 0;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, JSON_UTILS_REQ_RECV_BUF_SIZE));

        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {  // Timeout Error: retry until the client is declared gone
            stall_timeouts++;
            if (stall_timeouts >= OTA_RECV_MAX_STALL_TIMEOUTS) {
                ESP_LOGE(TAG, "OTA upload stalled: %d consecutive network timeouts (%d ms of silence)",
                         stall_timeouts, OTA_RECV_STALL_TIMEOUT_MS);
                ESP_LOGE(TAG, "Total received bytes: %d, total firmware size: %d", *total_received, (int)req->content_len);
                free(buf);
                return ESP_FAIL;
            }
            ESP_LOGW(TAG, "Network timeout %d/%d, trying to continue", stall_timeouts, OTA_RECV_MAX_STALL_TIMEOUTS);
            continue;

        } else if (recv_len <= 0) {  // Serious Error: Abort OTA
            ESP_LOGE(TAG, "Network error during OTA upload, received: %d", recv_len);
            ESP_LOGE(TAG, "Total received bytes: %d, total firmware size: %d", *total_received, (int)req->content_len);
            free(buf);
            return ESP_FAIL;
        }

        stall_timeouts = 0;  // Any accepted byte proves the client is still there

        // Validate OTA firmware (only once when first chunk received)
        if (!app_desc_validated) {
            int min_len = WB_APP_DESC_OFFSET + sizeof(wb_app_desc_t);
            if (recv_len >= min_len) {
                wb_app_desc_t* ota_desc = (wb_app_desc_t*)((void*)buf + WB_APP_DESC_OFFSET);
                esp_err_t ret = ota_validate_fw_desc(ota_desc);
                if (ret == ESP_OK) {
                    app_desc_validated = true;
                } else {
                    ESP_LOGE(TAG, "OTA firmware is not valid or not intended for this device");
                    free(buf);
                    return ret;
                }
            } else {
                ESP_LOGE(TAG, "App descriptor not received. Received: %d / %d bytes", recv_len, min_len);
                free(buf);
                return ESP_FAIL;
            }
        }

        // Successful Upload: Flash firmware chunk
        if (esp_ota_write(ota_handle, (const void *)buf, recv_len) != ESP_OK) {
            ESP_LOGE(TAG, "Flash write error at offset %d", *total_received);
            free(buf);
            return ESP_FAIL;
        }

        remaining -= recv_len;
        *total_received += recv_len;

        // Print update progress every OTA_PROGRESS_LOG_STEP bytes
        if ((*total_received >= progress_threshold) || (remaining == 0)) {
            progress_threshold += OTA_PROGRESS_LOG_STEP;
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
    esp_err_t ret = ota_receive_and_write(req, ota_handle, &result->bytes_written);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NOT_SUPPORTED) {
            result->error_message = "Invalid OTA firmware";
        } else {
            result->error_message = "Network timeout during upload";
        }
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
    ota_result_t result = {0};
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
