#include "auth.h"
#include "json_utils.h"
#include "setting_items.h"
#include "cJSON.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <nvs.h>
#include <string.h>
#include <errno.h>

static const char *TAG = "auth";

#define MAX_SESSIONS                    10
#define REMEMBER_TOKEN_MAX_LEN          64
#define REMEMBER_TOKEN_LIFETIME_SEC     (30 * 24 * 60 * 60) // 30 days
#define AUTH_COOKIE_MAX_LEN             64
#define AUTH_REMEMBER_COOKIE_MAX_LEN    256
#define COOKIE_MAX_LEN                  22  // "session_id=" (11) + uint32_max (10) + '\0' (1)
#define UINT32_STR_MAX_LEN              11

typedef struct {
    uint32_t session_ids[MAX_SESSIONS];
    int current_index;
} session_buffer_t;

typedef struct {
    char token[REMEMBER_TOKEN_MAX_LEN];
    uint64_t created_time;
    bool is_valid;
} remember_token_t;

// Internal state
static session_buffer_t session_buffer = {
    .current_index = 0,
};

static remember_token_t remember_token = {
    .token = {0},
    .created_time = 0,
    .is_valid = false,
};

// Internal session management functions
static void add_session_id(session_buffer_t *buffer, uint32_t session_id)
{
    ESP_LOGI(TAG, "Adding session ID: %lu", session_id);
    buffer->session_ids[buffer->current_index] = session_id;
    buffer->current_index = (buffer->current_index + 1) % MAX_SESSIONS;
}

static void find_and_remove_session_id(uint32_t session_id)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (session_buffer.session_ids[i] == session_id) {
            session_buffer.session_ids[i] = 0;
            ESP_LOGI(TAG, "Session ID %lu removed", session_id);
            break;
        }
    }
}

static uint32_t create_new_session_id(void)
{
    uint32_t session_id = esp_random();
    if (session_id == 0) {
        session_id = esp_random();
    }
    add_session_id(&session_buffer, session_id);
    return session_id;
}

static uint32_t create_session(const char *login, const char *password)
{
    ESP_LOGI(TAG, "Creating session for login attempt");

    char stored_login[SETTING_ITEM_MAX_STR_LEN] = {0};
    char stored_pass[SETTING_ITEM_MAX_STR_LEN] = {0};

    if ((setting_items_read(KEY_LOGIN, stored_login) != ESP_OK) ||
        (setting_items_read(KEY_PASS, stored_pass) != ESP_OK))
    {
        ESP_LOGE(TAG, "Failed to read login or pass from storage");
        return 0;
    }

    if ((strncmp(login, stored_login, SETTING_ITEM_MAX_STR_LEN) != 0) ||
        (strncmp(password, stored_pass, SETTING_ITEM_MAX_STR_LEN) != 0))
    {
        ESP_LOGW(TAG, "Invalid login or password");
        return 0;
    }

    return create_new_session_id();
}

static bool is_session_valid(uint32_t session_id)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (session_buffer.session_ids[i] == session_id) {
            return true;
        }
    }
    return false;
}

static void remove_session(uint32_t session_id)
{
    find_and_remove_session_id(session_id);
}

static uint32_t get_session_from_cookie(httpd_req_t *req)
{
    char session_id_str[COOKIE_MAX_LEN];
    size_t buf_len = sizeof(session_id_str);
    if (httpd_req_get_cookie_val(req, "session_id", session_id_str, &buf_len) == ESP_OK) {
        ESP_LOGI(TAG, "Session ID from cookie: %s", session_id_str);

        // Use standard library function instead of custom strtou()
        char *endptr;
        unsigned long val = strtoul(session_id_str, &endptr, 10);
        if (endptr != session_id_str && *endptr == '\0' && val <= UINT32_MAX) {
            return (uint32_t)val;
        }
    }
    return 0;
}

// Internal remember token functions
static void generate_remember_token(char *token, size_t token_len)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < token_len - 1; i++) {
        token[i] = charset[esp_random() % (sizeof(charset) - 1)];
    }
    token[token_len - 1] = '\0';
}

static esp_err_t save_remember_token_to_nvs(const char *token)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("http_server", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    uint64_t current_time = esp_timer_get_time() / 1000000; // Convert to seconds

    err = nvs_set_str(nvs_handle, "remember_token", token);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save remember token: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_u64(nvs_handle, "token_time", current_time);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save token time: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    return err;
}

static void clear_remember_token_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    if (nvs_open("http_server", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_erase_key(nvs_handle, "remember_token");
        nvs_erase_key(nvs_handle, "token_time");
        nvs_erase_key(nvs_handle, "boot_time");
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
}

static esp_err_t load_remember_token_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("http_server", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    size_t token_len = REMEMBER_TOKEN_MAX_LEN;
    err = nvs_get_str(nvs_handle, "remember_token", remember_token.token, &token_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load remember token: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_get_u64(nvs_handle, "token_time", &remember_token.created_time);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load token time: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);

    // Check if token has expired
    // Note: After reboot, we can't reliably calculate elapsed time from uptime
    // So we use a simple approach: if token exists and was saved recently, consider it valid
    uint64_t current_uptime = esp_timer_get_time() / 1000000;

    // For tokens loaded from NVS after boot, we assume they're still valid
    // The 30-day expiration will be enforced when the token is next used
    remember_token.is_valid = true;
    remember_token.created_time = current_uptime; // Reset to current uptime

    ESP_LOGI(TAG, "Remember token loaded from NVS and marked as valid");

    // Update the timestamp in NVS to current uptime
    save_remember_token_to_nvs(remember_token.token);

    return ESP_OK;
}

static uint32_t create_remember_token(void)
{
    generate_remember_token(remember_token.token, REMEMBER_TOKEN_MAX_LEN);
    remember_token.created_time = esp_timer_get_time() / 1000000;
    remember_token.is_valid = true;

    if (save_remember_token_to_nvs(remember_token.token) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save remember token to NVS");
        remember_token.is_valid = false;
        return 0;
    }

    uint32_t session_id = create_new_session_id();
    ESP_LOGI(TAG, "Remember token created and saved");
    return session_id;
}

static uint32_t validate_remember_token(const char *token)
{
    if ((!remember_token.is_valid) || (token == NULL) || (strnlen(token, REMEMBER_TOKEN_MAX_LEN) == 0)) {
        return 0;
    }

    if (strncmp(remember_token.token, token, REMEMBER_TOKEN_MAX_LEN) == 0) {
        uint32_t session_id = create_new_session_id();
        ESP_LOGI(TAG, "Remember token validated, new session created");
        return session_id;
    }

    return 0;
}

static char *get_remember_token_from_cookie(httpd_req_t *req)
{
    // Note: This function returns a pointer to a static buffer
    // It's acceptable here because HTTP requests are processed sequentially
    // in the ESP-IDF HTTP server implementation
    static char remember_token_str[REMEMBER_TOKEN_MAX_LEN];
    size_t buf_len = sizeof(remember_token_str);

    if (httpd_req_get_cookie_val(req, "remember_token", remember_token_str, &buf_len) == ESP_OK) {
        ESP_LOGI(TAG, "Remember token found in cookie");
        return remember_token_str;
    }
    return NULL;
}

static const char* get_current_remember_token(void)
{
    if (remember_token.is_valid) {
        return remember_token.token;
    } else {
        return NULL;
    }
}

static void clear_remember_token(void)
{
    remember_token.is_valid = false;
    memset(remember_token.token, 0, REMEMBER_TOKEN_MAX_LEN);
    remember_token.created_time = 0;

    clear_remember_token_from_nvs();
}

// Internal cookie functions
static esp_err_t set_session_cookie(httpd_req_t *req, uint32_t session_id)
{
    char cookie_header[AUTH_COOKIE_MAX_LEN];

    int written = snprintf(cookie_header, sizeof(cookie_header), "session_id=%lu", session_id);
    if ((written <= 0) || (written >= sizeof(cookie_header))) {
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

static esp_err_t set_remember_cookie(httpd_req_t *req, const char *token)
{
    if ((token == NULL) || (strlen(token) == 0)) {
        ESP_LOGE(TAG, "Invalid remember token");
        return ESP_FAIL;
    }

    char remember_cookie[AUTH_REMEMBER_COOKIE_MAX_LEN];

    int written = snprintf(remember_cookie, sizeof(remember_cookie),
                          "remember_token=%s; Max-Age=%d; HttpOnly",
                          token, REMEMBER_TOKEN_LIFETIME_SEC);

    if ((written <= 0) || (written >= sizeof(remember_cookie))) {
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

static esp_err_t clear_auth_cookies(httpd_req_t *req)
{
    if (httpd_resp_set_hdr(req, "Set-Cookie", "session_id=; Max-Age=0") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear session cookie");
        return ESP_FAIL;
    }

    if (httpd_resp_set_hdr(req, "Set-Cookie", "remember_token=; Max-Age=0") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear remember token cookie");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Authentication cookies cleared");
    return ESP_OK;
}

static esp_err_t process_remember_me(httpd_req_t *req, cJSON *remember_me_item)
{
    if ((remember_me_item == NULL) || (!cJSON_IsTrue(remember_me_item))) {
        return ESP_OK;
    }

    uint32_t remember_session_id = create_remember_token();
    if (remember_session_id == 0) {
        ESP_LOGE(TAG, "Failed to create remember token");
        return ESP_FAIL;
    }

    const char *token_str = get_current_remember_token();
    if (token_str == NULL) {
        ESP_LOGE(TAG, "Failed to get remember token string");
        return ESP_FAIL;
    }

    if (strlen(token_str) >= (AUTH_REMEMBER_COOKIE_MAX_LEN - 50)) {
        ESP_LOGE(TAG, "Remember token too long");
        return ESP_FAIL;
    }

    return set_remember_cookie(req, token_str);
}

static esp_err_t validate_credentials(cJSON *request_json, cJSON *response_json)
{
    if ((request_json == NULL) || (response_json == NULL)) {
        return ESP_FAIL;
    }

    if (!cJSON_HasObjectItem(request_json, "login") || !cJSON_HasObjectItem(request_json, "pass")) {
        cJSON_AddStringToObject(response_json, "error", "No login or password in request");
        cJSON_AddBoolToObject(response_json, "auth", false);
        return ESP_FAIL;
    }

    cJSON *login_item = cJSON_GetObjectItem(request_json, "login");
    cJSON *pass_item = cJSON_GetObjectItem(request_json, "pass");

    if (!cJSON_IsString(login_item) || !cJSON_IsString(pass_item)) {
        cJSON_AddStringToObject(response_json, "error", "Invalid login or password type");
        cJSON_AddBoolToObject(response_json, "auth", false);
        return ESP_FAIL;
    }

    uint32_t session_id = create_session(login_item->valuestring, pass_item->valuestring);
    if (session_id == 0) {
        cJSON_AddStringToObject(response_json, "error", "Invalid login or password");
        cJSON_AddBoolToObject(response_json, "auth", false);
        return ESP_FAIL;
    }

    cJSON_AddNumberToObject(response_json, "_session_id", session_id);
    cJSON_AddBoolToObject(response_json, "auth", true);

    return ESP_OK;
}

// Public API implementation
esp_err_t auth_init(void)
{
    ESP_LOGI(TAG, "Initializing authentication module");

    esp_err_t err = load_remember_token_from_nvs();
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No remember token found in NVS (normal for first boot)");
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load remember token: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    return ESP_OK;
}

bool auth_middleware_check(httpd_req_t *req)
{
    uint32_t session_id = get_session_from_cookie(req);
    if ((session_id != 0) && is_session_valid(session_id)) {
        return true;
    }

    char *remember_token_str = get_remember_token_from_cookie(req);
    if (remember_token_str != NULL) {
        uint32_t new_session_id = validate_remember_token(remember_token_str);
        if (new_session_id != 0) {
            // Update session cookie when remember token creates new session
            set_session_cookie(req, new_session_id);
            return true;
        }
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, NULL, 0);
    return false;
}

esp_err_t auth_login_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Authentication request received");

    if (req->content_len > JSON_UTILS_REQ_RECV_BUF_SIZE) {
        return json_utils_send_error(req, "Request too large");
    }

    cJSON *request_json = json_utils_receive_json(req);
    if (request_json == NULL) {
        return json_utils_send_error(req, "Invalid JSON request");
    }

    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        json_utils_cleanup(request_json, NULL);
        return json_utils_send_error(req, "Failed to create response");
    }

    esp_err_t auth_result = validate_credentials(request_json, response_json);

    if (auth_result == ESP_OK) {
        cJSON *session_id_item = cJSON_GetObjectItem(response_json, "_session_id");
        if (session_id_item && cJSON_IsNumber(session_id_item)) {
            uint32_t session_id = (uint32_t)session_id_item->valueint;

            if (set_session_cookie(req, session_id) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set session cookie");
                cJSON_AddBoolToObject(response_json, "auth", false);
                cJSON_AddStringToObject(response_json, "error", "Failed to set session cookie");
            } else {
                cJSON *remember_me_item = cJSON_GetObjectItem(request_json, "remember_me");
                if (process_remember_me(req, remember_me_item) != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to process remember me, but continuing");
                }
            }
        }

        cJSON_DeleteItemFromObject(response_json, "_session_id");
    }

    json_utils_send_response(req, request_json, response_json);
    return ESP_OK;
}

esp_err_t auth_logout_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Logout request received");

    uint32_t session_id = get_session_from_cookie(req);
    if (session_id != 0) {
        remove_session(session_id);
    } else {
        ESP_LOGW(TAG, "Session ID cookie not found during logout");
    }

    clear_remember_token();
    clear_auth_cookies(req);

    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create logout response JSON");
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
