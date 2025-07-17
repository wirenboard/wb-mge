#pragma once

#include <esp_http_server.h>
#include "cJSON.h"

// Buffer size for HTTP requests
// Use a reasonable default size for JSON requests
#define JSON_UTILS_REQ_RECV_BUF_SIZE 1024

/**
 * @brief Receive and parse JSON from HTTP request
 * @param req HTTP request handle
 * @return Parsed cJSON object or NULL on error
 */
cJSON *json_utils_receive_json(httpd_req_t *req);

/**
 * @brief Send JSON response and cleanup
 * @param req HTTP request handle
 * @param req_json Request JSON object (will be freed)
 * @param resp_json Response JSON object (will be freed)
 */
void json_utils_send_response(httpd_req_t *req, cJSON *req_json, cJSON *resp_json);

/**
 * @brief Send error JSON response
 * @param req HTTP request handle
 * @param error_message Error message string
 * @return ESP_OK on success
 */
esp_err_t json_utils_send_error(httpd_req_t *req, const char *error_message);

/**
 * @brief Cleanup JSON objects
 * @param req_json Request JSON object (can be NULL)
 * @param resp_json Response JSON object (can be NULL)
 */
void json_utils_cleanup(cJSON *req_json, cJSON *resp_json);
