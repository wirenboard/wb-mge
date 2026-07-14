#include "unity.h"
#include "console_log.h"

#include "auth.h"
#include "esp_system.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

// Symbols exported by the mocks
extern int mock_xSemaphoreCreateMutex_called;
void mock_freertos_semaphore_reset(void);

extern int mock_esp_reset_reason_called;
void mock_esp_system_reset(void);
void mock_esp_system_set_reset_reason(esp_reset_reason_t reason);

void mock_esp_random_reset(void);
void mock_esp_random_push(uint32_t value);

void mock_setting_items_reset(void);

extern int mock_json_response_auth;
extern int mock_json_response_logout;
void mock_json_utils_reset(void);
void mock_json_utils_set_credentials(const char *login, const char *pass);

extern const char *mock_httpd_resp_set_hdr_last_value;
extern const char *mock_httpd_resp_set_status_last;
void mock_esp_http_server_reset(void);
void mock_esp_http_server_set_cookie(const char *value);

static httpd_req_t req;

void setUp(void)
{
    mock_freertos_semaphore_reset();
    mock_esp_system_reset();
    mock_esp_random_reset();
    mock_setting_items_reset();
    mock_json_utils_reset();
    mock_esp_http_server_reset();
    auth_reset_for_test();

    memset(&req, 0, sizeof(req));
}

void tearDown(void)
{
}

// Log in with the mocked credentials and return the session id the handler put in the cookie.
static uint32_t login_and_get_session_id(uint32_t random_value)
{
    mock_esp_random_push(random_value);
    mock_json_utils_set_credentials("admin", "wirenboard");
    TEST_ASSERT_EQUAL(ESP_OK, auth_login_handler(&req));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_json_response_auth, "the login must be accepted");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_httpd_resp_set_hdr_last_value, "a session cookie must be set");
    return random_value;
}

// Point the next request at a session id, as the browser's Cookie header would.
static void set_session_cookie(uint32_t session_id)
{
    static char cookie[32];
    snprintf(cookie, sizeof(cookie), "%u", (unsigned)session_id);
    mock_esp_http_server_set_cookie(cookie);
}

// ===================================================================
// auth_init() idempotency
// ===================================================================

// http_server_init() calls auth_init() on EVERY web server start, and the web server is
// restarted at runtime on every web_port change (plus once per rollback attempt when the new
// port will not bind). auth_init() used to call xSemaphoreCreateMutex() unconditionally, so
// each of those restarts overwrote session_mutex and leaked the previous handle — with no
// auth_deinit() anywhere to free it. A device stuck retrying an impossible port move leaked a
// mutex per settings write, forever.
void test_auth_init_creates_the_session_mutex_once(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test auth_init - the session mutex is created exactly once");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_OK, auth_init());
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called,
        "the first auth_init() must create the session mutex");

    // Three more web server restarts (a web_port change, a failed move, a rollback).
    TEST_ASSERT_EQUAL(ESP_OK, auth_init());
    TEST_ASSERT_EQUAL(ESP_OK, auth_init());
    TEST_ASSERT_EQUAL(ESP_OK, auth_init());

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called,
        "re-initializing auth must not create (and leak) another mutex");
}

// The session restore is a boot-time decision: it must not be re-run on a web server restart,
// where the live in-RAM buffer — not the reset reason — is the truth.
void test_auth_init_reads_the_reset_reason_only_on_the_first_call(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test auth_init - the reset reason is read once");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_OK, auth_init());
    TEST_ASSERT_EQUAL(ESP_OK, auth_init());

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_reset_reason_called,
        "only the first auth_init() may go down the boot path");
}

// Restarting the web server must not log anybody out: a web_port change (or any other setting
// that restarts the HTTP server) has to keep the operator's session alive, otherwise the very
// request that applied the setting is answered with a 401 on the new port.
void test_auth_init_keeps_the_sessions_across_an_http_server_restart(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test auth_init - sessions survive a web server restart");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_OK, auth_init());
    uint32_t session_id = login_and_get_session_id(0x1234ABCD);

    // The settings update task restarts the web server, which re-inits auth.
    TEST_ASSERT_EQUAL(ESP_OK, auth_init());

    set_session_cookie(session_id);
    TEST_ASSERT_TRUE_MESSAGE(auth_middleware_check(&req),
        "the session opened before the restart must still be valid after it");
}

// ===================================================================
// Session lifecycle
// ===================================================================

void test_auth_login_with_wrong_password_is_rejected(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test auth - wrong password opens no session");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_OK, auth_init());

    mock_json_utils_set_credentials("admin", "not-the-password");
    TEST_ASSERT_EQUAL(ESP_OK, auth_login_handler(&req));

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_json_response_auth, "the login must be refused");
    TEST_ASSERT_NULL_MESSAGE(mock_httpd_resp_set_hdr_last_value, "no session cookie may be handed out");
}

void test_auth_middleware_rejects_an_unknown_session(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test auth - an unknown session id gets a 401");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_OK, auth_init());
    login_and_get_session_id(0x1234ABCD);

    set_session_cookie(0xDEADBEEF);     // never issued
    TEST_ASSERT_FALSE(auth_middleware_check(&req));
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_httpd_resp_set_status_last);
}

void test_auth_logout_invalidates_the_session(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test auth - logout drops the session");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_OK, auth_init());
    uint32_t session_id = login_and_get_session_id(0x1234ABCD);

    set_session_cookie(session_id);
    TEST_ASSERT_TRUE(auth_middleware_check(&req));

    TEST_ASSERT_EQUAL(ESP_OK, auth_logout_handler(&req));
    TEST_ASSERT_EQUAL_INT(1, mock_json_response_logout);

    TEST_ASSERT_FALSE_MESSAGE(auth_middleware_check(&req),
        "the session id must stop working once it was logged out");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_auth_init_creates_the_session_mutex_once);
    RUN_TEST(test_auth_init_reads_the_reset_reason_only_on_the_first_call);
    RUN_TEST(test_auth_init_keeps_the_sessions_across_an_http_server_restart);

    RUN_TEST(test_auth_login_with_wrong_password_is_rejected);
    RUN_TEST(test_auth_middleware_rejects_an_unknown_session);
    RUN_TEST(test_auth_logout_invalidates_the_session);

    return UNITY_END();
}
