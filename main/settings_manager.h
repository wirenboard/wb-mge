#pragma once

#include <esp_http_server.h>
#include "setting_items.h"

// Forward declaration — consumers that call settings_process_request_json or
// settings_build_response_json must include <cJSON.h> themselves.
struct cJSON;
typedef struct cJSON cJSON;

// HTTP handler for settings GET endpoint
esp_err_t settings_get_handler(httpd_req_t *req);
// HTTP handler for settings POST endpoint
esp_err_t settings_post_handler(httpd_req_t *req);

// Process a parsed settings request JSON and produce a response JSON.
// Both request_json and response_json must be non-NULL.
// Returns ESP_OK in all cases where a JSON response was produced (including
// validation errors); the caller must inspect the "success" field in the
// response to determine whether the settings were applied.
esp_err_t settings_process_request_json(cJSON *request_json, cJSON **response_json);

// Build the full settings response JSON (used by the GET handler).
esp_err_t settings_build_response_json(cJSON **response_json);

// Returns ESP_OK if switching RS-485 port `port_index` (0-based) to `new_port_mode`
// introduces no new local TCP listener collision, ESP_ERR_INVALID_STATE if it does.
// Mirrors the collision check POST /settings runs, so POST /ports/{n}/mode rejects the
// same conflicts up front instead of failing later at bind().
esp_err_t settings_manager_check_port_mode_collision(unsigned port_index, const char *new_port_mode);
