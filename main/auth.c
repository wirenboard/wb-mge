#include "auth.h"
#include "json_utils.h"
#include "setting_items.h"
#include "cJSON.h"

#include <assert.h>
#include <esp_log.h>
#include <esp_random.h>
#include <string.h>
#include <errno.h>
#include "array_size.h"
#include "freertos/semphr.h"


#define MAX_SESSIONS            10
#define COOKIE_MAX_LEN          (11 + 10 + 35 + 1)   // "session_id=" (11) + uint32_max digits (10) + "; HttpOnly; SameSite=Strict; Path=/" (35) + '\0' (1)


typedef struct {
    uint32_t session_ids[MAX_SESSIONS];
    int current_index;
} session_buffer_t;

static const char *TAG = "auth";

static session_buffer_t session_buffer = {
    .session_ids = {0},
    .current_index = 0,
};

static SemaphoreHandle_t session_mutex = NULL;

static inline void session_lock(void)
{
    BaseType_t taken = xSemaphoreTake(session_mutex, portMAX_DELAY);
    assert(taken == pdTRUE);
}

static inline void session_unlock(void)
{
    xSemaphoreGive(session_mutex);
}

// Backup session IDs buffer is used to restore sessions only
// when reset reason is ESP_RST_SW (esp_restart() called)
// Separate buffer is used to increase session search speed
static RTC_NOINIT_ATTR uint32_t backup_session_ids[MAX_SESSIONS];
static RTC_NOINIT_ATTR int backup_current_index;


static void defrag_session_buf(void)
{
    int write = 0;

    for (int read = 0; read < ARRAY_SIZE(session_buffer.session_ids); read++) {
        if (session_buffer.session_ids[read]) {
            if (write != read) {
                session_buffer.session_ids[write] = session_buffer.session_ids[read];
                session_buffer.session_ids[read] = 0;
            }
            write++;
        }
    }

    session_buffer.current_index = write % ARRAY_SIZE(session_buffer.session_ids);

    ESP_LOGD(TAG, "Session IDs buffer was defragmented, current_index: %d", session_buffer.current_index);
}


static void load_session_buf(void)
{
    memcpy(&session_buffer.session_ids[0], &backup_session_ids[0], sizeof(session_buffer.session_ids));
    // Validate restored index to guard against uninitialized RTC memory on first SW reboot
    if ((backup_current_index >= 0) && (backup_current_index < MAX_SESSIONS)) {
        session_buffer.current_index = backup_current_index;
    } else {
        session_buffer.current_index = 0;
    }
    ESP_LOGD(TAG, "Session IDs was read from backup buffer, current_index: %d", session_buffer.current_index);
}


static void save_session_buf(void)
{
    memcpy(&backup_session_ids[0], &session_buffer.session_ids[0], sizeof(backup_session_ids));
    backup_current_index = session_buffer.current_index;
    ESP_LOGD(TAG, "Session IDs was saved to backup buffer");
}


static void add_session_id(session_buffer_t *buffer, uint32_t session_id)
{
    ESP_LOGD(TAG, "Adding session ID: %" PRIu32, session_id);
    buffer->session_ids[buffer->current_index] = session_id;
    buffer->current_index = (buffer->current_index + 1) % MAX_SESSIONS;
    save_session_buf();
}

static inline bool session_id_exists(uint32_t session_id)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (session_buffer.session_ids[i] == session_id) {
            return true;
        }
    }
    return false;
}

static inline void find_and_remove_session_id(uint32_t session_id)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (session_buffer.session_ids[i] == session_id) {
            session_buffer.session_ids[i] = 0;
            defrag_session_buf();
            save_session_buf();
            ESP_LOGD(TAG, "Session ID %" PRIu32 " removed", session_id);
            break;
        }
    }
}

static uint32_t get_session_id_from_cookie(httpd_req_t *req)
{
    char session_id_str[COOKIE_MAX_LEN];
    size_t buf_len = sizeof(session_id_str);
    if (httpd_req_get_cookie_val(req, "session_id", session_id_str, &buf_len) == ESP_OK) {
        ESP_LOGD(TAG, "Session ID from cookie: %s", session_id_str);

        char *endptr;
        errno = 0;
        uint64_t value = strtoul(session_id_str, &endptr, 10);

        if ((errno == ERANGE) || (value > UINT32_MAX)) {
            ESP_LOGE(TAG, "Overflow occurred");
            return 0;
        }
        if (endptr == session_id_str) {
            ESP_LOGE(TAG, "No digits were found, session_id_str: '%s'", session_id_str);
            return 0;
        }
        if (*endptr != '\0') {
            ESP_LOGE(TAG, "Further characters after number: %s", endptr);
            return 0;
        }

        return (uint32_t)value;
    }
    return 0;
}


static uint32_t generate_unique_session_id(void)
{
    uint32_t session_id = 0;

    do {
        session_id = esp_random();
    } while (session_id == 0 || session_id_exists(session_id));

    return session_id;
}


static uint32_t authorization(char *login_req, char *pass_req)
{
    ESP_LOGD(TAG, "Authorization attempt");

    char login[SETTING_ITEM_MAX_STR_LEN] = {0};
    char pass[SETTING_ITEM_MAX_STR_LEN] = {0};

    if ((setting_items_read(KEY_LOGIN, login) != ESP_OK) ||
        (setting_items_read(KEY_PASS, pass) != ESP_OK))
    {
        ESP_LOGE(TAG, "Failed to read login or pass from storage");
        return 0;
    }

    if ((strncmp(login_req, login, SETTING_ITEM_MAX_STR_LEN) != 0) ||
        (strncmp(pass_req, pass, SETTING_ITEM_MAX_STR_LEN) != 0))
    {
        ESP_LOGW(TAG, "Invalid login or password");
        return 0;
    }

    uint32_t session_id = generate_unique_session_id();
    add_session_id(&session_buffer, session_id);

    return session_id;
}

static inline bool set_cookie_session_id(httpd_req_t *req, uint32_t session_id, char *cookie_header)
{
    snprintf(cookie_header, COOKIE_MAX_LEN, "session_id=%" PRIu32 "; HttpOnly; SameSite=Strict; Path=/", session_id);
    ESP_LOGD(TAG, "Cookie header: %s", cookie_header);
    if (httpd_resp_set_hdr(req, "Set-Cookie", cookie_header) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set cookie header");
        return false;
    }
    return true;
}

// === PUBLIC API ===

esp_err_t auth_init(void)
{
    esp_reset_reason_t reset_reason = esp_reset_reason();
    ESP_LOGD(TAG, "Reset reason: %d", reset_reason);

    // Load auth sessions only when restart initiated by esp_restart()
    if (reset_reason == ESP_RST_SW) {
        load_session_buf();
        ESP_LOGI(TAG, "Saved auth sessions were loaded");
    } else {
        ESP_LOGI(TAG, "Auth sessions were reset");
    }

    session_mutex = xSemaphoreCreateMutex();
    assert(session_mutex != NULL);

    ESP_LOGI(TAG, "Auth initialized");
    return ESP_OK;
}

bool auth_middleware_check(httpd_req_t *req)
{
    uint32_t session_id = get_session_id_from_cookie(req);
    bool valid = false;

    if (session_id != 0) {
        session_lock();
        valid = session_id_exists(session_id);
        session_unlock();
        if (!valid) {
            ESP_LOGW(TAG, "Session ID %" PRIu32 " is not valid", session_id);
        }
    } else {
        ESP_LOGW(TAG, "Session ID cookie not found");
    }

    if (valid) {
        return true;
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, NULL, 0);
    return false;
}

esp_err_t auth_login_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "Login request received");

    if (req->content_len > JSON_UTILS_REQ_RECV_BUF_SIZE) {
        return json_utils_send_error(req, "Login request is too large");
    }

    cJSON *request_json = json_utils_receive_json(req);
    if (request_json == NULL) {
        return json_utils_send_error(req, "Invalid login request JSON");
    }

    char cookie_header[COOKIE_MAX_LEN] = {0};
    cJSON *response_json = cJSON_CreateObject();
    bool result = false;

    if (cJSON_HasObjectItem(request_json, "login") && cJSON_HasObjectItem(request_json, "pass")) {
        cJSON *login_req = cJSON_GetObjectItem(request_json, "login");
        cJSON *pass_req = cJSON_GetObjectItem(request_json, "pass");

        if ((login_req->type == cJSON_String) && (pass_req->type == cJSON_String)) {
            session_lock();
            uint32_t session_id = authorization(login_req->valuestring, pass_req->valuestring);
            session_unlock();
            if (session_id != 0) {
                result = set_cookie_session_id(req, session_id, cookie_header);
            } else {
                cJSON_AddStringToObject(response_json, "error", "Invalid login or password");
            }
        } else {
            cJSON_AddStringToObject(response_json, "error", "Invalid login or password type");
        }
    } else {
        cJSON_AddStringToObject(response_json, "error", "No login or password in request");
    }

    cJSON_AddBoolToObject(response_json, "auth", result);
    json_utils_send_response(req, request_json, response_json);
    return ESP_OK;
}

esp_err_t auth_logout_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Logout request received");

    uint32_t session_id = get_session_id_from_cookie(req);
    if (session_id != 0) {
        session_lock();
        find_and_remove_session_id(session_id);
        session_unlock();
    } else {
        ESP_LOGW(TAG, "Session ID cookie not found, skipping logout");
    }

    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        return json_utils_send_error(req, "Failed to create response");
    }
    cJSON_AddBoolToObject(response_json, "logout", true);
    json_utils_send_response(req, NULL, response_json);
    return ESP_OK;
}

esp_err_t auth_session_check_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "Session check request received");
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}
