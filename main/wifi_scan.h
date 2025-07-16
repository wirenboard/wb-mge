#ifndef WIFI_SCAN_H
#define WIFI_SCAN_H

#include <esp_wifi.h>
#include <esp_http_server.h>
#include "cJSON.h"

// WiFi scan constants
#define WIFI_SCAN_MAX_RESULTS   20
#define WIFI_SCAN_BSSID_STR_SIZE 18

/**
 * @brief Initialize WiFi scan module
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t wifi_scan_init(void);

/**
 * @brief Start WiFi scan operation
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t wifi_scan_start(void);

/**
 * @brief Check if WiFi scan is in progress
 * @return true if scan is in progress, false otherwise
 */
bool wifi_scan_is_in_progress(void);

/**
 * @brief Check if WiFi scan is completed
 * @return true if scan is completed, false otherwise
 */
bool wifi_scan_is_completed(void);

/**
 * @brief Get WiFi scan results as JSON
 * @param results_json Pointer to store the results JSON object
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t wifi_scan_get_results_json(cJSON **results_json);

/**
 * @brief Get WiFi scan status as JSON
 * @param status_json Pointer to store the status JSON object
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t wifi_scan_get_status_json(cJSON **status_json);

/**
 * @brief HTTP handler for WiFi scan start endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t wifi_scan_start_handler(httpd_req_t *req);

/**
 * @brief HTTP handler for WiFi scan results endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t wifi_scan_results_handler(httpd_req_t *req);

#endif // WIFI_SCAN_H