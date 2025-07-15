#include "auth_session.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "setting_items.h"
#include <string.h>
#include <errno.h>

// Максимальная длина cookie с идентификатором сессии (session_id=<u32_id>)
#define COOKIE_MAX_LEN          22  // "session_id=" (11) + uint32_max (10) + '\0' (1)
#define UINT32_STR_MAX_LEN      11

typedef struct {
    uint32_t session_ids[MAX_SESSIONS];
    int current_index;
} session_buffer_t;

typedef struct {
    char token[REMEMBER_TOKEN_MAX_LEN];
    uint64_t created_time;
    bool is_valid;
} remember_token_t;

static const char *TAG = "auth_session";

static session_buffer_t session_buffer = {
    .current_index = 0,
};

static remember_token_t remember_token = {
    .token = {0},
    .created_time = 0,
    .is_valid = false,
};

// Session management functions
static void add_session_id(session_buffer_t *buffer, uint32_t session_id)
{
    ESP_LOGI(TAG, "%s", __func__);
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

static uint32_t strtou(const char *u32_str)
{
    if (u32_str == NULL) {
        ESP_LOGE(TAG, "%s: String is NULL", __func__);
        return 0;
    }
    if (strnlen(u32_str, (UINT32_STR_MAX_LEN + 1)) > UINT32_STR_MAX_LEN) {
        ESP_LOGE(TAG, "%s: String is too long", __func__);
        return 0;
    }

    char *endptr;
    errno = 0;
    uint64_t value = strtoul(u32_str, &endptr, 10);

    if ((errno == ERANGE) || (value > UINT32_MAX)) {
        ESP_LOGE(TAG, "%s: Overflow occurred", __func__);
        return 0;
    }
    if (endptr == u32_str) {
        ESP_LOGE(TAG, "%s: No digits were found", __func__);
        return 0;
    }
    if (*endptr != '\0') {
        ESP_LOGE(TAG, "%s: Further characters after number: %s", __func__, endptr);
        return 0;
    }

    return (uint32_t)value;
}

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

    // Store current uptime as boot time reference
    err = nvs_set_u64(nvs_handle, "boot_time", current_time);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save boot time: %s", esp_err_to_name(err));
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    return err;
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

    // Get current uptime and stored device boot time
    uint64_t current_uptime = esp_timer_get_time() / 1000000; // Convert to seconds
    uint64_t stored_boot_time = 0;
    
    // Try to get stored boot time from NVS
    nvs_handle_t boot_handle;
    if (nvs_open("http_server", NVS_READONLY, &boot_handle) == ESP_OK) {
        nvs_get_u64(boot_handle, "boot_time", &stored_boot_time);
        nvs_close(boot_handle);
    }

    // Calculate elapsed time since token creation
    uint64_t elapsed_time;
    if (current_uptime >= remember_token.created_time) {
        // Normal case: no reboot occurred
        elapsed_time = current_uptime - remember_token.created_time;
    } else {
        // Device was rebooted: calculate real elapsed time
        // elapsed_time = time_before_reboot + time_after_reboot
        elapsed_time = (stored_boot_time - remember_token.created_time) + current_uptime;
    }

    // Check if token is still valid
    if (elapsed_time < REMEMBER_TOKEN_LIFETIME_SEC) {
        remember_token.is_valid = true;
        ESP_LOGI(TAG, "Remember token loaded and is valid (elapsed: %llu seconds)", elapsed_time);
        
        // Update token creation time to current uptime for future calculations
        remember_token.created_time = current_uptime;
        save_remember_token_to_nvs(remember_token.token);
    } else {
        ESP_LOGW(TAG, "Remember token expired (elapsed: %llu seconds)", elapsed_time);
        remember_token.is_valid = false;
        // Clear expired token from NVS
        nvs_handle_t nvs_handle_write;
        if (nvs_open("http_server", NVS_READWRITE, &nvs_handle_write) == ESP_OK) {
            nvs_erase_key(nvs_handle_write, "remember_token");
            nvs_erase_key(nvs_handle_write, "token_time");
            nvs_erase_key(nvs_handle_write, "boot_time");
            nvs_commit(nvs_handle_write);
            nvs_close(nvs_handle_write);
        }
    }

    return ESP_OK;
}

static uint32_t create_remember_token(void)
{
    generate_remember_token(remember_token.token, REMEMBER_TOKEN_MAX_LEN);
    remember_token.created_time = esp_timer_get_time() / 1000000; // Convert to seconds
    remember_token.is_valid = true;

    if (save_remember_token_to_nvs(remember_token.token) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save remember token to NVS");
        remember_token.is_valid = false;
        return 0;
    }

    // Create new session for this token
    uint32_t session_id = esp_random();
    if (session_id == 0) {
        session_id = esp_random();
    }
    add_session_id(&session_buffer, session_id);

    ESP_LOGI(TAG, "Remember token created and saved");
    return session_id;
}

static uint32_t validate_remember_token(const char *token)
{
    if (!remember_token.is_valid || strlen(token) == 0) {
        return 0;
    }

    if (strncmp(remember_token.token, token, REMEMBER_TOKEN_MAX_LEN) == 0) {
        // Token is valid, create new session
        uint32_t session_id = esp_random();
        if (session_id == 0) {
            session_id = esp_random();
        }
        add_session_id(&session_buffer, session_id);
        ESP_LOGI(TAG, "Remember token validated, new session created");
        return session_id;
    }

    return 0;
}

static char *get_remember_token_from_cookie(httpd_req_t *req)
{
    static char remember_token_str[REMEMBER_TOKEN_MAX_LEN];
    size_t buf_len = sizeof(remember_token_str);
    
    if (httpd_req_get_cookie_val(req, "remember_token", remember_token_str, &buf_len) == ESP_OK) {
        ESP_LOGI(TAG, "Remember token found in cookie");
        return remember_token_str;
    }
    return NULL;
}

static void clear_remember_token(void)
{
    remember_token.is_valid = false;
    memset(remember_token.token, 0, REMEMBER_TOKEN_MAX_LEN);
    remember_token.created_time = 0;

    // Clear from NVS
    nvs_handle_t nvs_handle;
    if (nvs_open("http_server", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_erase_key(nvs_handle, "remember_token");
        nvs_erase_key(nvs_handle, "token_time");
        nvs_erase_key(nvs_handle, "boot_time");
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
}

static void store_boot_time_reference(void)
{
    nvs_handle_t nvs_handle;
    if (nvs_open("http_server", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        uint64_t current_uptime = esp_timer_get_time() / 1000000;
        nvs_set_u64(nvs_handle, "boot_time", current_uptime);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Boot time reference stored: %llu", current_uptime);
    }
}

// Public API implementations
esp_err_t auth_session_init(void)
{
    ESP_LOGI(TAG, "Initializing authentication session module");
    
    // Store boot time reference for remember token calculations
    store_boot_time_reference();
    
    // Try to load remember token from NVS if it exists
    // This is optional - if no token exists, that's normal
    esp_err_t err = load_remember_token_from_nvs();
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No remember token found in NVS (normal for first boot)");
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load remember token: %s", esp_err_to_name(err));
        return ESP_OK;  // Still continue initialization
    }
    
    return ESP_OK;
}

uint32_t auth_create_session(const char *login, const char *password)
{
    ESP_LOGI(TAG, "%s", __func__);

    char stored_login[SETTING_ITEM_MAX_STR_LEN] = {0};
    char stored_pass[SETTING_ITEM_MAX_STR_LEN] = {0};

    if ((setting_items_read_raw("login", stored_login, SETTING_ITEM_TYPE_STR) != 0) ||
        (setting_items_read_raw("pass", stored_pass, SETTING_ITEM_TYPE_STR) != 0))
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

    uint32_t session_id = esp_random();
    if (session_id == 0) {
        session_id = esp_random();  // Повторная попытка генерации session_id
    }
    add_session_id(&session_buffer, session_id);

    return session_id;
}

bool auth_is_session_valid(uint32_t session_id)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (session_buffer.session_ids[i] == session_id) {
            return true;
        }
    }
    return false;
}

void auth_remove_session(uint32_t session_id)
{
    find_and_remove_session_id(session_id);
}

uint32_t auth_get_session_from_cookie(httpd_req_t *req)
{
    char session_id_str[COOKIE_MAX_LEN];
    size_t buf_len = sizeof(session_id_str);
    if (httpd_req_get_cookie_val(req, "session_id", session_id_str, &buf_len) == ESP_OK) {
        ESP_LOGI(TAG, "%s: %s", __func__, session_id_str);
        return strtou(session_id_str);
    }
    return 0;  // Возвращает 0, если не удалось получить session_id
}

uint32_t auth_create_remember_token(void)
{
    return create_remember_token();
}

uint32_t auth_validate_remember_token(const char *token)
{
    return validate_remember_token(token);
}

char* auth_get_remember_token_from_cookie(httpd_req_t *req)
{
    return get_remember_token_from_cookie(req);
}

const char* auth_get_current_remember_token(void)
{
    return remember_token.is_valid ? remember_token.token : NULL;
}

void auth_clear_remember_token(void)
{
    clear_remember_token();
}

bool auth_check_request(httpd_req_t *req)
{
    uint32_t session_id = auth_get_session_from_cookie(req);
    if (session_id != 0 && auth_is_session_valid(session_id)) {
        return true;
    }

    char *remember_token_str = auth_get_remember_token_from_cookie(req);
    if (remember_token_str != NULL) {
        uint32_t new_session_id = auth_validate_remember_token(remember_token_str);
        if (new_session_id != 0) {
            return true;
        }
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, NULL, 0);
    return false;
}
