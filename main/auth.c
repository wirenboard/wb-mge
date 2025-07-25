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

#define MAX_SESSIONS 10
#define REMEMBER_TOKEN_LEN 64
#define REMEMBER_TOKEN_LIFETIME_SEC (30 * 24 * 60 * 60) // 30 days

// Simplified state management
static uint32_t sessions[MAX_SESSIONS] = {0};
static char remember_token[REMEMBER_TOKEN_LEN] = {0};
static bool token_valid = false;

// === UTILITY FUNCTIONS ===

static esp_err_t set_cookie(httpd_req_t *req, const char *name, const char *value, int max_age)
{
    char cookie[256];
    if (max_age > 0) {
        snprintf(cookie, sizeof(cookie), "%s=%s; Max-Age=%d; HttpOnly", name, value, max_age);
    } else if (max_age == 0) {
        snprintf(cookie, sizeof(cookie), "%s=; Max-Age=0", name);
    } else {
        snprintf(cookie, sizeof(cookie), "%s=%s", name, value);
    }
    return httpd_resp_set_hdr(req, "Set-Cookie", cookie);
}

static uint32_t get_session_from_cookie(httpd_req_t *req)
{
    char buf[32];
    size_t len = sizeof(buf);
    if (httpd_req_get_cookie_val(req, "session_id", buf, &len) == ESP_OK) {
        return strtoul(buf, NULL, 10);
    }
    return 0;
}

static char* get_remember_token_from_cookie(httpd_req_t *req)
{
    static char buf[REMEMBER_TOKEN_LEN];
    size_t len = sizeof(buf);
    if (httpd_req_get_cookie_val(req, "remember_token", buf, &len) == ESP_OK) {
        return buf;
    }
    return NULL;
}

// === SESSION MANAGEMENT ===

static uint32_t create_session(const char *login, const char *pass)
{
    char stored_login[SETTING_ITEM_MAX_STR_LEN] = {0};
    char stored_pass[SETTING_ITEM_MAX_STR_LEN] = {0};

    if (setting_items_read(KEY_LOGIN, stored_login) != ESP_OK ||
        setting_items_read(KEY_PASS, stored_pass) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read credentials from storage");
        return 0;
    }

    if (strcmp(login, stored_login) != 0 || strcmp(pass, stored_pass) != 0) {
        ESP_LOGW(TAG, "Invalid login or password");
        return 0;
    }

    uint32_t session_id = esp_random();
    if (session_id == 0) session_id = esp_random(); // Avoid 0

    // Find empty slot or overwrite oldest
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i] == 0) {
            sessions[i] = session_id;
            ESP_LOGI(TAG, "Session created: %lu", session_id);
            return session_id;
        }
    }

    // Overwrite first session if all slots full
    sessions[0] = session_id;
    ESP_LOGI(TAG, "Session created (overwritten): %lu", session_id);
    return session_id;
}

static bool is_session_valid(uint32_t session_id)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i] == session_id) {
            return true;
        }
    }
    return false;
}

static void remove_session(uint32_t session_id)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i] == session_id) {
            sessions[i] = 0;
            ESP_LOGI(TAG, "Session removed: %lu", session_id);
            return;
        }
    }
}

// === REMEMBER TOKEN MANAGEMENT ===

static void generate_remember_token(void)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < REMEMBER_TOKEN_LEN - 1; i++) {
        remember_token[i] = charset[esp_random() % (sizeof(charset) - 1)];
    }
    remember_token[REMEMBER_TOKEN_LEN - 1] = '\0';
    token_valid = true;
}

static esp_err_t save_remember_token_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("http_server", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(nvs_handle, "remember_token", remember_token);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    return err;
}

static esp_err_t load_remember_token_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("http_server", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return err;

    size_t len = REMEMBER_TOKEN_LEN;
    err = nvs_get_str(nvs_handle, "remember_token", remember_token, &len);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        token_valid = true;
        ESP_LOGI(TAG, "Remember token loaded from NVS");
    }
    return err;
}

static void clear_remember_token(void)
{
    memset(remember_token, 0, REMEMBER_TOKEN_LEN);
    token_valid = false;

    nvs_handle_t nvs_handle;
    if (nvs_open("http_server", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_erase_key(nvs_handle, "remember_token");
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    ESP_LOGI(TAG, "Remember token cleared");
}

static uint32_t create_remember_token(void)
{
    generate_remember_token();
    if (save_remember_token_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save remember token");
        token_valid = false;
        return 0;
    }
    return create_session("", ""); // Create session without credential check
}

static uint32_t validate_remember_token(const char *token)
{
    if (!token_valid || !token || strcmp(remember_token, token) != 0) {
        return 0;
    }

    // Create new session for valid remember token
    uint32_t session_id = esp_random();
    if (session_id == 0) session_id = esp_random();

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i] == 0) {
            sessions[i] = session_id;
            ESP_LOGI(TAG, "Session created from remember token: %lu", session_id);
            return session_id;
        }
    }

    sessions[0] = session_id; // Overwrite if full
    return session_id;
}

// === PUBLIC API ===

esp_err_t auth_init(void)
{
    ESP_LOGI(TAG, "Initializing authentication");
    load_remember_token_from_nvs(); // Ignore errors
    return ESP_OK;
}

bool auth_middleware_check(httpd_req_t *req)
{
    // Check session cookie first
    uint32_t session_id = get_session_from_cookie(req);
    if (session_id && is_session_valid(session_id)) {
        return true;
    }

    // Check remember token
    char *token = get_remember_token_from_cookie(req);
    if (token) {
        uint32_t new_session_id = validate_remember_token(token);
        if (new_session_id) {
            // Set new session cookie
            char session_str[32];
            snprintf(session_str, sizeof(session_str), "%lu", new_session_id);
            set_cookie(req, "session_id", session_str, -1);
            return true;
        }
    }

    // Unauthorized
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
    cJSON *remember_me = cJSON_GetObjectItem(request_json, "remember_me");

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

            // Set session cookie
            char session_str[32];
            snprintf(session_str, sizeof(session_str), "%lu", session_id);
            set_cookie(req, "session_id", session_str, -1);

            // Handle remember me
            if (cJSON_IsTrue(remember_me)) {
                generate_remember_token();
                save_remember_token_to_nvs();
                set_cookie(req, "remember_token", remember_token, REMEMBER_TOKEN_LIFETIME_SEC);
                ESP_LOGI(TAG, "Remember token set");
            }
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

    // Remove session
    uint32_t session_id = get_session_from_cookie(req);
    if (session_id) {
        remove_session(session_id);
    }

    // Clear remember token
    clear_remember_token();

    // Clear cookies
    set_cookie(req, "session_id", "", 0);
    set_cookie(req, "remember_token", "", 0);

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
        return ESP_OK; // Already sent 401
    }

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}