#ifndef INFO_HANDLERS_H
#define INFO_HANDLERS_H

#include <esp_http_server.h>
#include "cJSON.h"
#include "sys_info.h"

/**
 * @brief Initialize info handlers module
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t info_handlers_init(void);

/**
 * @brief HTTP handler for system info GET endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t info_get_handler(httpd_req_t *req);

/**
 * @brief HTTP handler for system info POST endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t info_post_handler(httpd_req_t *req);

/**
 * @brief HTTP handler for system uptime GET endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t uptime_get_handler(httpd_req_t *req);

/**
 * @brief HTTP handler for AP clients GET endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t ap_clients_get_handler(httpd_req_t *req);

/**
 * @brief Build device info JSON object
 * @param device_json Pointer to store the device JSON object
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t info_build_device_json(cJSON **device_json);

/**
 * @brief Build network info JSON object
 * @param network_json Pointer to store the network JSON object
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t info_build_network_json(cJSON **network_json);

/**
 * @brief Build RS485 info JSON object
 * @param rs485_json Pointer to store the RS485 JSON object
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t info_build_rs485_json(cJSON **rs485_json);

/**
 * @brief Build uptime info JSON object
 * @param uptime_json Pointer to store the uptime JSON object
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t info_build_uptime_json(cJSON **uptime_json);

/**
 * @brief Build AP clients info JSON array
 * @param clients_json Pointer to store the clients JSON array
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t info_build_ap_clients_json(cJSON **clients_json);

/**
 * @brief Update system info from JSON
 * @param request_json JSON object containing updates
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t info_update_from_json(cJSON *request_json);

#endif // INFO_HANDLERS_H