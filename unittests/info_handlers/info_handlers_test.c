#include "unity.h"
#include "console_log.h"

#include "info_handlers.h"
#include "setting_items.h"
#include "cJSON.h"

#include <stdbool.h>

/* ---- Test shim exposing the static AP-clients JSON builder ----------------
 * info_build_ap_clients_json() is static in info_handlers.c. The production file
 * exposes it under __unittest_env__ via info_handlers_test_build_ap_clients_json()
 * (mirrors the cache_modbus_server.c test-shim pattern). */
esp_err_t info_handlers_test_build_ap_clients_json(cJSON **clients_json);

/* ---- Mock state exposed by mocks/setting_items.c ------------------------- */
void mock_setting_items_set_wifi_perm_disable(bool value);
void mock_setting_items_set_cache_modbus_port(int value);
void mock_setting_items_set_cache_modbus_server_enabled(bool value);
void mock_setting_items_reset(void);

/* ---- Mock state exposed by mocks/esp_wifi.c ------------------------------ */
extern int mock_esp_wifi_get_sta_list_called;
void       mock_esp_wifi_reset(void);

/* ---- Mock state exposed by mocks/cache_modbus_server.c ------------------- */
void mock_cache_modbus_server_set_port(int port);
void mock_cache_modbus_server_reset(void);

/* ---- Mock state exposed by mocks/json_utils.c ---------------------------- */
cJSON *mock_json_utils_take_response(void);
void   mock_json_utils_reset(void);

/* ---- setUp / tearDown ---------------------------------------------------- */

void setUp(void)
{
    mock_setting_items_reset();
    mock_esp_wifi_reset();
    mock_cache_modbus_server_reset();
    mock_json_utils_reset();
}

void tearDown(void)
{
    /* Frees a response a failing assertion left untaken. */
    mock_json_utils_reset();
}

/* Run GET /info and return the emitted response object (caller deletes it). */
static cJSON *run_info_get_handler(void)
{
    httpd_req_t req = {0};

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, info_get_handler(&req),
        "info_get_handler must return ESP_OK");

    cJSON *resp = mock_json_utils_take_response();
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "info_get_handler must emit a response object");
    return resp;
}

/* ===================================================================
 * Tests for info_build_ap_clients_json wifi_perm_disable early return
 * info_handlers.c:193-197
 * =================================================================== */

/* When Wi-Fi is permanently disabled, the AP-clients builder must short-circuit:
 * it returns ESP_OK with an empty JSON array and must NOT consult the Wi-Fi
 * driver (which was never started — calling it on hardware would crash). */
void test_ap_clients_perm_disable_true_returns_empty_array(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "ap_clients: wifi_perm_disable=true -> empty array, ESP_OK, no wifi driver call");
    LOG_MESSAGE();

    mock_setting_items_set_wifi_perm_disable(true);
    TEST_ASSERT_TRUE_MESSAGE(setting_items_read_bool(KEY_WIFI_PERM_DISABLE),
        "Precondition: wifi_perm_disable must read true");

    cJSON *clients = NULL;
    esp_err_t ret = info_handlers_test_build_ap_clients_json(&clients);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "perm-disabled early return must succeed (ESP_OK)");
    TEST_ASSERT_NOT_NULL_MESSAGE(clients, "clients JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsArray(clients),
        "clients JSON must be an array");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, cJSON_GetArraySize(clients),
        "clients array must be empty when Wi-Fi is permanently disabled");

    /* The early return must happen BEFORE the Wi-Fi station list is queried. */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_wifi_get_sta_list_called,
        "perm-disabled path must NOT query the Wi-Fi driver");

    cJSON_Delete(clients);
}

/* When Wi-Fi is NOT permanently disabled, the builder takes the normal path:
 * it queries the Wi-Fi station list (mocked to report zero stations) and returns
 * an empty array with ESP_OK. This locks the branch boundary: the early return
 * is taken only on perm-disable, otherwise the driver is consulted. */
void test_ap_clients_perm_disable_false_uses_normal_path(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "ap_clients: wifi_perm_disable=false -> normal path queries wifi driver");
    LOG_MESSAGE();

    mock_setting_items_set_wifi_perm_disable(false);
    TEST_ASSERT_FALSE_MESSAGE(setting_items_read_bool(KEY_WIFI_PERM_DISABLE),
        "Precondition: wifi_perm_disable must read false");

    cJSON *clients = NULL;
    esp_err_t ret = info_handlers_test_build_ap_clients_json(&clients);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "normal path must succeed (ESP_OK)");
    TEST_ASSERT_NOT_NULL_MESSAGE(clients, "clients JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsArray(clients), "clients JSON must be an array");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, cJSON_GetArraySize(clients),
        "no associated stations -> empty array on the normal path");

    /* The normal path must consult the Wi-Fi driver (proves the early return
     * was NOT taken when Wi-Fi is enabled). */
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_wifi_get_sta_list_called,
        "normal path must query the Wi-Fi station list exactly once");

    cJSON_Delete(clients);
}

/* ===================================================================
 * Tests for the cache_modbus_active_port field of GET /info
 * info_handlers.c: the cache Modbus block of info_get_handler()
 * =================================================================== */

/* Healthy case: the server listens on the port NVS asks for, so the runtime field
 * matches the configured one. Also pins that the two configured fields keep coming
 * from NVS — the new field is added alongside them, it does not replace them. */
void test_info_cache_modbus_active_port_reports_running_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "info: server running on the configured port -> active_port == cache_modbus_port");
    LOG_MESSAGE();

    mock_setting_items_set_cache_modbus_server_enabled(true);
    mock_setting_items_set_cache_modbus_port(504);
    mock_cache_modbus_server_set_port(504);

    cJSON *resp = run_info_get_handler();

    cJSON *active = cJSON_GetObjectItem(resp, "cache_modbus_active_port");
    TEST_ASSERT_NOT_NULL_MESSAGE(active, "cache_modbus_active_port must be present");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsNumber(active),
        "cache_modbus_active_port must be a number");
    TEST_ASSERT_EQUAL_INT_MESSAGE(504, active->valueint,
        "cache_modbus_active_port must carry the port the server is bound to");

    cJSON *configured = cJSON_GetObjectItem(resp, "cache_modbus_port");
    TEST_ASSERT_NOT_NULL_MESSAGE(configured, "cache_modbus_port must still be present");
    TEST_ASSERT_EQUAL_INT_MESSAGE(504, configured->valueint,
        "cache_modbus_port must still report the configured port from NVS");

    cJSON *enabled = cJSON_GetObjectItem(resp, "cache_modbus_server_enabled");
    TEST_ASSERT_NOT_NULL_MESSAGE(enabled,
        "cache_modbus_server_enabled must still be present");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsTrue(enabled),
        "cache_modbus_server_enabled must still report the configured flag from NVS");

    cJSON_Delete(resp);
}

/* The exact state this field exists for: NVS says the cache Modbus server is enabled on
 * port 504, but the server failed to start (or was stopped) and nobody is listening.
 * Before this field the failure was invisible over REST — it is only logged over UART —
 * so /info advertised enabled=true on a dead port. active_port must read 0 here while
 * the configured fields keep reporting what NVS holds. */
void test_info_cache_modbus_active_port_zero_when_server_not_listening(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "info: enabled=true in NVS but server not listening -> active_port == 0");
    LOG_MESSAGE();

    mock_setting_items_set_cache_modbus_server_enabled(true);
    mock_setting_items_set_cache_modbus_port(504);
    mock_cache_modbus_server_set_port(0);   /* server is not running */

    cJSON *resp = run_info_get_handler();

    cJSON *active = cJSON_GetObjectItem(resp, "cache_modbus_active_port");
    TEST_ASSERT_NOT_NULL_MESSAGE(active, "cache_modbus_active_port must be present");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, active->valueint,
        "cache_modbus_active_port must be 0 when nothing is listening");

    /* The configured fields must NOT be affected: they still describe the intent, and
     * that is precisely the mismatch the operator needs to see. */
    cJSON *configured = cJSON_GetObjectItem(resp, "cache_modbus_port");
    TEST_ASSERT_NOT_NULL_MESSAGE(configured, "cache_modbus_port must still be present");
    TEST_ASSERT_EQUAL_INT_MESSAGE(504, configured->valueint,
        "cache_modbus_port must keep reporting the configured port while the server is down");

    cJSON *enabled = cJSON_GetObjectItem(resp, "cache_modbus_server_enabled");
    TEST_ASSERT_NOT_NULL_MESSAGE(enabled,
        "cache_modbus_server_enabled must still be present");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsTrue(enabled),
        "cache_modbus_server_enabled must keep reporting true while the server is down");

    cJSON_Delete(resp);
}

/* Third state: a port change failed and the server was rolled back onto the port it was
 * serving before (settings_update.c does that instead of leaving it down), so the running
 * port legitimately differs from the configured one. A boolean "running" flag would
 * collapse this into the healthy case; the port number keeps it visible. */
void test_info_cache_modbus_active_port_may_differ_from_configured_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "info: failed port move -> active_port is the previous port, != cache_modbus_port");
    LOG_MESSAGE();

    mock_setting_items_set_cache_modbus_server_enabled(true);
    mock_setting_items_set_cache_modbus_port(1502);   /* newly configured port */
    mock_cache_modbus_server_set_port(504);           /* rolled back to the old one */

    cJSON *resp = run_info_get_handler();

    cJSON *active = cJSON_GetObjectItem(resp, "cache_modbus_active_port");
    TEST_ASSERT_NOT_NULL_MESSAGE(active, "cache_modbus_active_port must be present");
    TEST_ASSERT_EQUAL_INT_MESSAGE(504, active->valueint,
        "cache_modbus_active_port must report the port actually bound, not the configured one");

    cJSON *configured = cJSON_GetObjectItem(resp, "cache_modbus_port");
    TEST_ASSERT_NOT_NULL_MESSAGE(configured, "cache_modbus_port must still be present");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1502, configured->valueint,
        "cache_modbus_port must report the newly configured port");

    cJSON_Delete(resp);
}

/* ---- Test runner --------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_ap_clients_perm_disable_true_returns_empty_array);
    RUN_TEST(test_ap_clients_perm_disable_false_uses_normal_path);
    RUN_TEST(test_info_cache_modbus_active_port_reports_running_port);
    RUN_TEST(test_info_cache_modbus_active_port_zero_when_server_not_listening);
    RUN_TEST(test_info_cache_modbus_active_port_may_differ_from_configured_port);

    return UNITY_END();
}
