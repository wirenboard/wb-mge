#include "tg_bot.h"

// The bot opens the lock of the fingerprint access control, so it exists where that does and
// compiles to nothing everywhere else (see main/fp_acs/fp_acs.h).
#include "fp_acs.h"

#if FP_ACS_ENABLED

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "fp_lock.h"
#include "fp_store.h"
#include "setting_items.h"

static const char *TAG = "tg_bot";

#define TG_BOT_TASK_STACK_SIZE      (8 * 1024)      // the TLS handshake runs on it
#define TG_BOT_TASK_PRIORITY        3

// Every request URL is "<tg_api_url>/bot<token>/<method>". Every URL but the bare server carries
// the token, so none of them is ever logged. The buffer takes the longest server and token a
// setting can hold and the longest method: getUpdates with every number at its widest, the NUL
// counted in by sizeof.
#define TG_BOT_API_PATH             "/bot"
#define TG_BOT_METHOD_MAX           sizeof("/getUpdates?offset=-9223372036854775808" \
                                           "&limit=-2147483648&timeout=-2147483648")
#define TG_BOT_URL_MAX              ((SETTING_ITEM_MAX_STR_LEN - 1) + (sizeof(TG_BOT_API_PATH) - 1) + \
                                     (SETTING_ITEM_MAX_STR_LEN - 1) + TG_BOT_METHOD_MAX)

#define TG_BOT_POLL_TIMEOUT_S       25      // how long Telegram holds a getUpdates open
#define TG_BOT_HTTP_TIMEOUT_MS      ((TG_BOT_POLL_TIMEOUT_S + 10) * 1000)
#define TG_BOT_OFFSET_LAST          (-1)    // getUpdates offset: only the newest update, the rest are forgotten
// One update per poll: an update too big for the response buffer is then known to be that one,
// and is skipped by its update_id without losing any other.
#define TG_BOT_POLL_LIMIT           1
#define TG_BOT_RESPONSE_MAX         (16 * 1024)
#define TG_BOT_IDLE_MS              5000    // how often an idle bot looks for a token again
#define TG_BOT_RETRY_MS             10000   // pause after a failed poll
#define TG_BOT_HTTP_OK              200
#define TG_BOT_DECIMAL_BASE         10
#define TG_BOT_OK_PREFIX            "{\"ok\":true"          // how the Bot API opens an answer that succeeded
#define TG_BOT_ERROR_NOT_OK         "unexpected answer"     // a 200 whose answer does not say "ok": true
#define TG_BOT_ERROR_BAD_TOKEN      "invalid token"         // tg_token has characters no bot token has

#define TG_BOT_ID_MAX               24      // a decimal Telegram id, sign included
#define TG_BOT_REPLY_MAX            2048

_Static_assert(TG_BOT_ID_MAX > (FP_STORE_TG_CHAT_ID_MAX + 1), "a chat id cut to TG_BOT_ID_MAX must stay too long");
#define TG_BOT_UPDATE_ID_KEY        "\"update_id\":"
#define TG_BOT_ADMIN_SEPARATOR      ','
#define TG_BOT_ARG_SEPARATOR        ' '

#define TG_BOT_CMD_ACTIVATE         "/activate"
#define TG_BOT_CMD_DEACTIVATE       "/deactivate"
#define TG_BOT_CMD_HELP             "/help"
#define TG_BOT_CMD_RESET            "/reset"
#define TG_BOT_CMD_LIST             "/list"
#define TG_BOT_CMD_OPEN             "open"
#define TG_BOT_CMD_OPEN_CAPITAL     "Open"

#define TG_BOT_TEXT_BOOTED          "Lock door controller booted"
#define TG_BOT_TEXT_NO_PERMISSION   "Error: You don't have permission to use this command"
#define TG_BOT_TEXT_OPENED          "Opened"
#define TG_BOT_TEXT_NOT_ALLOWED     "You are not allowed to open the door"
#define TG_BOT_TEXT_NOT_ACTIVATED   "This group is not activated. Admin command: /activate"
#define TG_BOT_TEXT_GROUP_PREFIX    "Group "
#define TG_BOT_TEXT_HELP_ADMIN      "Admin commands:\n\n"                                          \
                                    "/activate [chat_id [name]] - Activate a group (default: this one)\n" \
                                    "/deactivate [chat_id] - Deactivate a group (default: this one)\n"    \
                                    "/list - List all activated groups\n"                          \
                                    "/reset - Remove all activated groups\n"                       \
                                    "Regular users in activated groups can use:\n"                 \
                                    "'open' - Open the door"
#define TG_BOT_TEXT_HELP_USER       "Available commands:\n\n"                                      \
                                    "'open' - Open the door (works in activated groups, "          \
                                    "and for users allowed to open it through Telegram)\n"

// Task only: nothing in this group is touched by any other task.
static esp_http_client_handle_t tg_bot_http;
static char   *tg_bot_response;                         // TG_BOT_RESPONSE_MAX bytes, NUL-terminated
static size_t  tg_bot_response_len;
static bool    tg_bot_response_overflow;
static char    tg_bot_server[SETTING_ITEM_MAX_STR_LEN]; // the Bot API server the polls use (tg_api_url)
static char    tg_bot_token[SETTING_ITEM_MAX_STR_LEN];  // the token the polls use - never logged
static bool    tg_bot_token_usable;
static int64_t tg_bot_offset;                           // the next update_id to ask for
static bool    tg_bot_backlog_dropped;                  // false until the first poll with this server and token
static bool    tg_bot_booted;                           // the boot message has been sent
static char    tg_bot_admins[SETTING_ITEM_MAX_STR_LEN];
static char    tg_bot_reply[TG_BOT_REPLY_MAX];

// The status tg_bot_get_status() reports: written by the bot task, read by the web server. The
// flags use the atomic builtins; the reason, a string, is copied in and out under the spinlock.
static bool         tg_bot_enabled;                 // a token is set
static bool         tg_bot_online;                  // see tg_bot_get_status() in tg_bot.h
static char         tg_bot_error[TG_BOT_ERROR_MAX]; // see tg_bot_get_status() in tg_bot.h
static portMUX_TYPE tg_bot_error_lock = portMUX_INITIALIZER_UNLOCKED;


static esp_err_t tg_bot_http_event(esp_http_client_event_t *evt)
{
    if ((evt->event_id != HTTP_EVENT_ON_DATA) || (evt->data_len <= 0) || tg_bot_response_overflow) {
        return ESP_OK;
    }
    size_t len = (size_t)evt->data_len;
    // What does not fit is dropped; the beginning, which holds the update_id, is kept.
    if ((tg_bot_response_len + len) >= TG_BOT_RESPONSE_MAX) {
        tg_bot_response_overflow = true;
        return ESP_OK;
    }
    memcpy(tg_bot_response + tg_bot_response_len, evt->data, len);
    tg_bot_response_len += len;
    return ESP_OK;
}

// One request on the kept-alive connection: a GET when body is NULL, a JSON POST otherwise.
// The response lands in tg_bot_response, the HTTP status in *status.
static esp_err_t tg_bot_exchange(const char *url, const char *body, int *status)
{
    if (tg_bot_http == NULL) {
        // Made for the bare server, which carries no token: a server esp_http_client cannot parse
        // is refused - and logged - here, before any URL with the token reaches it. The crt
        // bundle is used by an https server only.
        esp_http_client_config_t config = {
            .url = tg_bot_server,
            .timeout_ms = TG_BOT_HTTP_TIMEOUT_MS,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = tg_bot_http_event,
        };
        tg_bot_http = esp_http_client_init(&config);
        if (tg_bot_http == NULL) {
            ESP_LOGE(TAG, "No HTTP client for the Bot API server %s", tg_bot_server);
            return ESP_FAIL;
        }
    }

    tg_bot_response_len = 0;
    tg_bot_response_overflow = false;

    esp_err_t err = esp_http_client_set_url(tg_bot_http, url);
    if (err == ESP_OK) {
        if (body != NULL) {
            esp_http_client_set_method(tg_bot_http, HTTP_METHOD_POST);
            esp_http_client_set_header(tg_bot_http, "Content-Type", "application/json");
            esp_http_client_set_post_field(tg_bot_http, body, (int)strlen(body));
        } else {
            esp_http_client_set_method(tg_bot_http, HTTP_METHOD_GET);
            // Also drops the Content-Type a POST before it set.
            esp_http_client_set_post_field(tg_bot_http, NULL, 0);
        }
        err = esp_http_client_perform(tg_bot_http);
    }
    if (err != ESP_OK) {
        // A request that broke off leaves the connection in the middle of an exchange; the next
        // one starts on a fresh connection.
        esp_http_client_close(tg_bot_http);
        return err;
    }

    tg_bot_response[tg_bot_response_len] = '\0';
    *status = esp_http_client_get_status_code(tg_bot_http);
    return ESP_OK;
}

// Formatted outside the spinlock, so that only a copy runs with the interrupts off.
static void tg_bot_set_error(const char *reason)
{
    char copy[TG_BOT_ERROR_MAX];
    snprintf(copy, sizeof(copy), "%s", reason);
    portENTER_CRITICAL(&tg_bot_error_lock);
    memcpy(tg_bot_error, copy, sizeof(tg_bot_error));
    portEXIT_CRITICAL(&tg_bot_error_lock);
}

// Whether the answer in tg_bot_response says "ok": true. One cut off by the buffer cannot be
// parsed and is judged by its beginning: the Bot API opens every answer with that field.
static bool tg_bot_response_ok(void)
{
    if (tg_bot_response_overflow) {
        return (strncmp(tg_bot_response, TG_BOT_OK_PREFIX, sizeof(TG_BOT_OK_PREFIX) - 1) == 0);
    }
    cJSON *root = cJSON_ParseWithLength(tg_bot_response, tg_bot_response_len);
    bool ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok"));
    cJSON_Delete(root);
    return ok;
}

// Records the outcome of a request for tg_bot_get_status(): online after an HTTP 200 whose answer
// says "ok": true, offline with the reason after anything else. status counts only when err is
// ESP_OK. Only a change between online and offline is logged, not every request.
static void tg_bot_note_result(esp_err_t err, int status)
{
    char reason[TG_BOT_ERROR_MAX] = "";
    if (err != ESP_OK) {
        snprintf(reason, sizeof(reason), "%s", esp_err_to_name(err));
    } else if (status != TG_BOT_HTTP_OK) {
        snprintf(reason, sizeof(reason), "HTTP %d", status);
    } else if (!tg_bot_response_ok()) {
        snprintf(reason, sizeof(reason), "%s", TG_BOT_ERROR_NOT_OK);
    }

    bool online = (reason[0] == '\0');
    if (!online) {
        tg_bot_set_error(reason);
    }
    bool was_online = __atomic_exchange_n(&tg_bot_online, online, __ATOMIC_SEQ_CST);
    if (online == was_online) {
        return;
    }
    if (online) {
        ESP_LOGI(TAG, "Bot API server %s is online", tg_bot_server);
    } else {
        ESP_LOGW(TAG, "Bot API server %s is offline: %s", tg_bot_server, reason);
    }
}

// Whether snprintf's result says the whole request URL made it into its buffer. A cut URL is never
// sent: it may end inside the token or name another method. TG_BOT_URL_MAX rules that out for
// any server and token the settings can hold.
static bool tg_bot_url_fits(int len)
{
    return ((len >= 0) && ((size_t)len < TG_BOT_URL_MAX));
}

static void tg_bot_send(const char *chat_id, const char *text)
{
    cJSON *body = cJSON_CreateObject();
    if (body == NULL) {
        ESP_LOGE(TAG, "No memory for a message to %s", chat_id);
        return;
    }
    cJSON_AddStringToObject(body, "chat_id", chat_id);
    cJSON_AddStringToObject(body, "text", text);
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (json == NULL) {
        ESP_LOGE(TAG, "No memory for a message to %s", chat_id);
        return;
    }

    char url[TG_BOT_URL_MAX];
    int url_len = snprintf(url, sizeof(url), "%s" TG_BOT_API_PATH "%s/sendMessage", tg_bot_server, tg_bot_token);
    if (!tg_bot_url_fits(url_len)) {
        free(json);
        ESP_LOGE(TAG, "sendMessage to %s: the URL does not fit its buffer", chat_id);
        return;
    }
    int status = 0;
    esp_err_t err = tg_bot_exchange(url, json, &status);
    free(json);
    if (err != ESP_OK) {
        // Only a request that did not reach the server says it is offline: one that did can still
        // be refused (400 or 403 for an admin who never started the bot) by a server that works.
        tg_bot_note_result(err, status);
        ESP_LOGW(TAG, "sendMessage to %s failed: %s", chat_id, esp_err_to_name(err));
    } else if (status != TG_BOT_HTTP_OK) {
        ESP_LOGW(TAG, "sendMessage to %s answered HTTP %d", chat_id, status);
    }
}

// A bot token is "<digits>:<letters, digits, '_' and '-'>". Checked before the token goes into a
// URL, because esp_http_client logs a URL it cannot parse in full - token included.
static bool tg_bot_token_valid(const char *token)
{
    if (token[0] == '\0') {
        return false;
    }
    for (const char *c = token; *c != '\0'; c++) {
        bool ok = (((*c >= 'a') && (*c <= 'z')) || ((*c >= 'A') && (*c <= 'Z')) || ((*c >= '0') && (*c <= '9')) ||
                   (*c == '_') || (*c == '-') || (*c == ':'));
        if (!ok) {
            return false;
        }
    }
    return true;
}

// Switches the polls to another Bot API server or token. Update ids belong to one bot, and another
// server may number them its own way, so the offset reached before means nothing after either
// change, and the backlog is dropped as at boot.
static void tg_bot_use_settings(const char *server, const char *token)
{
    snprintf(tg_bot_server, sizeof(tg_bot_server), "%s", server);
    snprintf(tg_bot_token, sizeof(tg_bot_token), "%s", token);
    tg_bot_offset = 0;
    tg_bot_backlog_dropped = false;
    tg_bot_token_usable = tg_bot_token_valid(token);
    // What was known of the old server or token says nothing of the new: offline, and no reason,
    // until a request with them goes through or fails. A token with characters no bot token has is
    // never sent, and shows as that reason rather than as no token at all.
    bool token_set = (token[0] != '\0');
    tg_bot_set_error((token_set && (!tg_bot_token_usable)) ? TG_BOT_ERROR_BAD_TOKEN : "");
    __atomic_store_n(&tg_bot_online, false, __ATOMIC_SEQ_CST);
    __atomic_store_n(&tg_bot_enabled, token_set, __ATOMIC_SEQ_CST);
    // The next request makes a new client: its scheme, port and Host header come from the server
    // it is made for, and a connection left open while the bot was idle may have been closed by
    // the server since.
    if (tg_bot_http != NULL) {
        esp_http_client_cleanup(tg_bot_http);
        tg_bot_http = NULL;
    }

    if (token[0] == '\0') {
        ESP_LOGI(TAG, "No Telegram token: the bot is idle");
    } else if (!tg_bot_token_usable) {
        ESP_LOGE(TAG, "The Telegram token has characters no bot token has: the bot is idle");
    } else {
        ESP_LOGI(TAG, "Telegram token set, polling %s for updates", tg_bot_server);
    }
}

// Copies the next id of tg_admins into out (empty when it does not fit, so that it matches
// nobody) and moves *cursor past it. False once the list is exhausted.
static bool tg_bot_next_admin(const char **cursor, char *out, size_t len)
{
    const char *item = *cursor;
    if (*item == '\0') {
        return false;
    }
    const char *end = strchr(item, TG_BOT_ADMIN_SEPARATOR);
    size_t item_len = (end != NULL) ? (size_t)(end - item) : strlen(item);
    *cursor = (end != NULL) ? (end + 1) : (item + item_len);

    if (item_len >= len) {
        out[0] = '\0';
    } else {
        memcpy(out, item, item_len);
        out[item_len] = '\0';
    }
    return true;
}

static bool tg_bot_is_admin(const char *user_id)
{
    if (user_id[0] == '\0') {
        return false;
    }
    const char *cursor = tg_bot_admins;
    char admin[TG_BOT_ID_MAX];
    while (tg_bot_next_admin(&cursor, admin, sizeof(admin))) {
        if (strcmp(admin, user_id) == 0) {
            return true;
        }
    }
    return false;
}

// The text after the first space of a command, or NULL when nothing follows one.
static const char *tg_bot_args(const char *text)
{
    const char *space = strchr(text, TG_BOT_ARG_SEPARATOR);
    if ((space == NULL) || (space[1] == '\0')) {
        return NULL;
    }
    return (space + 1);
}

static void tg_bot_cmd_activate(const char *chat_id, const char *from_id, bool admin, const char *text)
{
    if (!admin) {
        tg_bot_send(chat_id, TG_BOT_TEXT_NO_PERMISSION);
        ESP_LOGW(TAG, "Unauthorized activation attempt from user %s", from_id);
        return;
    }

    // Over each limit of the store: an argument cut to fit here is then still too long for it, and
    // is refused instead of stored truncated.
    char target[TG_BOT_ID_MAX];
    char name[FP_STORE_TG_CHAT_NAME_MAX + 2];
    const char *args = tg_bot_args(text);
    const char *space = (args != NULL) ? strchr(args, TG_BOT_ARG_SEPARATOR) : NULL;

    if (args == NULL) {
        snprintf(target, sizeof(target), "%s", chat_id);
        snprintf(name, sizeof(name), TG_BOT_TEXT_GROUP_PREFIX "%s", target);
    } else if ((space != NULL) && (space > args)) {
        snprintf(target, sizeof(target), "%.*s", (int)(space - args), args);
        snprintf(name, sizeof(name), "%s", space + 1);
    } else {
        snprintf(target, sizeof(target), "%s", args);
        snprintf(name, sizeof(name), TG_BOT_TEXT_GROUP_PREFIX "%s", target);
    }

    esp_err_t err = fp_store_tg_chat_add(target, name);
    if (err == ESP_OK) {
        snprintf(tg_bot_reply, sizeof(tg_bot_reply), "Group activated successfully: %s", name);
        tg_bot_send(chat_id, tg_bot_reply);
        ESP_LOGI(TAG, "Group activated: %s (%s)", target, name);
    } else {
        tg_bot_send(chat_id, "Failed to activate group");
        ESP_LOGW(TAG, "Failed to activate group %s: %s", target, esp_err_to_name(err));
    }
}

static void tg_bot_cmd_deactivate(const char *chat_id, const char *from_id, bool admin, const char *text)
{
    if (!admin) {
        tg_bot_send(chat_id, TG_BOT_TEXT_NO_PERMISSION);
        ESP_LOGW(TAG, "Unauthorized deactivation attempt from user %s", from_id);
        return;
    }

    char target[TG_BOT_ID_MAX];
    const char *args = tg_bot_args(text);
    snprintf(target, sizeof(target), "%s", (args != NULL) ? args : chat_id);

    esp_err_t err = fp_store_tg_chat_remove(target);
    if (err == ESP_OK) {
        snprintf(tg_bot_reply, sizeof(tg_bot_reply), "Group deactivated successfully: %s", target);
        ESP_LOGI(TAG, "Group deactivated: %s", target);
    } else {
        snprintf(tg_bot_reply, sizeof(tg_bot_reply), "Failed to deactivate group: %s", target);
        ESP_LOGW(TAG, "Failed to deactivate group %s: %s", target, esp_err_to_name(err));
    }
    tg_bot_send(chat_id, tg_bot_reply);
}

static void tg_bot_cmd_reset(const char *chat_id)
{
    esp_err_t err = fp_store_tg_chat_clear();
    if (err == ESP_OK) {
        tg_bot_send(chat_id, "Allowed groups list reset successfully");
        ESP_LOGI(TAG, "Allowed groups reset");
    } else {
        tg_bot_send(chat_id, "Failed to reset allowed groups list");
        ESP_LOGW(TAG, "Failed to reset allowed groups: %s", esp_err_to_name(err));
    }
}

static void tg_bot_cmd_list(const char *chat_id)
{
    fp_store_tg_chat_t *chats = malloc(FP_STORE_TG_CHATS_MAX * sizeof(*chats));
    if (chats == NULL) {
        tg_bot_send(chat_id, "Failed to read allowed groups");
        return;
    }
    size_t count = fp_store_tg_chats_list(chats, FP_STORE_TG_CHATS_MAX);
    if (count == 0) {
        free(chats);
        tg_bot_send(chat_id, "No allowed groups defined");
        return;
    }

    size_t used = (size_t)snprintf(tg_bot_reply, sizeof(tg_bot_reply), "Allowed groups:\n");
    for (size_t i = 0; (i < count) && (used < sizeof(tg_bot_reply)); i++) {
        bool named = (chats[i].name[0] != '\0');
        int written = snprintf(tg_bot_reply + used, sizeof(tg_bot_reply) - used, "- %s%s%s%s\n", chats[i].id,
                               named ? " (" : "", chats[i].name, named ? ")" : "");
        if (written < 0) {
            break;
        }
        used += (size_t)written;
    }
    free(chats);
    tg_bot_send(chat_id, tg_bot_reply);
}

static void tg_bot_open_lock(void)
{
    if (!fp_lock_open()) {
        ESP_LOGW(TAG, "The lock did not take the request: an opening is already under way, or its task is not running");
    }
}

static void tg_bot_cmd_open(const char *chat_id, const char *username)
{
    fp_store_user_t user;
    if ((username != NULL) && fp_store_find_by_tg_username(username, &user)) {
        // A sender the store knows is decided by their own permission, whatever chat this is.
        if (user.enabled && user.allow_tg) {
            tg_bot_open_lock();
            fp_acs_report_telegram_event(FP_ACS_RESULT_GRANTED, user.id);
            tg_bot_send(chat_id, TG_BOT_TEXT_OPENED);
        } else {
            fp_acs_report_telegram_event(FP_ACS_RESULT_DENIED, user.id);
            tg_bot_send(chat_id, TG_BOT_TEXT_NOT_ALLOWED);
        }
    } else if (fp_store_tg_chat_is_allowed(chat_id)) {
        tg_bot_open_lock();
        fp_acs_report_telegram_event(FP_ACS_RESULT_GRANTED, FP_STORE_NO_USER);
        tg_bot_send(chat_id, TG_BOT_TEXT_OPENED);
    } else {
        fp_acs_report_telegram_event(FP_ACS_RESULT_UNKNOWN, FP_STORE_NO_USER);
        tg_bot_send(chat_id, TG_BOT_TEXT_NOT_ACTIVATED);
        ESP_LOGW(TAG, "Unauthorized open attempt from chat %s", chat_id);
    }
}

// The same commands, matched the same way, as pp-lock: /activate and /deactivate by prefix, the
// rest exactly, and /reset and /list ignored for anyone but an admin.
static void tg_bot_handle_message(const char *chat_id, const char *from_id, const char *username, const char *text)
{
    bool admin = tg_bot_is_admin(from_id);

    if (strncmp(text, TG_BOT_CMD_ACTIVATE, sizeof(TG_BOT_CMD_ACTIVATE) - 1) == 0) {
        tg_bot_cmd_activate(chat_id, from_id, admin, text);
    } else if (strncmp(text, TG_BOT_CMD_DEACTIVATE, sizeof(TG_BOT_CMD_DEACTIVATE) - 1) == 0) {
        tg_bot_cmd_deactivate(chat_id, from_id, admin, text);
    } else if (strcmp(text, TG_BOT_CMD_HELP) == 0) {
        tg_bot_send(chat_id, admin ? TG_BOT_TEXT_HELP_ADMIN : TG_BOT_TEXT_HELP_USER);
    } else if ((strcmp(text, TG_BOT_CMD_RESET) == 0) && admin) {
        tg_bot_cmd_reset(chat_id);
    } else if ((strcmp(text, TG_BOT_CMD_LIST) == 0) && admin) {
        tg_bot_cmd_list(chat_id);
    } else if ((strcmp(text, TG_BOT_CMD_OPEN) == 0) || (strcmp(text, TG_BOT_CMD_OPEN_CAPITAL) == 0)) {
        tg_bot_cmd_open(chat_id, username);
    }
}

// A Telegram id as the decimal string the store and tg_admins use. Ids have at most 52
// significant bits, so the double cJSON parses them into holds them exactly.
static bool tg_bot_json_id(const cJSON *item, char *buf, size_t len)
{
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    snprintf(buf, len, "%" PRId64, (int64_t)item->valuedouble);
    return true;
}

static void tg_bot_handle_update(const cJSON *update)
{
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(update, "message");
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(message, "text");
    const cJSON *chat = cJSON_GetObjectItemCaseSensitive(message, "chat");
    const cJSON *from = cJSON_GetObjectItemCaseSensitive(message, "from");
    const cJSON *username = cJSON_GetObjectItemCaseSensitive(from, "username");

    char chat_id[TG_BOT_ID_MAX];
    bool has_chat = tg_bot_json_id(cJSON_GetObjectItemCaseSensitive(chat, "id"), chat_id, sizeof(chat_id));
    if ((!cJSON_IsString(text)) || (!has_chat)) {
        return;
    }
    char from_id[TG_BOT_ID_MAX];
    if (!tg_bot_json_id(cJSON_GetObjectItemCaseSensitive(from, "id"), from_id, sizeof(from_id))) {
        from_id[0] = '\0';
    }

    tg_bot_handle_message(chat_id, from_id, cJSON_IsString(username) ? username->valuestring : NULL,
                          text->valuestring);
}

// An update that did not fit in the response buffer is skipped by its update_id, which leads the
// update and so is in the part that did fit. Commands are a few bytes; what overflows is a long
// message that was not meant for the bot.
static void tg_bot_skip_oversized(void)
{
    const char *key = strstr(tg_bot_response, TG_BOT_UPDATE_ID_KEY);
    if (key == NULL) {
        ESP_LOGW(TAG, "An update did not fit in %d bytes and its id could not be read", TG_BOT_RESPONSE_MAX);
        return;
    }
    int64_t id = strtoll(key + (sizeof(TG_BOT_UPDATE_ID_KEY) - 1), NULL, TG_BOT_DECIMAL_BASE);
    tg_bot_offset = id + 1;
    ESP_LOGW(TAG, "Update %" PRId64 " did not fit in %d bytes, skipped", id, TG_BOT_RESPONSE_MAX);
}

static void tg_bot_send_boot_message(void)
{
    const char *cursor = tg_bot_admins;
    char admin[TG_BOT_ID_MAX];
    while (tg_bot_next_admin(&cursor, admin, sizeof(admin))) {
        if (admin[0] != '\0') {
            tg_bot_send(admin, TG_BOT_TEXT_BOOTED);
        }
    }
}

static esp_err_t tg_bot_poll(void)
{
    // The first poll after boot or a server or token change drops the backlog: Telegram keeps
    // updates for a day, and an "open" sent while the device was off, or handled just before a
    // reboot that came ahead of the poll confirming it, must not open the door now. It asks for the
    // newest update only, which also makes Telegram forget the older ones, and takes that one as
    // handled. It returns at once, so the boot message does not wait out a long poll either.
    bool drop = (!tg_bot_backlog_dropped);
    int timeout_s = drop ? 0 : TG_BOT_POLL_TIMEOUT_S;
    int64_t offset = drop ? TG_BOT_OFFSET_LAST : tg_bot_offset;
    char url[TG_BOT_URL_MAX];
    int url_len = snprintf(url, sizeof(url), "%s" TG_BOT_API_PATH "%s/getUpdates?offset=%" PRId64
                           "&limit=%d&timeout=%d", tg_bot_server, tg_bot_token, offset, TG_BOT_POLL_LIMIT, timeout_s);
    if (!tg_bot_url_fits(url_len)) {
        ESP_LOGE(TAG, "getUpdates: the URL does not fit its buffer");
        return ESP_ERR_INVALID_SIZE;
    }

    int status = 0;
    esp_err_t err = tg_bot_exchange(url, NULL, &status);
    tg_bot_note_result(err, status);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "getUpdates failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status != TG_BOT_HTTP_OK) {
        // 401 or 404: the token is wrong; 409: another client polls this bot, or it has a webhook.
        ESP_LOGW(TAG, "getUpdates answered HTTP %d", status);
        return ESP_FAIL;
    }

    // Parsed before anything is sent: a send reuses the response buffer.
    cJSON *root = NULL;
    const cJSON *result = NULL;
    if (tg_bot_response_overflow) {
        tg_bot_skip_oversized();
    } else {
        root = cJSON_ParseWithLength(tg_bot_response, tg_bot_response_len);
        result = cJSON_GetObjectItemCaseSensitive(root, "result");
        if ((!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok"))) || (!cJSON_IsArray(result))) {
            ESP_LOGW(TAG, "getUpdates answered something that is not a list of updates");
            cJSON_Delete(root);
            return ESP_FAIL;
        }
    }

    if (!tg_bot_booted) {
        tg_bot_booted = true;
        tg_bot_send_boot_message();
    }

    const cJSON *update = NULL;
    cJSON_ArrayForEach(update, result) {
        const cJSON *update_id = cJSON_GetObjectItemCaseSensitive(update, "update_id");
        if (!cJSON_IsNumber(update_id)) {
            continue;
        }
        // Confirmed by the next poll whatever happens to it here, as FastBot2 does in pp-lock.
        tg_bot_offset = ((int64_t)update_id->valuedouble) + 1;
        if (drop) {
            ESP_LOGI(TAG, "Update %" PRId64 " dropped with the backlog", tg_bot_offset - 1);
        } else {
            tg_bot_handle_update(update);
        }
    }
    cJSON_Delete(root);
    tg_bot_backlog_dropped = true;
    return ESP_OK;
}

static void tg_bot_task(void *arg)
{
    (void)arg;
    char server[SETTING_ITEM_MAX_STR_LEN];
    char token[SETTING_ITEM_MAX_STR_LEN];

    while (1) {
        // The settings are read on every pass, so a change needs no restart hook: it is picked up
        // once the request under way, or the pause after it, is over.
        if (setting_items_read(KEY_TG_API_URL, server) != ESP_OK) {
            snprintf(server, sizeof(server), "%s", DEFAULT_TG_API_URL);
        }
        if (setting_items_read(KEY_TG_TOKEN, token) != ESP_OK) {
            token[0] = '\0';
        }
        if (setting_items_read(KEY_TG_ADMINS, tg_bot_admins) != ESP_OK) {
            tg_bot_admins[0] = '\0';
        }
        if ((strcmp(server, tg_bot_server) != 0) || (strcmp(token, tg_bot_token) != 0)) {
            tg_bot_use_settings(server, token);
        }

        if (!tg_bot_token_usable) {
            vTaskDelay(pdMS_TO_TICKS(TG_BOT_IDLE_MS));
        } else if (tg_bot_poll() != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(TG_BOT_RETRY_MS));
        }
    }
}

void tg_bot_start(void)
{
    // PSRAM first: internal RAM is what the TLS session needs, and PSRAM may be absent.
    tg_bot_response = heap_caps_malloc_prefer(TG_BOT_RESPONSE_MAX, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT);
    if (tg_bot_response == NULL) {
        ESP_LOGE(TAG, "No memory for the response buffer: the Telegram bot is off");
        return;
    }
    if (xTaskCreate(tg_bot_task, "tg_bot", TG_BOT_TASK_STACK_SIZE, NULL, TG_BOT_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the bot task: the Telegram bot is off");
        heap_caps_free(tg_bot_response);
        tg_bot_response = NULL;
    }
}

void tg_bot_get_status(bool *enabled, bool *online, char *error, size_t error_size)
{
    char reason[TG_BOT_ERROR_MAX] = "";
    *enabled = __atomic_load_n(&tg_bot_enabled, __ATOMIC_SEQ_CST);
    *online = ((*enabled) && __atomic_load_n(&tg_bot_online, __ATOMIC_SEQ_CST));
    if ((*enabled) && (!(*online))) {
        portENTER_CRITICAL(&tg_bot_error_lock);
        memcpy(reason, tg_bot_error, sizeof(reason));
        portEXIT_CRITICAL(&tg_bot_error_lock);
    }
    if (error_size > 0) {
        snprintf(error, error_size, "%s", reason);
    }
}

#endif // FP_ACS_ENABLED
