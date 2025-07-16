#include "auth_handlers.h"
#include "json_utils.h"
#include "auth_session.h"
#include <esp_log.h>
#include <string.h>

static const char *TAG = "auth_handlers";

// Length of string with maximum uint32 number (10 digits + 1 for '\0')
#define UINT32_STR_MAX_LEN 11

esp_err_t auth_handlers_init(void)
{
    ESP_LOGI(TAG, "Authentication handlers initialized");
    return ESP_OK;
}

esp_err_t auth_set_session_cookie(httpd_req_t *req, uint32_t session_id)
{
    char cookie_header[AUTH_COOKIE_MAX_LEN];
    
    int written = snprintf(cookie_header, sizeof(cookie_header), "session_id=%lu", session_id);
    if (written <= 0 || written >= sizeof(cookie_header)) {
        ESP_LOGE(TAG, "Failed to format session cookie");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Setting session cookie: %s", cookie_header);
    
    if (httpd_resp_set_hdr(req, "Set-Cookie", cookie_header) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set session cookie header");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t auth_set_remember_cookie(httpd_req_t *req, const char *token)
{
    if (token == NULL || strlen(token) == 0) {
        ESP_LOGE(TAG, "Invalid remember token");
        return ESP_FAIL;
    }
    
    char remember_cookie[AUTH_REMEMBER_COOKIE_MAX_LEN];
    
    int written = snprintf(remember_cookie, sizeof(remember_cookie), 
                          "remember_token=%s; Max-Age=%d; HttpOnly", 
                          token, REMEMBER_TOKEN_LIFETIME_SEC);
    
    if (written <= 0 || written >= sizeof(remember_cookie)) {
        ESP_LOGE(TAG, "Remember cookie buffer too small or formatting error");
        return ESP_FAIL;
    }
    
    if (httpd_resp_set_hdr(req, "Set-Cookie", remember_cookie) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set remember cookie header");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Remember token set in cookie");
    return ESP_OK;
}

esp_err_t auth_clear_auth_cookies(httpd_req_t *req)
{
    // Clear session cookie
    if (httpd_resp_set_hdr(req, "Set-Cookie", "session_id=; Max-Age=0") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear session cookie");
        return ESP_FAIL;
    }
    
    // Clear remember token cookie
    if (httpd_resp_set_hdr(req, "Set-Cookie", "remember_token=; Max-Age=0") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear remember token cookie");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Authentication cookies cleared");
    return ESP_OK;
}

bool auth_middleware_check(httpd_req_t *req)
{
    return auth_check_request(req);
}

esp_err_t auth_process_remember_me(httpd_req_t *req, cJSON *remember_me_item)
{
    if (remember_me_item == NULL || !cJSON_IsTrue(remember_me_item)) {
        return ESP_OK; // Remember me not requested
    }
    
    uint32_t remember_session_id = auth_create_remember_token();
    if (remember_session_id == 0) {
        ESP_LOGE(TAG, "Failed to create remember token");
        return ESP_FAIL;
    }
    
    // Get the token from the auth module
    const char *token_str = auth_get_current_remember_token();
    if (token_str == NULL) {
        ESP_LOGE(TAG, "Failed to get remember token string");
        return ESP_FAIL;
    }
    
    // Validate token length
    if (strlen(token_str) >= (AUTH_REMEMBER_COOKIE_MAX_LEN - 50)) {
        ESP_LOGE(TAG, "Remember token too long");
        return ESP_FAIL;
    }
    
    return auth_set_remember_cookie(req, token_str);
}

esp_err_t auth_validate_credentials(cJSON *request_json, cJSON *response_json)
{
    if (request_json == NULL || response_json == NULL) {
        return ESP_FAIL;
    }
    
    // Check for required fields
    if (!cJSON_HasObjectItem(request_json, "login") || !cJSON_HasObjectItem(request_json, "pass")) {
        cJSON_AddStringToObject(response_json, "error", "No login or password in request");
        cJSON_AddBoolToObject(response_json, "auth", false);
        return ESP_FAIL;
    }
    
    cJSON *login_item = cJSON_GetObjectItem(request_json, "login");
    cJSON *pass_item = cJSON_GetObjectItem(request_json, "pass");
    
    // Validate field types
    if (!cJSON_IsString(login_item) || !cJSON_IsString(pass_item)) {
        cJSON_AddStringToObject(response_json, "error", "Invalid login or password type");
        cJSON_AddBoolToObject(response_json, "auth", false);
        return ESP_FAIL;
    }
    
    // Attempt authentication
    uint32_t session_id = auth_create_session(login_item->valuestring, pass_item->valuestring);
    if (session_id == 0) {
        cJSON_AddStringToObject(response_json, "error", "Invalid login or password");
        cJSON_AddBoolToObject(response_json, "auth", false);
        return ESP_FAIL;
    }
    
    // Store session ID for later use
    cJSON_AddNumberToObject(response_json, "_session_id", session_id);
    cJSON_AddBoolToObject(response_json, "auth", true);
    
    return ESP_OK;
}

esp_err_t auth_login_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Authentication request received");
    
    // Check content length
    if (req->content_len > JSON_UTILS_REQ_RECV_BUF_SIZE) {
        return json_utils_send_error(req, "Request too large");
    }
    
    // Receive JSON request
    cJSON *request_json = json_utils_receive_json(req);
    if (request_json == NULL) {
        return ESP_FAIL;
    }
    
    // Create response JSON
    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        json_utils_cleanup(request_json, NULL);
        return ESP_FAIL;
    }
    
    // Validate credentials
    esp_err_t auth_result = auth_validate_credentials(request_json, response_json);
    
    if (auth_result == ESP_OK) {
        // Get session ID from response
        cJSON *session_id_item = cJSON_GetObjectItem(response_json, "_session_id");
        if (session_id_item && cJSON_IsNumber(session_id_item)) {
            uint32_t session_id = (uint32_t)session_id_item->valueint;
            
            // Set session cookie
            if (auth_set_session_cookie(req, session_id) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set session cookie");
                cJSON_AddBoolToObject(response_json, "auth", false);
                cJSON_AddStringToObject(response_json, "error", "Failed to set session cookie");
            } else {
                // Process remember me functionality
                cJSON *remember_me_item = cJSON_GetObjectItem(request_json, "remember_me");
                if (auth_process_remember_me(req, remember_me_item) != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to process remember me, but continuing");
                }
            }
        }
        
        // Remove internal session ID from response
        cJSON_DeleteItemFromObject(response_json, "_session_id");
    }
    
    // Send response
    json_utils_send_response(req, request_json, response_json);
    return ESP_OK;
}

esp_err_t auth_logout_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Logout request received");
    
    // Get session ID from cookie and remove it
    uint32_t session_id = auth_get_session_from_cookie(req);
    if (session_id != 0) {
        auth_remove_session(session_id);
    } else {
        ESP_LOGW(TAG, "Session ID cookie not found during logout");
    }
    
    // Clear remember token
    auth_clear_remember_token();
    
    // Clear authentication cookies
    auth_clear_auth_cookies(req);
    
    // Create success response
    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create logout response JSON");
        return ESP_FAIL;
    }
    
    cJSON_AddBoolToObject(response_json, "success", true);
    json_utils_send_response(req, NULL, response_json);
    
    return ESP_OK;
}

esp_err_t auth_session_check_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Session check request received");
    
    if (!auth_middleware_check(req)) {
        return ESP_OK; // auth_check_request handles the response
    }
    
    // Send empty response with 200 status if user is authenticated
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}