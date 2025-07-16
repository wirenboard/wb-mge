#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <esp_http_server.h>
#include "cJSON.h"
#include "setting_items.h"

// Forward declarations
typedef struct setting_mapping_s setting_mapping_t;
typedef struct setting_group_s setting_group_t;

// Validation function pointer type
typedef bool (*setting_validator_t)(const void *value);

/**
 * @brief Setting mapping structure
 */
struct setting_mapping_s {
    const char *json_key;           // Key in JSON request/response
    const char *setting_key;        // Key in settings storage
    setting_item_type_t type;       // Type of the setting
    setting_validator_t validator;  // Optional validation function
    bool required;                  // Whether this setting is required
};

/**
 * @brief Setting group structure
 */
struct setting_group_s {
    const char *group_name;         // Name of the group (e.g., "wifi", "ethernet")
    const setting_mapping_t *mappings; // Array of setting mappings
    size_t mapping_count;           // Number of mappings in the array
    bool is_top_level;              // Whether this group is at top level
};

/**
 * @brief Initialize settings manager module
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t settings_manager_init(void);

/**
 * @brief HTTP handler for settings GET endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t settings_get_handler(httpd_req_t *req);

/**
 * @brief HTTP handler for settings POST endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t settings_post_handler(httpd_req_t *req);

/**
 * @brief Build settings response JSON
 * @param response_json Pointer to store the response JSON object
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t settings_build_response_json(cJSON **response_json);

/**
 * @brief Process settings request JSON
 * @param request_json Request JSON object
 * @param response_json Pointer to store the response JSON object
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t settings_process_request_json(cJSON *request_json, cJSON **response_json);

/**
 * @brief Validate hostname string
 * @param hostname Hostname string to validate
 * @return true if valid, false otherwise
 */
bool settings_validate_hostname(const char *hostname);

/**
 * @brief Validate port number
 * @param port Port number to validate
 * @return true if valid, false otherwise
 */
bool settings_validate_port(int port);

/**
 * @brief Get setting groups array
 * @return Array of setting groups
 */
const setting_group_t *settings_get_groups(void);

/**
 * @brief Get number of setting groups
 * @return Number of setting groups
 */
size_t settings_get_group_count(void);

#endif // SETTINGS_MANAGER_H