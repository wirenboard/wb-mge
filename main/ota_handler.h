#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include "cJSON.h"

// OTA result structure
typedef struct {
    bool success;
    int bytes_written;
    const char *error_message;
} ota_result_t;

/**
 * @brief Initialize OTA handler module
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ota_handler_init(void);

/**
 * @brief HTTP handler for OTA update POST endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t ota_update_post_handler(httpd_req_t *req);

/**
 * @brief Perform OTA update from HTTP request
 * @param req HTTP request handle
 * @param result Pointer to store update result
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ota_update_from_http(httpd_req_t *req, ota_result_t *result);

/**
 * @brief Validate OTA content type from HTTP request
 * @param req HTTP request handle
 * @return ESP_OK if valid, ESP_FAIL if invalid
 */
esp_err_t ota_validate_content_type(httpd_req_t *req);

/**
 * @brief Begin OTA update process
 * @param ota_partition Pointer to store OTA partition
 * @param ota_handle Pointer to store OTA handle
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ota_begin_update(const esp_partition_t **ota_partition, esp_ota_handle_t *ota_handle);

/**
 * @brief Receive and write firmware data from HTTP request
 * @param req HTTP request handle
 * @param ota_handle OTA handle
 * @param total_received Pointer to store total bytes received
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ota_receive_and_write(httpd_req_t *req, esp_ota_handle_t ota_handle, int *total_received);

/**
 * @brief Finalize OTA update process
 * @param ota_handle OTA handle
 * @param ota_partition OTA partition
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ota_finalize_update(esp_ota_handle_t ota_handle, const esp_partition_t *ota_partition);

/**
 * @brief Create OTA success response JSON
 * @param bytes_written Number of bytes written
 * @return JSON object with success response
 */
cJSON *ota_create_success_response(int bytes_written);

#endif // OTA_HANDLER_H