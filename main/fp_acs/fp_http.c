#include "fp_acs.h"

#if FP_ACS_ENABLED

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "array_size.h"
#include "auth.h"
#include "fp_console.h"
#include "fp_lock.h"
#include "fp_log.h"
#include "fp_store.h"
#include "json_utils.h"
#include "tg_bot.h"

static const char *TAG = "fp_http";

#define FP_HTTP_STATUS_BAD_REQUEST  "400 Bad Request"
#define FP_HTTP_STATUS_NOT_FOUND    "404 Not Found"
#define FP_HTTP_STATUS_CONFLICT     "409 Conflict"
#define FP_HTTP_STATUS_ERROR        "500 Internal Server Error"
#define FP_HTTP_SLOT_MAX            UINT16_MAX

#define FP_HTTP_ACTION_START        "start"
#define FP_HTTP_ACTION_CANCEL       "cancel"
#define FP_HTTP_ACTION_ADD          "add"
#define FP_HTTP_ACTION_REMOVE       "remove"
#define FP_HTTP_ACTION_CLEAR        "clear"

#define FP_HTTP_ERROR_JSON          "invalid JSON object"
#define FP_HTTP_ERROR_MEMORY        "out of memory"
#define FP_HTTP_ERROR_STORAGE       "user storage unavailable"
#define FP_HTTP_ERROR_NOT_FOUND     "not found"
#define FP_HTTP_ERROR_TG_TAKEN      "telegram username taken"
#define FP_HTTP_ERROR_USER_FIELDS   "invalid user: name is 1..48 bytes of UTF-8, tg_username up to 32 of A-Z a-z 0-9 _"
#define FP_HTTP_ERROR_CHAT_FIELDS   "invalid chat: id is a decimal chat id, name up to 48 bytes of UTF-8"
#define FP_HTTP_ERROR_LOCK_BUSY     "lock busy"
#define FP_HTTP_ERROR_NO_CONSOLE    "console capture not running"

#define FP_HTTP_LOG_HEAD            "{\"entries\":["
#define FP_HTTP_LOG_TAIL            "]}"
// Room for one record's JSON and the comma before it: a user name and a chat of control characters
// escape to six bytes a character, 601 bytes of JSON at worst.
#define FP_HTTP_LOG_ENTRY_MAX       640


// ── Responses ────────────────────────────────────────────────────────────────────────────

static esp_err_t fp_http_send_json(httpd_req_t *req, cJSON *root, bool ok)
{
    if ((root == NULL) || (!ok)) {
        cJSON_Delete(root);
        return json_utils_send_error_status(req, FP_HTTP_STATUS_ERROR, FP_HTTP_ERROR_MEMORY);
    }
    json_utils_send_response(req, NULL, root);
    return ESP_OK;
}

static esp_err_t fp_http_send_success(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    bool ok = ((root != NULL) && (cJSON_AddBoolToObject(root, "success", true) != NULL));
    return fp_http_send_json(req, root, ok);
}

// An unknown user or slot is a 400 rather than a 404: the web UI reports a 404 or a 500 as a lost
// connection, without the reason given here.
static esp_err_t fp_http_send_req(httpd_req_t *req, fp_acs_req_t result)
{
    switch (result) {
    case FP_ACS_REQ_OK:
        return fp_http_send_success(req);
    case FP_ACS_REQ_UNKNOWN_USER:
    case FP_ACS_REQ_UNKNOWN_SLOT:
        return json_utils_send_error(req, fp_acs_req_str(result));
    default:
        return json_utils_send_error_status(req, FP_HTTP_STATUS_CONFLICT, fp_acs_req_str(result));
    }
}

// The store's errors (fp_store.h) as HTTP statuses. invalid is the message for a bad field,
// not_found_status the status for an unknown user (400) or chat (404). A store that cannot be
// used or written is a 409 rather than a 500, for the reason given at fp_http_send_req().
static esp_err_t fp_http_send_store_error(httpd_req_t *req, esp_err_t err, const char *invalid,
                                          const char *not_found_status)
{
    switch (err) {
    case ESP_ERR_INVALID_ARG:
        return json_utils_send_error(req, invalid);
    case ESP_ERR_NOT_FOUND:
        return json_utils_send_error_status(req, not_found_status, FP_HTTP_ERROR_NOT_FOUND);
    case ESP_ERR_NO_MEM:
        return json_utils_send_error_status(req, FP_HTTP_STATUS_CONFLICT, "full");
    case ESP_ERR_NOT_ALLOWED:
        return json_utils_send_error_status(req, FP_HTTP_STATUS_CONFLICT, FP_HTTP_ERROR_TG_TAKEN);
    case ESP_ERR_INVALID_STATE:
        return json_utils_send_error_status(req, FP_HTTP_STATUS_CONFLICT, FP_HTTP_ERROR_STORAGE);
    default:
        return json_utils_send_error_status(req, FP_HTTP_STATUS_CONFLICT, esp_err_to_name(err));
    }
}

static bool fp_http_add_nullable_number(cJSON *object, const char *key, bool valid, double value)
{
    cJSON *item = valid ? cJSON_AddNumberToObject(object, key, value) : cJSON_AddNullToObject(object, key);
    return (item != NULL);
}

static bool fp_http_add_nullable_string(cJSON *object, const char *key, bool valid, const char *value)
{
    cJSON *item = valid ? cJSON_AddStringToObject(object, key, value) : cJSON_AddNullToObject(object, key);
    return (item != NULL);
}

static cJSON *fp_http_user_json(const fp_store_user_t *user)
{
    cJSON *object = cJSON_CreateObject();
    if (object == NULL) {
        return NULL;
    }
    bool ok = ((cJSON_AddNumberToObject(object, "id", user->id) != NULL) &&
               (cJSON_AddStringToObject(object, "name", user->name) != NULL) &&
               (cJSON_AddBoolToObject(object, "enabled", user->enabled) != NULL) &&
               (cJSON_AddBoolToObject(object, "allow_fp", user->allow_fp) != NULL) &&
               (cJSON_AddBoolToObject(object, "allow_tg", user->allow_tg) != NULL) &&
               (cJSON_AddStringToObject(object, "tg_username", user->tg_username) != NULL));
    cJSON *fingers = ok ? cJSON_AddArrayToObject(object, "fingers") : NULL;
    ok = (fingers != NULL);
    for (size_t i = 0; ok && (i < user->finger_count); i++) {
        cJSON *slot = cJSON_CreateNumber(user->fingers[i]);
        ok = ((slot != NULL) && cJSON_AddItemToArray(fingers, slot));
    }
    if (!ok) {
        cJSON_Delete(object);
        return NULL;
    }
    return object;
}


// ── Requests ─────────────────────────────────────────────────────────────────────────────

// The request body as a JSON object, or NULL (a 400 has then been sent).
static cJSON *fp_http_receive_object(httpd_req_t *req)
{
    cJSON *body = json_utils_receive_json(req);
    if (!cJSON_IsObject(body)) {
        cJSON_Delete(body);
        json_utils_send_error(req, FP_HTTP_ERROR_JSON);
        return NULL;
    }
    return body;
}

// A whole number in 0..max.
static bool fp_http_get_uint(const cJSON *object, const char *key, uint32_t max, uint32_t *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    double number = item->valuedouble;
    if ((number < 0) || (number > (double)max) || (number != (double)(uint32_t)number)) {
        return false;
    }
    *value = (uint32_t)number;
    return true;
}

// An optional boolean: false only when it is present and not a boolean.
static bool fp_http_opt_bool(const cJSON *object, const char *key, bool *set, bool *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    *set = (item != NULL);
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsBool(item)) {
        return false;
    }
    *value = cJSON_IsTrue(item);
    return true;
}

// An optional string: false only when it is present and not a string.
static bool fp_http_opt_string(const cJSON *object, const char *key, const char **value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    *value = NULL;
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsString(item)) {
        return false;
    }
    *value = item->valuestring;
    return true;
}

static const char *fp_http_get_string(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}


// ── Handlers ─────────────────────────────────────────────────────────────────────────────

static bool fp_http_status_enroll(cJSON *root, const fp_acs_enroll_t *enroll)
{
    cJSON *object = cJSON_AddObjectToObject(root, "enroll");
    if (object == NULL) {
        return false;
    }
    bool ok = (cJSON_AddStringToObject(object, "state", fp_acs_enroll_state_str(enroll->state)) != NULL);
    ok = fp_http_add_nullable_number(object, "user_id", (enroll->user_id != FP_STORE_NO_USER), enroll->user_id) && ok;
    ok = (cJSON_AddNumberToObject(object, "step", enroll->step) != NULL) && ok;
    ok = (cJSON_AddNumberToObject(object, "steps", FP_ACS_ENROLL_STEPS) != NULL) && ok;
    ok = fp_http_add_nullable_number(object, "slot", enroll->has_slot, enroll->slot) && ok;
    ok = fp_http_add_nullable_string(object, "error", (enroll->error[0] != '\0'), enroll->error) && ok;
    return ok;
}

// The fields the last event and a log record share: who tried, how, and what came of it.
static bool fp_http_add_attempt(cJSON *object, fp_acs_source_t source, fp_acs_result_t result, uint8_t user_id,
                                const char *user_name, bool has_score, uint16_t score, const char *chat)
{
    bool known = (user_id != FP_STORE_NO_USER);
    bool ok = (cJSON_AddStringToObject(object, "source", fp_acs_source_str(source)) != NULL);
    ok = (cJSON_AddStringToObject(object, "result", fp_acs_result_str(result)) != NULL) && ok;
    ok = fp_http_add_nullable_number(object, "user_id", known, user_id) && ok;
    ok = fp_http_add_nullable_string(object, "user_name", known, user_name) && ok;
    ok = fp_http_add_nullable_number(object, "score", has_score, score) && ok;
    ok = fp_http_add_nullable_string(object, "chat", (chat[0] != '\0'), chat) && ok;
    return ok;
}

static bool fp_http_status_event(cJSON *root)
{
    fp_acs_event_t event;
    if (!fp_acs_get_last_event(&event)) {
        return (cJSON_AddNullToObject(root, "last_event") != NULL);
    }
    cJSON *object = cJSON_AddObjectToObject(root, "last_event");
    if (object == NULL) {
        return false;
    }
    bool ok = (cJSON_AddNumberToObject(object, "seq", event.seq) != NULL);
    ok = (cJSON_AddNumberToObject(object, "uptime_s", event.uptime_s) != NULL) && ok;
    ok = fp_http_add_attempt(object, event.source, event.result, event.user_id, event.user_name, event.has_score,
                             event.score, event.chat) && ok;
    return ok;
}

static bool fp_http_status_image(cJSON *root, uint32_t seq, uint32_t event_seq)
{
    if (seq == 0) {
        return (cJSON_AddNullToObject(root, "image") != NULL);
    }
    cJSON *object = cJSON_AddObjectToObject(root, "image");
    if (object == NULL) {
        return false;
    }
    bool ok = (cJSON_AddNumberToObject(object, "seq", seq) != NULL);
    ok = (cJSON_AddNumberToObject(object, "event_seq", event_seq) != NULL) && ok;
    ok = (cJSON_AddNumberToObject(object, "width", FP_ACS_IMAGE_WIDTH) != NULL) && ok;
    ok = (cJSON_AddNumberToObject(object, "height", FP_ACS_IMAGE_HEIGHT) != NULL) && ok;
    return ok;
}

static bool fp_http_status_lock(cJSON *root)
{
    cJSON *object = cJSON_AddObjectToObject(root, "lock");
    if (object == NULL) {
        return false;
    }
    bool ok = (cJSON_AddBoolToObject(object, "on", fp_lock_relay_on()) != NULL);
    ok = (cJSON_AddBoolToObject(object, "online", fp_lock_relay_online()) != NULL) && ok;
    return ok;
}

static bool fp_http_status_telegram(cJSON *root)
{
    bool enabled = false;
    bool online = false;
    char error[TG_BOT_ERROR_MAX];
    tg_bot_get_status(&enabled, &online, error, sizeof(error));

    cJSON *object = cJSON_AddObjectToObject(root, "telegram");
    if (object == NULL) {
        return false;
    }
    bool ok = (cJSON_AddBoolToObject(object, "enabled", enabled) != NULL);
    ok = (cJSON_AddBoolToObject(object, "online", online) != NULL) && ok;
    ok = fp_http_add_nullable_string(object, "error", (error[0] != '\0'), error) && ok;
    return ok;
}

static esp_err_t fp_http_status_get(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    fp_acs_status_t status;
    fp_acs_get_status(&status);

    cJSON *root = cJSON_CreateObject();
    cJSON *sensor = (root != NULL) ? cJSON_AddObjectToObject(root, "sensor") : NULL;
    bool ok = (sensor != NULL);
    ok = ok && (cJSON_AddBoolToObject(sensor, "online", status.online) != NULL);
    ok = ok && (cJSON_AddNumberToObject(sensor, "capacity", status.capacity) != NULL);
    ok = ok && (cJSON_AddNumberToObject(sensor, "used", status.used) != NULL);
    ok = ok && (cJSON_AddNumberToObject(root, "users_rev", status.users_rev) != NULL);
    ok = ok && fp_http_status_enroll(root, &status.enroll);
    ok = ok && fp_http_status_event(root);
    ok = ok && fp_http_status_image(root, status.image_seq, status.image_event_seq);
    ok = ok && fp_http_status_lock(root);
    ok = ok && fp_http_status_telegram(root);
    return fp_http_send_json(req, root, ok);
}

// width * height bytes of 8-bit grey, copied out first so that the sensor task never waits on
// the network.
static esp_err_t fp_http_image_get(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    fp_acs_status_t status;
    fp_acs_get_status(&status);
    if (status.image_seq == 0) {
        return json_utils_send_error_status(req, FP_HTTP_STATUS_NOT_FOUND, "no image");
    }

    uint8_t *image = heap_caps_malloc(FP_ACS_IMAGE_SIZE, MALLOC_CAP_SPIRAM);
    if (image == NULL) {
        return json_utils_send_error_status(req, FP_HTTP_STATUS_ERROR, FP_HTTP_ERROR_MEMORY);
    }
    size_t len = fp_acs_copy_image(image, FP_ACS_IMAGE_SIZE, NULL);
    if (len == 0) {
        heap_caps_free(image);
        return json_utils_send_error_status(req, FP_HTTP_STATUS_NOT_FOUND, "no image");
    }

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, (const char *)image, (ssize_t)len);
    heap_caps_free(image);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Sending the image failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

static cJSON *fp_http_log_entry_json(const fp_log_entry_t *entry)
{
    cJSON *object = cJSON_CreateObject();
    if (object == NULL) {
        return NULL;
    }
    bool ok = (cJSON_AddNumberToObject(object, "seq", entry->seq) != NULL);
    ok = fp_http_add_nullable_number(object, "time", (entry->time != FP_LOG_TIME_UNKNOWN), entry->time) && ok;
    ok = fp_http_add_attempt(object, entry->source, entry->result, entry->user_id, entry->user_name, entry->has_score,
                             entry->score, entry->chat) && ok;
    if (!ok) {
        cJSON_Delete(object);
        return NULL;
    }
    return object;
}

// Newest first. Each record is printed on its own into one PSRAM buffer: the whole log as one
// cJSON tree would be well over a thousand small allocations, which land in internal RAM.
static esp_err_t fp_http_log_get(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    size_t body_size = sizeof(FP_HTTP_LOG_HEAD) + (FP_LOG_CAPACITY * FP_HTTP_LOG_ENTRY_MAX) + sizeof(FP_HTTP_LOG_TAIL);
    fp_log_entry_t *entries = heap_caps_malloc(FP_LOG_CAPACITY * sizeof(fp_log_entry_t), MALLOC_CAP_SPIRAM);
    char *body = heap_caps_malloc(body_size, MALLOC_CAP_SPIRAM);
    if ((entries == NULL) || (body == NULL)) {
        heap_caps_free(entries);
        heap_caps_free(body);
        return json_utils_send_error_status(req, FP_HTTP_STATUS_ERROR, FP_HTTP_ERROR_MEMORY);
    }
    size_t count = fp_log_snapshot(entries, FP_LOG_CAPACITY);

    size_t len = sizeof(FP_HTTP_LOG_HEAD) - 1;
    memcpy(body, FP_HTTP_LOG_HEAD, len);
    bool ok = true;
    for (size_t i = 0; ok && (i < count); i++) {
        if (i > 0) {
            body[len++] = ',';
        }
        cJSON *object = fp_http_log_entry_json(&entries[i]);
        ok = ((object != NULL) && cJSON_PrintPreallocated(object, body + len, FP_HTTP_LOG_ENTRY_MAX - 1, false));
        cJSON_Delete(object);
        if (ok) {
            len += strlen(body + len);
        }
    }
    heap_caps_free(entries);
    if (!ok) {
        heap_caps_free(body);
        return json_utils_send_error_status(req, FP_HTTP_STATUS_ERROR, FP_HTTP_ERROR_MEMORY);
    }
    memcpy(body + len, FP_HTTP_LOG_TAIL, sizeof(FP_HTTP_LOG_TAIL) - 1);
    len += sizeof(FP_HTTP_LOG_TAIL) - 1;

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, body, (ssize_t)len);
    heap_caps_free(body);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Sending the log failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

// The device log as the UART console printed it, oldest line first. Copied out first, so that no
// task that logs waits on the network.
static esp_err_t fp_http_console_get(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    char *text = heap_caps_malloc(FP_CONSOLE_SIZE, MALLOC_CAP_SPIRAM);
    if (text == NULL) {
        return json_utils_send_error_status(req, FP_HTTP_STATUS_ERROR, FP_HTTP_ERROR_MEMORY);
    }
    size_t len = 0;
    if (!fp_console_copy(text, FP_CONSOLE_SIZE, &len)) {
        heap_caps_free(text);
        return json_utils_send_error_status(req, FP_HTTP_STATUS_NOT_FOUND, FP_HTTP_ERROR_NO_CONSOLE);
    }

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, text, (ssize_t)len);
    heap_caps_free(text);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Sending the console failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

static esp_err_t fp_http_users_get(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    fp_store_user_t *users = malloc(FP_STORE_USERS_MAX * sizeof(fp_store_user_t));
    if (users == NULL) {
        return json_utils_send_error_status(req, FP_HTTP_STATUS_ERROR, FP_HTTP_ERROR_MEMORY);
    }
    size_t count = fp_store_users_snapshot(users, FP_STORE_USERS_MAX, NULL);

    cJSON *root = cJSON_CreateObject();
    cJSON *list = (root != NULL) ? cJSON_AddArrayToObject(root, "users") : NULL;
    bool ok = (list != NULL);
    for (size_t i = 0; ok && (i < count); i++) {
        cJSON *user = fp_http_user_json(&users[i]);
        ok = ((user != NULL) && cJSON_AddItemToArray(list, user));
    }
    free(users);
    ok = ok && (cJSON_AddNumberToObject(root, "max_users", FP_STORE_USERS_MAX) != NULL);
    ok = ok && (cJSON_AddNumberToObject(root, "max_fingers", FP_STORE_FINGERS_MAX) != NULL);
    return fp_http_send_json(req, root, ok);
}

// No id: create, with the defaults of fp_store_create_user(). An id: update the fields given.
static esp_err_t fp_http_user_post(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }
    cJSON *body = fp_http_receive_object(req);
    if (body == NULL) {
        return ESP_OK;
    }

    fp_store_user_patch_t patch;
    memset(&patch, 0, sizeof(patch));
    patch.name = fp_http_get_string(body, "name");
    bool has_id = (cJSON_GetObjectItemCaseSensitive(body, "id") != NULL);
    uint32_t id = FP_STORE_NO_USER;
    bool valid = ((patch.name != NULL) &&
                  fp_http_opt_string(body, "tg_username", &patch.tg_username) &&
                  fp_http_opt_bool(body, "enabled", &patch.set_enabled, &patch.enabled) &&
                  fp_http_opt_bool(body, "allow_fp", &patch.set_allow_fp, &patch.allow_fp) &&
                  fp_http_opt_bool(body, "allow_tg", &patch.set_allow_tg, &patch.allow_tg) &&
                  ((!has_id) || fp_http_get_uint(body, "id", UINT8_MAX, &id)));
    if (!valid) {
        cJSON_Delete(body);
        return json_utils_send_error(req, "expected {\"id\"?, \"name\", \"enabled\"?, \"allow_fp\"?, "
                                          "\"allow_tg\"?, \"tg_username\"?}");
    }

    fp_store_user_t user;
    esp_err_t err = has_id ? fp_store_update_user((uint8_t)id, &patch, &user) : fp_store_create_user(&patch, &user);
    cJSON_Delete(body);
    if (err != ESP_OK) {
        return fp_http_send_store_error(req, err, FP_HTTP_ERROR_USER_FIELDS, FP_HTTP_STATUS_BAD_REQUEST);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *object = fp_http_user_json(&user);
    bool ok = ((root != NULL) && (object != NULL) && (cJSON_AddBoolToObject(root, "success", true) != NULL));
    if (ok) {
        cJSON_AddItemToObject(root, "user", object);
    } else {
        cJSON_Delete(object);
    }
    return fp_http_send_json(req, root, ok);
}

static esp_err_t fp_http_user_delete_post(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }
    cJSON *body = fp_http_receive_object(req);
    if (body == NULL) {
        return ESP_OK;
    }
    uint32_t id = 0;
    bool valid = fp_http_get_uint(body, "id", UINT8_MAX, &id);
    cJSON_Delete(body);
    if (!valid) {
        return json_utils_send_error(req, "expected {\"id\"}");
    }
    return fp_http_send_req(req, fp_acs_delete_user((uint8_t)id));
}

static esp_err_t fp_http_finger_delete_post(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }
    cJSON *body = fp_http_receive_object(req);
    if (body == NULL) {
        return ESP_OK;
    }
    uint32_t slot = 0;
    bool valid = fp_http_get_uint(body, "slot", FP_HTTP_SLOT_MAX, &slot);
    cJSON_Delete(body);
    if (!valid) {
        return json_utils_send_error(req, "expected {\"slot\"}");
    }
    return fp_http_send_req(req, fp_acs_delete_finger((uint16_t)slot));
}

static esp_err_t fp_http_enroll_post(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }
    cJSON *body = fp_http_receive_object(req);
    if (body == NULL) {
        return ESP_OK;
    }

    const char *action = fp_http_get_string(body, "action");
    uint32_t user_id = 0;
    bool start = ((action != NULL) && (strcmp(action, FP_HTTP_ACTION_START) == 0) &&
                  fp_http_get_uint(body, "user_id", UINT8_MAX, &user_id));
    bool cancel = ((action != NULL) && (strcmp(action, FP_HTTP_ACTION_CANCEL) == 0));
    cJSON_Delete(body);

    if (start) {
        return fp_http_send_req(req, fp_acs_enroll_start((uint8_t)user_id));
    }
    if (cancel) {
        fp_acs_enroll_cancel();
        return fp_http_send_success(req);
    }
    return json_utils_send_error(req, "expected {\"action\": \"start\", \"user_id\"} or {\"action\": \"cancel\"}");
}

static esp_err_t fp_http_tg_chats_get(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    fp_store_tg_chat_t *chats = malloc(FP_STORE_TG_CHATS_MAX * sizeof(fp_store_tg_chat_t));
    if (chats == NULL) {
        return json_utils_send_error_status(req, FP_HTTP_STATUS_ERROR, FP_HTTP_ERROR_MEMORY);
    }
    size_t count = fp_store_tg_chats_list(chats, FP_STORE_TG_CHATS_MAX);

    cJSON *root = cJSON_CreateObject();
    cJSON *list = (root != NULL) ? cJSON_AddArrayToObject(root, "chats") : NULL;
    bool ok = (list != NULL);
    for (size_t i = 0; ok && (i < count); i++) {
        cJSON *chat = cJSON_CreateObject();
        ok = ((chat != NULL) && cJSON_AddItemToArray(list, chat) &&
              (cJSON_AddStringToObject(chat, "id", chats[i].id) != NULL) &&
              (cJSON_AddStringToObject(chat, "name", chats[i].name) != NULL));
    }
    free(chats);
    return fp_http_send_json(req, root, ok);
}

static esp_err_t fp_http_tg_chats_post(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }
    cJSON *body = fp_http_receive_object(req);
    if (body == NULL) {
        return ESP_OK;
    }

    const char *action = fp_http_get_string(body, "action");
    const char *id = fp_http_get_string(body, "id");
    const char *name = fp_http_get_string(body, "name");
    bool known = true;
    esp_err_t err = ESP_OK;
    if ((action != NULL) && (strcmp(action, FP_HTTP_ACTION_ADD) == 0) && (id != NULL)) {
        err = fp_store_tg_chat_add(id, (name != NULL) ? name : "");
    } else if ((action != NULL) && (strcmp(action, FP_HTTP_ACTION_REMOVE) == 0) && (id != NULL)) {
        err = fp_store_tg_chat_remove(id);
    } else if ((action != NULL) && (strcmp(action, FP_HTTP_ACTION_CLEAR) == 0)) {
        err = fp_store_tg_chat_clear();
    } else {
        known = false;
    }
    cJSON_Delete(body);

    if (!known) {
        return json_utils_send_error(req, "expected {\"action\": \"add\", \"id\", \"name\"}, "
                                          "{\"action\": \"remove\", \"id\"} or {\"action\": \"clear\"}");
    }
    if (err != ESP_OK) {
        return fp_http_send_store_error(req, err, FP_HTTP_ERROR_CHAT_FIELDS, FP_HTTP_STATUS_NOT_FOUND);
    }
    return fp_http_send_success(req);
}

// Queued to the lock task: the relay's new state shows in GET /fp/status once it has been written.
// A command the task did not take (not running, queue full) is a 409, for the reason given at
// fp_http_send_req().
static esp_err_t fp_http_lock_post(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }
    cJSON *body = fp_http_receive_object(req);
    if (body == NULL) {
        return ESP_OK;
    }

    const cJSON *on = cJSON_GetObjectItemCaseSensitive(body, "on");
    bool valid = cJSON_IsBool(on);
    bool value = cJSON_IsTrue(on);
    cJSON_Delete(body);
    if (!valid) {
        return json_utils_send_error(req, "expected {\"on\": true} or {\"on\": false}");
    }
    if (!fp_lock_set(value)) {
        return json_utils_send_error_status(req, FP_HTTP_STATUS_CONFLICT, FP_HTTP_ERROR_LOCK_BUSY);
    }
    return fp_http_send_success(req);
}


static const httpd_uri_t fp_http_uris[] = {
    {.uri = "/fp/status",        .method = HTTP_GET,  .handler = fp_http_status_get,         .user_ctx = NULL},
    {.uri = "/fp/image",         .method = HTTP_GET,  .handler = fp_http_image_get,          .user_ctx = NULL},
    {.uri = "/fp/log",           .method = HTTP_GET,  .handler = fp_http_log_get,            .user_ctx = NULL},
    {.uri = "/fp/console",       .method = HTTP_GET,  .handler = fp_http_console_get,        .user_ctx = NULL},
    {.uri = "/fp/users",         .method = HTTP_GET,  .handler = fp_http_users_get,          .user_ctx = NULL},
    {.uri = "/fp/user",          .method = HTTP_POST, .handler = fp_http_user_post,          .user_ctx = NULL},
    {.uri = "/fp/user/delete",   .method = HTTP_POST, .handler = fp_http_user_delete_post,   .user_ctx = NULL},
    {.uri = "/fp/finger/delete", .method = HTTP_POST, .handler = fp_http_finger_delete_post, .user_ctx = NULL},
    {.uri = "/fp/enroll",        .method = HTTP_POST, .handler = fp_http_enroll_post,        .user_ctx = NULL},
    {.uri = "/fp/tg_chats",      .method = HTTP_GET,  .handler = fp_http_tg_chats_get,       .user_ctx = NULL},
    {.uri = "/fp/tg_chats",      .method = HTTP_POST, .handler = fp_http_tg_chats_post,      .user_ctx = NULL},
    {.uri = "/fp/lock",          .method = HTTP_POST, .handler = fp_http_lock_post,          .user_ctx = NULL},
};

void fp_http_register_handlers(httpd_handle_t server)
{
    for (size_t i = 0; i < ARRAY_SIZE(fp_http_uris); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &fp_http_uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Registering %s failed: %s", fp_http_uris[i].uri, esp_err_to_name(err));
        }
    }
}

#endif // FP_ACS_ENABLED
