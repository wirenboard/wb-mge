#include "unity.h"

/*
 * tg_bot: the Telegram bot of the access control (main/telegram/tg_bot.h).
 *
 * tg_bot.c is #included so that the test can run one poll at a time, set the admins and reset its
 * statics. The Bot API server is a fake esp_http_client (mocks/esp_http_client.h): every request
 * lands in the trace as "GET getUpdates" or "POST sendMessage" (the token never does), next to
 * "open" from the lock and "report.<result>" from fp_acs. The task loop is driven pass by pass:
 * each pass starts by reading tg_api_url, and the read after the last pass longjmps back here.
 */

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "fp_acs_fake.h"
#include "fp_lock_fake.h"
#include "fp_store_fake.h"
#include "setting_items.h"
#include "setting_items_fake.h"
#include "trace.h"

// The spinlock tg_bot guards its error string with; FreeRTOS's port defines it on the device.
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED    0
static int tg_bot_test_critical_entries;
#define portENTER_CRITICAL(mux)         ((void)(mux), tg_bot_test_critical_entries++)
#define portEXIT_CRITICAL(mux)          ((void)(mux))

static esp_err_t tg_bot_test_setting_read(const char *key, char *value);
static void tg_bot_test_delay(const TickType_t ticks);

#define setting_items_read      tg_bot_test_setting_read
#define vTaskDelay              tg_bot_test_delay
#include "tg_bot.c"
#undef setting_items_read
#undef vTaskDelay

#if !FP_ACS_ENABLED
    #error "tg_bot_test needs FP_ACS_ENABLED: check DEFS in the Makefile"
#endif

#define TG_BOT_TEST_SERVER          "https://api.telegram.org"
#define TG_BOT_TEST_TOKEN           "123456:ABC-def_GHI"
#define TG_BOT_TEST_ADMINS          "111,222"
#define TG_BOT_TEST_TEXT_MAX        2048

// Chats and senders, as the Bot API sends them.
#define TG_BOT_TEST_GROUP           "{\"id\":-100123,\"title\":\"Office\",\"type\":\"supergroup\"}"
#define TG_BOT_TEST_UNTITLED        "{\"id\":-100123,\"title\":\"\",\"type\":\"group\"}"
#define TG_BOT_TEST_PRIVATE         "{\"id\":555,\"type\":\"private\",\"first_name\":\"Anna\"}"
#define TG_BOT_TEST_ADMIN           "{\"id\":111,\"is_bot\":false,\"first_name\":\"Admin\"}"
#define TG_BOT_TEST_SECOND_ADMIN    "{\"id\":222,\"is_bot\":false,\"first_name\":\"Boss\"}"
#define TG_BOT_TEST_STRANGER        "{\"id\":333,\"is_bot\":false,\"first_name\":\"Bob\"}"
#define TG_BOT_TEST_NEAR_ADMIN      "{\"id\":11,\"is_bot\":false,\"first_name\":\"Eve\"}"
#define TG_BOT_TEST_ANNA            "{\"id\":555,\"is_bot\":false,\"first_name\":\"Anna\",\"username\":\"Anna_K\"}"

typedef void (*tg_bot_test_hook_t)(void);

static jmp_buf            tg_bot_test_exit;
static bool               tg_bot_test_loop;
static unsigned           tg_bot_test_passes;
static unsigned           tg_bot_test_pass;
static tg_bot_test_hook_t tg_bot_test_hooks[8];
static int                tg_bot_test_update_id;
static char               tg_bot_test_text[TG_BOT_TEST_TEXT_MAX];
static char               tg_bot_test_chat[64];

esp_err_t esp_crt_bundle_attach(void *conf)
{
    (void)conf;
    return ESP_OK;
}


// ── Harness ──────────────────────────────────────────────────────────────────────────────

static esp_err_t tg_bot_test_setting_read(const char *key, char *value)
{
    if (tg_bot_test_loop && (strcmp(key, KEY_TG_API_URL) == 0)) {
        if (tg_bot_test_pass >= tg_bot_test_passes) {
            longjmp(tg_bot_test_exit, 1);
        }
        unsigned pass = tg_bot_test_pass++;
        trace_add("|");
        if (tg_bot_test_hooks[pass] != NULL) {
            tg_bot_test_hooks[pass]();
        }
    }
    return setting_items_read(key, value);
}

static void tg_bot_test_delay(const TickType_t ticks)
{
    trace_add("D%u", (unsigned)pdTICKS_TO_MS(ticks));
    vTaskDelay(ticks);
}

static void tg_bot_test_run_task(unsigned passes)
{
    tg_bot_test_loop = true;
    tg_bot_test_passes = passes;
    tg_bot_test_pass = 0;
    if (setjmp(tg_bot_test_exit) == 0) {
        tg_bot_task(NULL);
    }
    tg_bot_test_loop = false;
}

static void tg_bot_test_reset_statics(void)
{
    if (tg_bot_http != NULL) {
        esp_http_client_cleanup(tg_bot_http);
    }
    tg_bot_http = NULL;
    heap_caps_free(tg_bot_response);
    tg_bot_response = NULL;
    tg_bot_response_len = 0;
    tg_bot_response_overflow = false;
    tg_bot_server[0] = '\0';
    tg_bot_token[0] = '\0';
    tg_bot_token_usable = false;
    tg_bot_offset = 0;
    tg_bot_backlog_dropped = false;
    tg_bot_booted = false;
    tg_bot_admins[0] = '\0';
    tg_bot_reply[0] = '\0';
    tg_bot_enabled = false;
    tg_bot_online = false;
    tg_bot_error[0] = '\0';
}

// As after the first poll: the backlog is gone and the admins were greeted.
static void tg_bot_test_settled(void)
{
    tg_bot_backlog_dropped = true;
    tg_bot_booted = true;
    tg_bot_offset = 100;
    tg_bot_test_update_id = 100;
}

// Queues one update, a text message from a sender in a chat, as the answer to the next poll.
static void tg_bot_test_message(const char *chat, const char *from, const char *text)
{
    char body[1024];
    snprintf(body, sizeof(body),
             "{\"ok\":true,\"result\":[{\"update_id\":%d,\"message\":{\"message_id\":7,\"from\":%s,\"chat\":%s,"
             "\"date\":1700000000,\"text\":\"%s\"}}]}", tg_bot_test_update_id++, from, chat, text);
    esp_http_fake_respond(ESP_OK, 200, body);
}

// The request the bot sent last, and what its JSON body says.
static const esp_http_fake_request_t *tg_bot_test_last_request(void)
{
    TEST_ASSERT_GREATER_THAN_INT(0, esp_http_fake_request_count);
    return &esp_http_fake_requests[esp_http_fake_request_count - 1];
}

static const char *tg_bot_test_reply_text(void)
{
    const esp_http_fake_request_t *request = tg_bot_test_last_request();
    TEST_ASSERT_TRUE_MESSAGE(request->post, "the last request is no sendMessage");
    cJSON *root = cJSON_Parse(request->body);
    TEST_ASSERT_NOT_NULL(root);
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
    const cJSON *chat = cJSON_GetObjectItemCaseSensitive(root, "chat_id");
    TEST_ASSERT_TRUE(cJSON_IsString(text));
    TEST_ASSERT_TRUE(cJSON_IsString(chat));
    snprintf(tg_bot_test_text, sizeof(tg_bot_test_text), "%s", text->valuestring);
    snprintf(tg_bot_test_chat, sizeof(tg_bot_test_chat), "%s", chat->valuestring);
    cJSON_Delete(root);
    return tg_bot_test_text;
}

static void tg_bot_test_status(bool *enabled, bool *online, char *error)
{
    tg_bot_get_status(enabled, online, error, TG_BOT_ERROR_MAX);
}

void setUp(void)
{
    tg_bot_test_reset_statics();
    esp_http_fake_reset();
    mock_freertos_task_reset();
    mock_heap_caps_reset();
    mock_log_reset();
    mock_settings_reset();
    fp_acs_fake_reset();
    fp_lock_fake_reset();
    fp_store_fake_reset();
    trace_reset();
    tg_bot_test_loop = false;
    memset(tg_bot_test_hooks, 0, sizeof(tg_bot_test_hooks));
    tg_bot_test_update_id = 1;
    tg_bot_test_critical_entries = 0;

    tg_bot_start();
    TEST_ASSERT_NOT_NULL(tg_bot_response);
    tg_bot_use_settings(TG_BOT_TEST_SERVER, TG_BOT_TEST_TOKEN);
    snprintf(tg_bot_admins, sizeof(tg_bot_admins), "%s", TG_BOT_TEST_ADMINS);
    fp_store_fake_add_chat("-100123", "Office");
    mock_log_reset();
}

void tearDown(void)
{
    tg_bot_test_reset_statics();
    esp_http_fake_reset();
}


// ── Start ────────────────────────────────────────────────────────────────────────────────

static void test_start_without_memory_leaves_the_bot_off(void)
{
    tg_bot_test_reset_statics();
    mock_freertos_task_reset();
    mock_heap_caps_fail_count = 1;
    tg_bot_start();
    TEST_ASSERT_NULL(tg_bot_response);
    TEST_ASSERT_EQUAL_INT(0, mock_xTaskCreate_data.called);
}

static void test_start_without_a_task_frees_the_buffer(void)
{
    tg_bot_test_reset_statics();
    mock_freertos_task_reset();
    mock_xTaskCreate_data.should_fail = true;
    tg_bot_start();
    TEST_ASSERT_NULL(tg_bot_response);
    TEST_ASSERT_EQUAL_INT(1, mock_xTaskCreate_data.called);
}

static void test_response_buffer_prefers_psram(void)
{
    TEST_ASSERT_EQUAL_UINT32(MALLOC_CAP_SPIRAM, mock_heap_caps_last_caps);
}


// ── The token ────────────────────────────────────────────────────────────────────────────

static void test_bot_tokens_are_valid(void)
{
    TEST_ASSERT_TRUE(tg_bot_token_valid("123456789:AAHdqTcvCH1vGWJxfSeofSAs0K5PALDsaw"));
    TEST_ASSERT_TRUE(tg_bot_token_valid("1:a-b_c"));
    TEST_ASSERT_TRUE(tg_bot_token_valid(TG_BOT_TEST_TOKEN));
}

static void test_tokens_with_other_characters_are_invalid(void)
{
    const char *tokens[] = {"", "123 456", "12/34", "12?x", "12#x", "12%20", "12@host", "12:abc.def", "токен", "12\n",
                            "12&offset=1"};
    for (size_t i = 0; i < (sizeof(tokens) / sizeof(tokens[0])); i++) {
        TEST_ASSERT_FALSE_MESSAGE(tg_bot_token_valid(tokens[i]), tokens[i]);
    }
}


// ── Status ───────────────────────────────────────────────────────────────────────────────

static void test_no_token_leaves_the_bot_disabled_without_a_reason(void)
{
    tg_bot_use_settings(TG_BOT_TEST_SERVER, "");
    bool enabled = true;
    bool online = true;
    char error[TG_BOT_ERROR_MAX] = "junk";
    tg_bot_test_status(&enabled, &online, error);
    TEST_ASSERT_FALSE(enabled);
    TEST_ASSERT_FALSE(online);
    TEST_ASSERT_EQUAL_STRING("", error);
    TEST_ASSERT_FALSE(tg_bot_token_usable);
}

static void test_malformed_token_shows_as_invalid_token_not_as_no_token(void)
{
    // Regression (58e9199c1): it read as "not enabled" with no reason at all.
    tg_bot_use_settings(TG_BOT_TEST_SERVER, "123456:ABC def");
    bool enabled = false;
    bool online = true;
    char error[TG_BOT_ERROR_MAX] = "";
    tg_bot_test_status(&enabled, &online, error);
    TEST_ASSERT_TRUE(enabled);
    TEST_ASSERT_FALSE(online);
    TEST_ASSERT_EQUAL_STRING("invalid token", error);
    TEST_ASSERT_FALSE(tg_bot_token_usable);
    TEST_ASSERT_NULL(mock_log_find("ABC def"));
}

static void test_valid_token_is_enabled_but_offline_without_a_reason_until_a_poll(void)
{
    bool enabled = false;
    bool online = true;
    char error[TG_BOT_ERROR_MAX] = "junk";
    tg_bot_test_status(&enabled, &online, error);
    TEST_ASSERT_TRUE(enabled);
    TEST_ASSERT_FALSE(online);
    TEST_ASSERT_EQUAL_STRING("", error);
}

static void test_poll_answered_ok_puts_the_bot_online(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, tg_bot_poll());
    bool enabled = false;
    bool online = false;
    char error[TG_BOT_ERROR_MAX] = "junk";
    tg_bot_test_status(&enabled, &online, error);
    TEST_ASSERT_TRUE(online);
    TEST_ASSERT_EQUAL_STRING("", error);
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_matching("is online"));
}

static void test_poll_that_does_not_reach_the_server_names_the_error(void)
{
    esp_http_fake_respond(ESP_ERR_HTTP_CONNECT, 0, NULL);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_HTTP_CONNECT, tg_bot_poll());
    bool enabled = false;
    bool online = true;
    char error[TG_BOT_ERROR_MAX] = "";
    tg_bot_test_status(&enabled, &online, error);
    TEST_ASSERT_FALSE(online);
    TEST_ASSERT_EQUAL_STRING("ESP_ERR_HTTP_CONNECT", error);
    // The next request starts on a fresh connection.
    TEST_ASSERT_EQUAL_INT(1, esp_http_fake_close_calls);
}

static void test_poll_refused_by_the_server_names_the_status(void)
{
    esp_http_fake_respond(ESP_OK, 401, "{\"ok\":false,\"error_code\":401,\"description\":\"Unauthorized\"}");
    TEST_ASSERT_EQUAL_INT(ESP_FAIL, tg_bot_poll());
    bool enabled = false;
    bool online = true;
    char error[TG_BOT_ERROR_MAX] = "";
    tg_bot_test_status(&enabled, &online, error);
    TEST_ASSERT_FALSE(online);
    TEST_ASSERT_EQUAL_STRING("HTTP 401", error);
}

static void test_200_whose_answer_does_not_say_ok_is_an_unexpected_answer(void)
{
    const char *bodies[] = {"{\"ok\":false}", "<html>blocked</html>", "", "{\"result\":[]}"};
    for (size_t i = 0; i < (sizeof(bodies) / sizeof(bodies[0])); i++) {
        esp_http_fake_respond(ESP_OK, 200, bodies[i]);
        tg_bot_poll();
        bool enabled = false;
        bool online = true;
        char error[TG_BOT_ERROR_MAX] = "";
        tg_bot_test_status(&enabled, &online, error);
        TEST_ASSERT_FALSE_MESSAGE(online, bodies[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("unexpected answer", error, bodies[i]);
    }
}

static void test_reason_of_an_old_failure_is_not_shown_while_online(void)
{
    esp_http_fake_respond(ESP_OK, 502, "");
    tg_bot_poll();
    tg_bot_poll();
    bool enabled = false;
    bool online = false;
    char error[TG_BOT_ERROR_MAX] = "junk";
    tg_bot_test_status(&enabled, &online, error);
    TEST_ASSERT_TRUE(online);
    TEST_ASSERT_EQUAL_STRING("", error);
}

static void test_only_a_change_between_online_and_offline_is_logged(void)
{
    // The bot starts offline: a first failure is no change.
    tg_bot_test_settled();
    esp_http_fake_respond(ESP_OK, 502, "");
    esp_http_fake_respond(ESP_OK, 200, "{\"ok\":true,\"result\":[]}");
    esp_http_fake_respond(ESP_OK, 200, "{\"ok\":true,\"result\":[]}");
    esp_http_fake_respond(ESP_OK, 502, "");
    esp_http_fake_respond(ESP_OK, 503, "");
    for (int i = 0; i < 5; i++) {
        tg_bot_poll();
    }
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_matching("is online"));
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_matching("is offline: HTTP 502"));
    TEST_ASSERT_EQUAL_size_t(0, mock_log_count_matching("is offline: HTTP 503"));
    TEST_ASSERT_EQUAL_STRING("HTTP 503", tg_bot_error);
}

static void test_reason_is_cut_to_the_callers_buffer(void)
{
    esp_http_fake_respond(ESP_ERR_HTTP_CONNECT, 0, NULL);
    tg_bot_poll();
    bool enabled = false;
    bool online = false;
    char error[8];
    tg_bot_get_status(&enabled, &online, error, 5);
    TEST_ASSERT_EQUAL_STRING("ESP_", error);
    memset(error, '#', sizeof(error));
    tg_bot_get_status(&enabled, &online, error, 0);
    TEST_ASSERT_EQUAL_CHAR('#', error[0]);
    TEST_ASSERT_GREATER_THAN_INT(0, tg_bot_test_critical_entries);
}

static void test_change_of_server_or_token_starts_over_offline_on_a_new_client(void)
{
    tg_bot_poll();
    TEST_ASSERT_TRUE(tg_bot_online);
    TEST_ASSERT_EQUAL_INT(1, esp_http_fake_init_calls);

    tg_bot_use_settings("http://10.0.0.5:8081", TG_BOT_TEST_TOKEN);
    bool enabled = false;
    bool online = true;
    char error[TG_BOT_ERROR_MAX] = "junk";
    tg_bot_test_status(&enabled, &online, error);
    TEST_ASSERT_TRUE(enabled);
    TEST_ASSERT_FALSE(online);
    TEST_ASSERT_EQUAL_STRING("", error);
    TEST_ASSERT_EQUAL_INT(1, esp_http_fake_cleanup_calls);
    TEST_ASSERT_FALSE(tg_bot_backlog_dropped);
    TEST_ASSERT_EQUAL_INT64(0, tg_bot_offset);

    tg_bot_poll();
    TEST_ASSERT_EQUAL_INT(2, esp_http_fake_init_calls);
    TEST_ASSERT_EQUAL_STRING("http://10.0.0.5:8081", esp_http_fake_config_url);
    TEST_ASSERT_EQUAL_STRING("http://10.0.0.5:8081/bot" TG_BOT_TEST_TOKEN "/getUpdates?offset=-1&limit=1&timeout=0",
                             tg_bot_test_last_request()->url);
}

static void test_message_the_server_refuses_does_not_take_the_bot_offline(void)
{
    tg_bot_test_settled();
    tg_bot_poll();
    esp_http_fake_respond(ESP_OK, 403, "{\"ok\":false,\"description\":\"Forbidden: bot was blocked by the user\"}");
    tg_bot_send("111", "hello");
    TEST_ASSERT_TRUE(tg_bot_online);
    TEST_ASSERT_NOT_NULL(mock_log_find("answered HTTP 403"));
}

static void test_message_that_does_not_reach_the_server_takes_the_bot_offline(void)
{
    tg_bot_test_settled();
    tg_bot_poll();
    esp_http_fake_respond(ESP_ERR_HTTP_CONNECT, 0, NULL);
    tg_bot_send("111", "hello");
    bool enabled = false;
    bool online = true;
    char error[TG_BOT_ERROR_MAX] = "";
    tg_bot_test_status(&enabled, &online, error);
    TEST_ASSERT_FALSE(online);
    TEST_ASSERT_EQUAL_STRING("ESP_ERR_HTTP_CONNECT", error);
}

static void test_client_is_made_for_the_bare_server(void)
{
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING(TG_BOT_TEST_SERVER, esp_http_fake_config_url);
    TEST_ASSERT_EQUAL_INT(35000, esp_http_fake_config.timeout_ms);
    TEST_ASSERT_TRUE(esp_http_fake_config.crt_bundle_attach == esp_crt_bundle_attach);
    TEST_ASSERT_TRUE(esp_http_fake_config.event_handler == tg_bot_http_event);
}

static void test_client_that_cannot_be_made_fails_the_poll(void)
{
    esp_http_fake_init_fails = true;
    TEST_ASSERT_EQUAL_INT(ESP_FAIL, tg_bot_poll());
    TEST_ASSERT_EQUAL_INT(0, esp_http_fake_request_count);
    TEST_ASSERT_FALSE(tg_bot_online);
}


// ── Polling ──────────────────────────────────────────────────────────────────────────────

static void test_first_poll_drops_the_backlog_and_greets_the_admins(void)
{
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_STRANGER, "open");
    TEST_ASSERT_EQUAL_INT(ESP_OK, tg_bot_poll());

    TEST_ASSERT_EQUAL_STRING("GET getUpdates POST sendMessage POST sendMessage", trace_get());
    TEST_ASSERT_EQUAL_STRING(TG_BOT_TEST_SERVER "/bot" TG_BOT_TEST_TOKEN "/getUpdates?offset=-1&limit=1&timeout=0",
                             esp_http_fake_requests[0].url);
    // The "open" sent while the device was off does not open the door now.
    TEST_ASSERT_EQUAL_INT(0, fp_lock_fake_open_calls);
    TEST_ASSERT_EQUAL_INT(0, fp_acs_fake_telegram_reports);
    TEST_ASSERT_EQUAL_INT64(2, tg_bot_offset);
    TEST_ASSERT_TRUE(tg_bot_backlog_dropped);

    TEST_ASSERT_EQUAL_STRING("{\"chat_id\":\"111\",\"text\":\"Lock door controller booted\"}",
                             esp_http_fake_requests[1].body);
    TEST_ASSERT_EQUAL_STRING("application/json", esp_http_fake_requests[1].content_type);
    TEST_ASSERT_EQUAL_STRING("{\"chat_id\":\"222\",\"text\":\"Lock door controller booted\"}",
                             esp_http_fake_requests[2].body);
}

static void test_next_polls_wait_long_from_the_offset_and_greet_no_more(void)
{
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_STRANGER, "hello");
    tg_bot_poll();
    trace_reset();
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("GET getUpdates", trace_get());
    TEST_ASSERT_EQUAL_STRING(TG_BOT_TEST_SERVER "/bot" TG_BOT_TEST_TOKEN "/getUpdates?offset=2&limit=1&timeout=25",
                             tg_bot_test_last_request()->url);
    // A GET after a POST carries no body and no Content-Type.
    TEST_ASSERT_FALSE(tg_bot_test_last_request()->post);
    TEST_ASSERT_EQUAL_STRING("", tg_bot_test_last_request()->content_type);
}

static void test_greeting_skips_admin_ids_that_cannot_match(void)
{
    snprintf(tg_bot_admins, sizeof(tg_bot_admins), "111,,1234567890123456789012345,222");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_INT(3, esp_http_fake_request_count);
    TEST_ASSERT_NOT_NULL(strstr(esp_http_fake_requests[1].body, "\"111\""));
    TEST_ASSERT_NOT_NULL(strstr(esp_http_fake_requests[2].body, "\"222\""));
}

static void test_failed_first_poll_greets_nobody_and_drops_the_backlog_again(void)
{
    esp_http_fake_respond(ESP_ERR_HTTP_CONNECT, 0, NULL);
    tg_bot_poll();
    TEST_ASSERT_FALSE(tg_bot_booted);
    TEST_ASSERT_FALSE(tg_bot_backlog_dropped);
    tg_bot_poll();
    TEST_ASSERT_NOT_NULL(strstr(esp_http_fake_requests[1].url, "offset=-1&limit=1&timeout=0"));
    TEST_ASSERT_TRUE(tg_bot_booted);
}

static void test_each_update_moves_the_offset_past_it(void)
{
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_STRANGER, "hi");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_INT64(101, tg_bot_offset);
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_STRANGER, "open");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_INT64(102, tg_bot_offset);
    tg_bot_poll();
    TEST_ASSERT_NOT_NULL(strstr(tg_bot_test_last_request()->url, "offset=102&"));
}

static void test_update_too_big_for_the_buffer_is_skipped_by_its_id(void)
{
    tg_bot_test_settled();
    esp_http_fake_respond_long(200, "{\"ok\":true,\"result\":[{\"update_id\":777,\"message\":{\"text\":\"open", 17000);
    TEST_ASSERT_EQUAL_INT(ESP_OK, tg_bot_poll());
    TEST_ASSERT_EQUAL_INT64(778, tg_bot_offset);
    TEST_ASSERT_EQUAL_INT(0, fp_lock_fake_open_calls);
    TEST_ASSERT_TRUE(tg_bot_online);
    TEST_ASSERT_NOT_NULL(mock_log_find("Update 777 did not fit"));
}

static void test_oversized_answer_that_does_not_open_with_ok_is_offline(void)
{
    tg_bot_test_settled();
    esp_http_fake_respond_long(200, "{\"ok\":false,\"description\":\"", 17000);
    tg_bot_poll();
    TEST_ASSERT_FALSE(tg_bot_online);
    TEST_ASSERT_EQUAL_STRING("unexpected answer", tg_bot_error);
}

static void test_answer_that_is_no_list_of_updates_fails_the_poll(void)
{
    tg_bot_test_settled();
    esp_http_fake_respond(ESP_OK, 200, "{\"ok\":true,\"result\":{}}");
    TEST_ASSERT_EQUAL_INT(ESP_FAIL, tg_bot_poll());
    TEST_ASSERT_EQUAL_INT64(100, tg_bot_offset);
}

static void test_update_without_text_or_chat_is_ignored(void)
{
    tg_bot_test_settled();
    esp_http_fake_respond(ESP_OK, 200, "{\"ok\":true,\"result\":[{\"update_id\":100,\"message\":{\"chat\":"
                                       TG_BOT_TEST_GROUP ",\"sticker\":{}}},{\"update_id\":101,\"edited_message\":"
                                       "{\"text\":\"open\"}}]}");
    TEST_ASSERT_EQUAL_INT(ESP_OK, tg_bot_poll());
    TEST_ASSERT_EQUAL_STRING("GET getUpdates", trace_get());
    TEST_ASSERT_EQUAL_INT64(102, tg_bot_offset);
}


// ── open ─────────────────────────────────────────────────────────────────────────────────

static void test_open_in_an_allowed_chat_opens_and_names_the_chat_by_its_title(void)
{
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_STRANGER, "open");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("GET getUpdates open report.granted POST sendMessage", trace_get());
    TEST_ASSERT_EQUAL_UINT8(FP_STORE_NO_USER, fp_acs_fake_telegram_user_id);
    TEST_ASSERT_EQUAL_STRING("Office", fp_acs_fake_telegram_chat);
    TEST_ASSERT_EQUAL_STRING("Opened", tg_bot_test_reply_text());
    TEST_ASSERT_EQUAL_STRING("-100123", tg_bot_test_chat);
}

static void test_capitalised_open_works_and_nothing_else_does(void)
{
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_STRANGER, "Open");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_INT(1, fp_lock_fake_open_calls);

    const char *texts[] = {"OPEN", "open please", "/open", " open", "opened"};
    for (size_t i = 0; i < (sizeof(texts) / sizeof(texts[0])); i++) {
        tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_STRANGER, texts[i]);
        tg_bot_poll();
    }
    TEST_ASSERT_EQUAL_INT(1, fp_lock_fake_open_calls);
    TEST_ASSERT_EQUAL_INT(1, fp_acs_fake_telegram_reports);
}

static void test_private_chat_is_named_by_its_id(void)
{
    fp_store_fake_add_chat("555", "");
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_PRIVATE, TG_BOT_TEST_STRANGER, "open");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("555", fp_acs_fake_telegram_chat);
}

static void test_chat_with_an_empty_title_is_named_by_its_id(void)
{
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_UNTITLED, TG_BOT_TEST_STRANGER, "open");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("-100123", fp_acs_fake_telegram_chat);
}

static void test_open_in_a_chat_not_activated_is_reported_unknown_and_refused(void)
{
    tg_bot_test_settled();
    tg_bot_test_message("{\"id\":-100999,\"title\":\"Lobby\",\"type\":\"group\"}", TG_BOT_TEST_STRANGER, "open");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("GET getUpdates report.unknown POST sendMessage", trace_get());
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_UNKNOWN, fp_acs_fake_telegram_result);
    TEST_ASSERT_EQUAL_STRING("Lobby", fp_acs_fake_telegram_chat);
    TEST_ASSERT_EQUAL_STRING("This group is not activated. Admin command: /activate", tg_bot_test_reply_text());
}

static void test_known_user_allowed_telegram_opens_in_any_chat(void)
{
    fp_store_fake_add_user(5, "Anna", true, true, true, "anna_k");
    tg_bot_test_settled();
    tg_bot_test_message("{\"id\":-100999,\"title\":\"Lobby\",\"type\":\"group\"}", TG_BOT_TEST_ANNA, "open");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("GET getUpdates open report.granted POST sendMessage", trace_get());
    TEST_ASSERT_EQUAL_UINT8(5, fp_acs_fake_telegram_user_id);
    TEST_ASSERT_EQUAL_STRING("Lobby", fp_acs_fake_telegram_chat);
}

static void test_known_user_without_permission_is_denied_even_in_an_allowed_chat(void)
{
    fp_store_user_t *anna = fp_store_fake_add_user(5, "Anna", true, true, false, "anna_k");
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_ANNA, "open");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("GET getUpdates report.denied POST sendMessage", trace_get());
    TEST_ASSERT_EQUAL_UINT8(5, fp_acs_fake_telegram_user_id);
    TEST_ASSERT_EQUAL_STRING("You are not allowed to open the door", tg_bot_test_reply_text());

    // Disabled is refused the same way, whatever allow_tg says.
    anna->allow_tg = true;
    anna->enabled = false;
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_ANNA, "open");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_INT(0, fp_lock_fake_open_calls);
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_DENIED, fp_acs_fake_telegram_result);
}

static void test_open_the_lock_does_not_take_is_still_reported_and_answered(void)
{
    fp_lock_fake_open_result = false;
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_STRANGER, "open");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_GRANTED, fp_acs_fake_telegram_result);
    TEST_ASSERT_EQUAL_STRING("Opened", tg_bot_test_reply_text());
    TEST_ASSERT_NOT_NULL(mock_log_find("The lock did not take the request"));
}


// ── Admin commands ───────────────────────────────────────────────────────────────────────

static void test_admin_activates_this_chat_by_default(void)
{
    tg_bot_test_settled();
    tg_bot_test_message("{\"id\":-100999,\"title\":\"Lobby\",\"type\":\"group\"}", TG_BOT_TEST_ADMIN, "/activate");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("-100999", fp_store_fake_last_chat_id);
    TEST_ASSERT_EQUAL_STRING("Group -100999", fp_store_fake_last_chat_name);
    TEST_ASSERT_EQUAL_STRING("Group activated successfully: Group -100999", tg_bot_test_reply_text());
    TEST_ASSERT_TRUE(fp_store_tg_chat_is_allowed("-100999"));
}

static void test_admin_activates_another_chat_with_a_name(void)
{
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_PRIVATE, TG_BOT_TEST_SECOND_ADMIN, "/activate -100777 Back door");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("-100777", fp_store_fake_last_chat_id);
    TEST_ASSERT_EQUAL_STRING("Back door", fp_store_fake_last_chat_name);
    TEST_ASSERT_EQUAL_STRING("Group activated successfully: Back door", tg_bot_test_reply_text());
    TEST_ASSERT_EQUAL_STRING("555", tg_bot_test_chat);
}

static void test_admin_activates_another_chat_without_a_name(void)
{
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_PRIVATE, TG_BOT_TEST_ADMIN, "/activate -100777");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("-100777", fp_store_fake_last_chat_id);
    TEST_ASSERT_EQUAL_STRING("Group -100777", fp_store_fake_last_chat_name);
}

static void test_activation_by_anyone_else_is_refused(void)
{
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_NEAR_ADMIN, "/activate -100777");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_INT(0, fp_store_fake_chat_add_calls);
    TEST_ASSERT_EQUAL_STRING("Error: You don't have permission to use this command", tg_bot_test_reply_text());
}

static void test_too_long_chat_id_is_refused_not_stored_cut(void)
{
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_PRIVATE, TG_BOT_TEST_ADMIN, "/activate 1234567890123456789012345");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_INT(1, fp_store_fake_chat_add_calls);
    TEST_ASSERT_GREATER_THAN_size_t(FP_STORE_TG_CHAT_ID_MAX, strlen(fp_store_fake_last_chat_id));
    TEST_ASSERT_FALSE(fp_store_tg_chat_is_allowed(fp_store_fake_last_chat_id));
    TEST_ASSERT_EQUAL_STRING("Failed to activate group", tg_bot_test_reply_text());
}

static void test_too_long_chat_name_is_refused_not_stored_cut(void)
{
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_PRIVATE, TG_BOT_TEST_ADMIN,
                        "/activate -100777 nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn");
    tg_bot_poll();
    TEST_ASSERT_GREATER_THAN_size_t(FP_STORE_TG_CHAT_NAME_MAX, strlen(fp_store_fake_last_chat_name));
    TEST_ASSERT_EQUAL_STRING("Failed to activate group", tg_bot_test_reply_text());
}

static void test_admin_deactivates_this_chat_or_another(void)
{
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_ADMIN, "/deactivate");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("Group deactivated successfully: -100123", tg_bot_test_reply_text());
    TEST_ASSERT_FALSE(fp_store_tg_chat_is_allowed("-100123"));

    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_ADMIN, "/deactivate -100999");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("Failed to deactivate group: -100999", tg_bot_test_reply_text());

    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_STRANGER, "/deactivate");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("Error: You don't have permission to use this command", tg_bot_test_reply_text());
    TEST_ASSERT_EQUAL_INT(2, fp_store_fake_chat_remove_calls);
}

static void test_list_names_every_allowed_chat(void)
{
    fp_store_fake_add_chat("555", "");
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_PRIVATE, TG_BOT_TEST_ADMIN, "/list");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("Allowed groups:\n- -100123 (Office)\n- 555\n", tg_bot_test_reply_text());

    tg_bot_test_message(TG_BOT_TEST_PRIVATE, TG_BOT_TEST_ADMIN, "/reset");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("Allowed groups list reset successfully", tg_bot_test_reply_text());
    tg_bot_test_message(TG_BOT_TEST_PRIVATE, TG_BOT_TEST_ADMIN, "/list");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("No allowed groups defined", tg_bot_test_reply_text());
}

static void test_list_and_reset_are_ignored_for_anyone_else(void)
{
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_STRANGER, "/list");
    tg_bot_poll();
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_STRANGER, "/reset");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("GET getUpdates GET getUpdates", trace_get());
    TEST_ASSERT_EQUAL_INT(0, fp_store_fake_chat_clear_calls);
}

static void test_failed_reset_says_so(void)
{
    fp_store_fake_chat_result = ESP_FAIL;
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_PRIVATE, TG_BOT_TEST_ADMIN, "/reset");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING("Failed to reset allowed groups list", tg_bot_test_reply_text());
}

static void test_help_depends_on_who_asks(void)
{
    tg_bot_test_settled();
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_ADMIN, "/help");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING(TG_BOT_TEXT_HELP_ADMIN, tg_bot_test_reply_text());
    tg_bot_test_message(TG_BOT_TEST_GROUP, TG_BOT_TEST_STRANGER, "/help");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_STRING(TG_BOT_TEXT_HELP_USER, tg_bot_test_reply_text());
}

static void test_admin_ids_match_whole(void)
{
    TEST_ASSERT_TRUE(tg_bot_is_admin("111"));
    TEST_ASSERT_TRUE(tg_bot_is_admin("222"));
    TEST_ASSERT_FALSE(tg_bot_is_admin("11"));
    TEST_ASSERT_FALSE(tg_bot_is_admin("1111"));
    TEST_ASSERT_FALSE(tg_bot_is_admin("111,222"));
    TEST_ASSERT_FALSE(tg_bot_is_admin(""));
    tg_bot_admins[0] = '\0';
    TEST_ASSERT_FALSE(tg_bot_is_admin("111"));
}

static void test_message_without_a_sender_is_no_admins(void)
{
    tg_bot_test_settled();
    esp_http_fake_respond(ESP_OK, 200, "{\"ok\":true,\"result\":[{\"update_id\":100,\"message\":{\"chat\":"
                                       TG_BOT_TEST_GROUP ",\"text\":\"/reset\"}}]}");
    tg_bot_poll();
    TEST_ASSERT_EQUAL_INT(0, fp_store_fake_chat_clear_calls);
}


// ── The task ─────────────────────────────────────────────────────────────────────────────

static void tg_bot_test_hook_set_token(void)
{
    mock_settings_set(KEY_TG_TOKEN, TG_BOT_TEST_TOKEN);
}

static void tg_bot_test_hook_set_server(void)
{
    mock_settings_set(KEY_TG_API_URL, "http://10.0.0.5:8081");
}

static void test_task_without_a_token_idles_and_rereads_the_settings(void)
{
    tg_bot_test_reset_statics();
    esp_http_fake_reset();
    tg_bot_start();
    tg_bot_test_run_task(2);
    TEST_ASSERT_EQUAL_STRING("| D5000 | D5000", trace_get());
    TEST_ASSERT_EQUAL_STRING(DEFAULT_TG_API_URL, tg_bot_server);
    TEST_ASSERT_EQUAL_INT(6, mock_settings_reads);
    TEST_ASSERT_EQUAL_INT(0, esp_http_fake_init_calls);
}

static void test_task_never_sends_a_malformed_token(void)
{
    tg_bot_test_reset_statics();
    esp_http_fake_reset();
    tg_bot_start();
    mock_settings_set(KEY_TG_TOKEN, "123456:ABC/../x");
    tg_bot_test_run_task(2);
    TEST_ASSERT_EQUAL_STRING("| D5000 | D5000", trace_get());
    TEST_ASSERT_EQUAL_INT(0, esp_http_fake_init_calls);
    TEST_ASSERT_EQUAL_STRING("invalid token", tg_bot_error);
}

static void test_task_takes_up_a_token_and_then_long_polls(void)
{
    tg_bot_test_reset_statics();
    esp_http_fake_reset();
    tg_bot_start();
    tg_bot_test_hooks[1] = tg_bot_test_hook_set_token;
    tg_bot_test_run_task(3);
    TEST_ASSERT_EQUAL_STRING("| D5000 | GET getUpdates | GET getUpdates", trace_get());
    TEST_ASSERT_NOT_NULL(strstr(esp_http_fake_requests[0].url, "offset=-1&limit=1&timeout=0"));
    TEST_ASSERT_NOT_NULL(strstr(esp_http_fake_requests[1].url, "offset=0&limit=1&timeout=25"));
}

static void test_task_waits_10_s_after_a_failed_poll(void)
{
    tg_bot_test_reset_statics();
    esp_http_fake_reset();
    tg_bot_start();
    mock_settings_set(KEY_TG_TOKEN, TG_BOT_TEST_TOKEN);
    esp_http_fake_respond(ESP_ERR_HTTP_CONNECT, 0, NULL);
    tg_bot_test_run_task(2);
    TEST_ASSERT_EQUAL_STRING("| GET getUpdates D10000 | GET getUpdates", trace_get());
}

static void test_task_drops_the_backlog_again_on_a_new_server(void)
{
    tg_bot_test_reset_statics();
    esp_http_fake_reset();
    tg_bot_start();
    mock_settings_set(KEY_TG_TOKEN, TG_BOT_TEST_TOKEN);
    tg_bot_test_hooks[2] = tg_bot_test_hook_set_server;
    tg_bot_test_run_task(3);
    TEST_ASSERT_EQUAL_INT(3, esp_http_fake_request_count);
    TEST_ASSERT_EQUAL_STRING("http://10.0.0.5:8081/bot" TG_BOT_TEST_TOKEN "/getUpdates?offset=-1&limit=1&timeout=0",
                             esp_http_fake_requests[2].url);
    TEST_ASSERT_EQUAL_INT(1, esp_http_fake_cleanup_calls);
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_start_without_memory_leaves_the_bot_off);
    RUN_TEST(test_start_without_a_task_frees_the_buffer);
    RUN_TEST(test_response_buffer_prefers_psram);

    RUN_TEST(test_bot_tokens_are_valid);
    RUN_TEST(test_tokens_with_other_characters_are_invalid);

    RUN_TEST(test_no_token_leaves_the_bot_disabled_without_a_reason);
    RUN_TEST(test_malformed_token_shows_as_invalid_token_not_as_no_token);
    RUN_TEST(test_valid_token_is_enabled_but_offline_without_a_reason_until_a_poll);
    RUN_TEST(test_poll_answered_ok_puts_the_bot_online);
    RUN_TEST(test_poll_that_does_not_reach_the_server_names_the_error);
    RUN_TEST(test_poll_refused_by_the_server_names_the_status);
    RUN_TEST(test_200_whose_answer_does_not_say_ok_is_an_unexpected_answer);
    RUN_TEST(test_reason_of_an_old_failure_is_not_shown_while_online);
    RUN_TEST(test_only_a_change_between_online_and_offline_is_logged);
    RUN_TEST(test_reason_is_cut_to_the_callers_buffer);
    RUN_TEST(test_change_of_server_or_token_starts_over_offline_on_a_new_client);
    RUN_TEST(test_message_the_server_refuses_does_not_take_the_bot_offline);
    RUN_TEST(test_message_that_does_not_reach_the_server_takes_the_bot_offline);
    RUN_TEST(test_client_is_made_for_the_bare_server);
    RUN_TEST(test_client_that_cannot_be_made_fails_the_poll);

    RUN_TEST(test_first_poll_drops_the_backlog_and_greets_the_admins);
    RUN_TEST(test_next_polls_wait_long_from_the_offset_and_greet_no_more);
    RUN_TEST(test_greeting_skips_admin_ids_that_cannot_match);
    RUN_TEST(test_failed_first_poll_greets_nobody_and_drops_the_backlog_again);
    RUN_TEST(test_each_update_moves_the_offset_past_it);
    RUN_TEST(test_update_too_big_for_the_buffer_is_skipped_by_its_id);
    RUN_TEST(test_oversized_answer_that_does_not_open_with_ok_is_offline);
    RUN_TEST(test_answer_that_is_no_list_of_updates_fails_the_poll);
    RUN_TEST(test_update_without_text_or_chat_is_ignored);

    RUN_TEST(test_open_in_an_allowed_chat_opens_and_names_the_chat_by_its_title);
    RUN_TEST(test_capitalised_open_works_and_nothing_else_does);
    RUN_TEST(test_private_chat_is_named_by_its_id);
    RUN_TEST(test_chat_with_an_empty_title_is_named_by_its_id);
    RUN_TEST(test_open_in_a_chat_not_activated_is_reported_unknown_and_refused);
    RUN_TEST(test_known_user_allowed_telegram_opens_in_any_chat);
    RUN_TEST(test_known_user_without_permission_is_denied_even_in_an_allowed_chat);
    RUN_TEST(test_open_the_lock_does_not_take_is_still_reported_and_answered);

    RUN_TEST(test_admin_activates_this_chat_by_default);
    RUN_TEST(test_admin_activates_another_chat_with_a_name);
    RUN_TEST(test_admin_activates_another_chat_without_a_name);
    RUN_TEST(test_activation_by_anyone_else_is_refused);
    RUN_TEST(test_too_long_chat_id_is_refused_not_stored_cut);
    RUN_TEST(test_too_long_chat_name_is_refused_not_stored_cut);
    RUN_TEST(test_admin_deactivates_this_chat_or_another);
    RUN_TEST(test_list_names_every_allowed_chat);
    RUN_TEST(test_list_and_reset_are_ignored_for_anyone_else);
    RUN_TEST(test_failed_reset_says_so);
    RUN_TEST(test_help_depends_on_who_asks);
    RUN_TEST(test_admin_ids_match_whole);
    RUN_TEST(test_message_without_a_sender_is_no_admins);

    RUN_TEST(test_task_without_a_token_idles_and_rereads_the_settings);
    RUN_TEST(test_task_never_sends_a_malformed_token);
    RUN_TEST(test_task_takes_up_a_token_and_then_long_polls);
    RUN_TEST(test_task_waits_10_s_after_a_failed_poll);
    RUN_TEST(test_task_drops_the_backlog_again_on_a_new_server);

    return UNITY_END();
}
