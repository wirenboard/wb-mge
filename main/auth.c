#include "auth.h"
#include "json_utils.h"
#include "setting_items.h"
#include "cJSON.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <nvs.h>
#include <string.h>

static const char *TAG = "auth";

static esp_err_t set_cookie(httpd_req_t *req, const char *name, const char *value, int max_age)
{
    char cookie[256];
    if (max_age > 0) {
        snprintf(cookie, sizeof(cookie), "%s=%s; Max-Age=%d; Path=/; HttpOnly", name, value, max_age);
    } else if (max_age == 0) {
        snprintf(cookie, sizeof(cookie), "%s=; Max-Age=0; Path=/", name);
    } else {
        snprintf(cookie, sizeof(cookie), "%s=%s; Path=/", name, value);
    }
    return httpd_resp_set_hdr(req, "Set-Cookie", cookie);
}

static uint32_t get_session_from_cookie(httpd_req_t *req)
{
    char buf[32];
    size_t len = sizeof(buf);
    if (httpd_req_get_cookie_val(req, "session_id", buf, &len) == ESP_OK) {
        // Ensure null termination for safety
        if (len < sizeof(buf)) {
            buf[len] = '\0';
        } else {
            buf[sizeof(buf) - 1] = '\0';
        }
        return strtoul(buf, NULL, 10);
    }
    return 0;
}

static uint32_t create_session(const char *login, const char *pass)
{
    char stored_login[SETTING_ITEM_MAX_STR_LEN] = {0};
    char stored_pass[SETTING_ITEM_MAX_STR_LEN] = {0};

    if (setting_items_read(KEY_LOGIN, stored_login) != ESP_OK ||
        setting_items_read(KEY_PASS, stored_pass) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read credentials from storage");
        return 0;
    }

    if ((strcmp(login, stored_login) != 0) || (strcmp(pass, stored_pass) != 0)) {
        ESP_LOGW(TAG, "Invalid login or password");
        return 0;
    }

    uint32_t session_id = esp_random();
    if (session_id == 0) {
        session_id = esp_random(); // Avoid 0
    }
    ESP_LOGI(TAG, "Session created: %lu", session_id);
    return session_id;
}

static bool is_session_valid(uint32_t session_id)
{
    return session_id != 0;
}

static void remove_session(uint32_t session_id)
{
    ESP_LOGI(TAG, "Session removed: %lu (noop)", session_id);
}

// === PUBLIC API ===

esp_err_t auth_init(void)
{
    ESP_LOGI(TAG, "Initializing authentication");
    return ESP_OK;
}

bool auth_middleware_check(httpd_req_t *req)
{
    uint32_t session_id = get_session_from_cookie(req);
    if (session_id && is_session_valid(session_id)) {
        return true;
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, NULL, 0);
    return false;
}

esp_err_t auth_login_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Login request received");

    cJSON *request_json = json_utils_receive_json(req);
    if (!request_json) {
        return json_utils_send_error(req, "Invalid JSON");
    }

    cJSON *login = cJSON_GetObjectItem(request_json, "login");
    cJSON *pass = cJSON_GetObjectItem(request_json, "pass");

    cJSON *response_json = cJSON_CreateObject();
    if (!response_json) {
        json_utils_cleanup(request_json, NULL);
        return json_utils_send_error(req, "Failed to create response");
    }

    if (!cJSON_IsString(login) || !cJSON_IsString(pass)) {
        cJSON_AddBoolToObject(response_json, "auth", false);
        cJSON_AddStringToObject(response_json, "error", "Invalid login or password format");
    } else {
        uint32_t session_id = create_session(login->valuestring, pass->valuestring);
        if (session_id) {
            cJSON_AddBoolToObject(response_json, "auth", true);
            char session_str[32];
            snprintf(session_str, sizeof(session_str), "%lu", session_id);
            set_cookie(req, "session_id", session_str, -1);
        } else {
            cJSON_AddBoolToObject(response_json, "auth", false);
            cJSON_AddStringToObject(response_json, "error", "Invalid login or password");
        }
    }

    json_utils_send_response(req, request_json, response_json);
    return ESP_OK;
}

esp_err_t auth_logout_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Logout request received");
    uint32_t session_id = get_session_from_cookie(req);
    if (session_id) {
        remove_session(session_id);
    }
    set_cookie(req, "session_id", "", 0);
    cJSON *response_json = cJSON_CreateObject();
    if (!response_json) {
        return json_utils_send_error(req, "Failed to create response");
    }
    cJSON_AddBoolToObject(response_json, "success", true);
    json_utils_send_response(req, NULL, response_json);
    return ESP_OK;
}

esp_err_t auth_session_check_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Session check request received");
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}