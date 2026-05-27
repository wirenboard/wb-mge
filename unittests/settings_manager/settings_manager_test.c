#include "unity.h"
#include "console_log.h"

#include "settings_manager.h"
#include "setting_items.h"
#include "cache_modbus_server.h"

#include "cJSON.h"
#include "nvs.h"
#include <string.h>
#include <limits.h>
#include <float.h>

// -------------------------------------------------------------------
// Symbols exported by mocks
// -------------------------------------------------------------------
extern esp_err_t mock_setting_items_save_error;
extern int       mock_setting_items_save_call_count;
void             mock_setting_items_reset(void);

extern esp_err_t mock_cache_modbus_server_init_error;
extern esp_err_t mock_cache_modbus_server_deinit_error;
extern int       mock_cache_modbus_server_init_call_count;
extern int       mock_cache_modbus_server_deinit_call_count;
extern int       mock_cache_modbus_server_init_last_port;
void             mock_cache_modbus_server_reset(void);
void             mock_cache_modbus_server_set_running_port(int port);

extern int mock_settings_update_call_count;
void       mock_settings_update_reset(void);

// -------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------

static void reset_all(void)
{
    mock_setting_items_reset();
    mock_cache_modbus_server_reset();
    mock_settings_update_reset();
}

// Build a minimal valid request JSON containing a single top-level string field.
static cJSON *make_request_string(const char *json_key, const char *value)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, json_key, value);
    return req;
}

// Build a minimal valid request JSON containing a single top-level int field.
static cJSON *make_request_int(const char *json_key, double value)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddNumberToObject(req, json_key, value);
    return req;
}

// Build a minimal valid request JSON containing a single top-level bool field.
static cJSON *make_request_bool(const char *json_key, bool value)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddBoolToObject(req, json_key, value);
    return req;
}

// Extract the "success" boolean from a response JSON.
static bool response_success(const cJSON *resp)
{
    cJSON *item = cJSON_GetObjectItem(resp, "success");
    if (!item) {
        return false;
    }
    return cJSON_IsTrue(item);
}

// Extract the "error" string from a response JSON (returns NULL when absent).
static const char *response_error(const cJSON *resp)
{
    cJSON *item = cJSON_GetObjectItem(resp, "error");
    if (!item || !cJSON_IsString(item)) {
        return NULL;
    }
    return item->valuestring;
}

// -------------------------------------------------------------------
// setUp / tearDown
// -------------------------------------------------------------------

void setUp(void)
{
    reset_all();
}

void tearDown(void)
{
}

// ===================================================================
// Tests for NVS write failure propagation (save returns success:false)
// ===================================================================

// When NVS rejects a write for a top-level setting, the response must be
// success:false — the client must not believe the write succeeded.
void test_nvs_write_failure_on_top_level_setting_returns_success_false(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "NVS write failure on top-level setting → response success:false");
    LOG_MESSAGE();

    cJSON *req = make_request_string("hostname", "my-device");
    cJSON *resp = NULL;

    mock_setting_items_save_error = ESP_ERR_NVS_NO_FREE_PAGES;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK so HTTP layer can send the error JSON");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "success field must be false when NVS write fails");
    TEST_ASSERT_NOT_NULL_MESSAGE(response_error(resp),
        "error field must be present when NVS write fails");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// Same check for a WiFi group setting.
void test_nvs_write_failure_on_wifi_setting_returns_success_false(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "NVS write failure on WiFi group setting → response success:false");
    LOG_MESSAGE();

    cJSON *req = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "mode", "ap");
    cJSON_AddItemToObject(req, "wifi", wifi);

    cJSON *resp = NULL;

    mock_setting_items_save_error = ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK so HTTP layer sends the error JSON");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "success field must be false when a WiFi NVS write fails");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// Same check for an Ethernet group setting.
void test_nvs_write_failure_on_ethernet_setting_returns_success_false(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "NVS write failure on Ethernet group setting → response success:false");
    LOG_MESSAGE();

    cJSON *req = cJSON_CreateObject();
    cJSON *eth = cJSON_CreateObject();
    cJSON_AddBoolToObject(eth, "dhcpc", true);
    cJSON_AddItemToObject(req, "ethernet", eth);

    cJSON *resp = NULL;

    mock_setting_items_save_error = ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK so HTTP layer sends the error JSON");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "success field must be false when an Ethernet NVS write fails");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// Same check for an RS485 base field.
void test_nvs_write_failure_on_rs485_setting_returns_success_false(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "NVS write failure on RS485 setting → response success:false");
    LOG_MESSAGE();

    cJSON *req = cJSON_CreateObject();
    cJSON *rs485 = cJSON_CreateObject();
    cJSON_AddNumberToObject(rs485, "baudrate", 9600);
    cJSON_AddItemToObject(req, "rs485_1", rs485);

    cJSON *resp = NULL;

    mock_setting_items_save_error = ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK so HTTP layer sends the error JSON");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "success field must be false when an RS485 NVS write fails");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// A completely successful write must still return success:true.
void test_successful_write_returns_success_true(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Successful NVS write → response success:true");
    LOG_MESSAGE();

    cJSON *req = make_request_string("hostname", "my-device");
    cJSON *resp = NULL;

    // No error injection — saves succeed
    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK on success");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp),
        "success field must be true when NVS write succeeds");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// ===================================================================
// Tests for integer range validation (double-to-int cast safety)
// ===================================================================

// A value larger than INT_MAX must be rejected during validation (Phase 1),
// before any NVS write attempt.
void test_integer_above_INT_MAX_rejected_in_validation(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Integer value > INT_MAX rejected in Phase 1 validation");
    LOG_MESSAGE();

    // web_port is an INT setting; use a value way outside the int range.
    double huge = (double)INT_MAX + 1e10;
    cJSON *req = make_request_int("web_port", huge);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK so HTTP layer sends the error JSON");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "success field must be false for out-of-range integer");

    // No NVS write should have been attempted at all (validation happened in Phase 1)
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_setting_items_save_call_count,
        "No NVS write should occur when the integer fails range validation");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// A value smaller than INT_MIN must be rejected similarly.
void test_integer_below_INT_MIN_rejected_in_validation(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Integer value < INT_MIN rejected in Phase 1 validation");
    LOG_MESSAGE();

    double tiny = (double)INT_MIN - 1e10;
    cJSON *req = make_request_int("web_port", tiny);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK so HTTP layer sends the error JSON");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "success field must be false for out-of-range integer");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_setting_items_save_call_count,
        "No NVS write should occur when the integer fails range validation");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// A valid in-range integer must pass validation and be saved.
void test_integer_in_range_passes_validation(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Integer value within [INT_MIN, INT_MAX] passes validation");
    LOG_MESSAGE();

    cJSON *req = make_request_int("web_port", 8080);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK for a valid port");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp),
        "success field must be true for a valid integer");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// ===================================================================
// Tests for wifi_perm_disable NVS write failure handling
// ===================================================================

// When NVS write of wifi_perm_disable fails, the response must be success:false
// and the in-memory flag must NOT have been changed (verified by checking that
// subsequent reads still reflect the old value).
void test_wifi_perm_disable_nvs_failure_returns_success_false(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "wifi_perm_disable NVS write failure → success:false, in-memory flag unchanged");
    LOG_MESSAGE();

    // Precondition: Wi-Fi is not permanently disabled
    TEST_ASSERT_FALSE_MESSAGE(setting_items_read_bool(KEY_WIFI_PERM_DISABLE),
        "Precondition: wifi_perm_disable must be false before the test");

    cJSON *req = make_request_bool("wifi_perm_disable", true);
    cJSON *resp = NULL;

    mock_setting_items_save_error = ESP_ERR_NVS_NO_FREE_PAGES;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK so HTTP layer sends the error JSON");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "success field must be false when wifi_perm_disable NVS write fails");

    // The NVS write failed, so the mock storage was not updated;
    // the in-memory flag as read back must still be false.
    TEST_ASSERT_FALSE_MESSAGE(setting_items_read_bool(KEY_WIFI_PERM_DISABLE),
        "wifi_perm_disable must remain false when NVS write failed");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// A successful wifi_perm_disable=true write must flip the flag and return success:true.
void test_wifi_perm_disable_nvs_success_sets_flag(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Successful wifi_perm_disable=true write → flag persisted, success:true");
    LOG_MESSAGE();

    TEST_ASSERT_FALSE_MESSAGE(setting_items_read_bool(KEY_WIFI_PERM_DISABLE),
        "Precondition: wifi_perm_disable must be false");

    cJSON *req = make_request_bool("wifi_perm_disable", true);
    cJSON *resp = NULL;

    // No error — write succeeds
    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp),
        "success field must be true when wifi_perm_disable write succeeds");

    // The mock storage was updated — reading back must return true.
    TEST_ASSERT_TRUE_MESSAGE(setting_items_read_bool(KEY_WIFI_PERM_DISABLE),
        "wifi_perm_disable must be true after a successful write");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// ===================================================================
// Tests for cache server TOCTOU fix
// ===================================================================

// When the server is not running and the request enables it, init must be called
// exactly once with the configured port (verifies the single snapshot is used).
void test_cache_server_started_when_enabled_and_not_running(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Cache server enabled and not running → init called once with correct port");
    LOG_MESSAGE();

    // Server is currently stopped (running_port == 0 from reset)
    // NVS says: enabled=true, port=504
    setting_items_save(KEY_CACHE_MODBUS_SERVER_ENABLED, "false");
    setting_items_save(KEY_CACHE_MODBUS_PORT, "504");

    cJSON *req = make_request_bool("cache_modbus_server_enabled", true);
    cJSON *resp = NULL;

    // Simulate a concurrent enable: NVS now says enabled, server not running
    setting_items_save(KEY_CACHE_MODBUS_SERVER_ENABLED, "true");

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp), "success must be true");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_init_call_count,
        "cache_modbus_server_init must be called exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(504, mock_cache_modbus_server_init_last_port,
        "cache_modbus_server_init must be called with the configured port");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// When the server is running and the request disables it, deinit must be called
// exactly once (verifies the single snapshot is used).
void test_cache_server_stopped_when_disabled_and_running(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Cache server disabled and currently running → deinit called exactly once");
    LOG_MESSAGE();

    // Server is currently running on port 504
    mock_cache_modbus_server_set_running_port(504);
    setting_items_save(KEY_CACHE_MODBUS_SERVER_ENABLED, "false");

    cJSON *req = make_request_bool("cache_modbus_server_enabled", false);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp), "success must be true");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_deinit_call_count,
        "cache_modbus_server_deinit must be called exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_cache_modbus_server_init_call_count,
        "cache_modbus_server_init must not be called when disabling the server");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// When the server is already running and the request enables it again, neither
// init nor deinit should be called (server already in the desired state).
void test_cache_server_no_op_when_already_running_and_enabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Cache server already running and enabled → no init/deinit calls");
    LOG_MESSAGE();

    mock_cache_modbus_server_set_running_port(504);
    setting_items_save(KEY_CACHE_MODBUS_SERVER_ENABLED, "true");

    cJSON *req = make_request_bool("cache_modbus_server_enabled", true);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp), "success must be true");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_cache_modbus_server_init_call_count,
        "init must not be called when server is already running");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_cache_modbus_server_deinit_call_count,
        "deinit must not be called when server should keep running");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// -------------------------------------------------------------------
// Test runner
// -------------------------------------------------------------------

int main(void)
{
    UNITY_BEGIN();

    // NVS write failure propagation
    RUN_TEST(test_nvs_write_failure_on_top_level_setting_returns_success_false);
    RUN_TEST(test_nvs_write_failure_on_wifi_setting_returns_success_false);
    RUN_TEST(test_nvs_write_failure_on_ethernet_setting_returns_success_false);
    RUN_TEST(test_nvs_write_failure_on_rs485_setting_returns_success_false);
    RUN_TEST(test_successful_write_returns_success_true);

    // Integer range validation
    RUN_TEST(test_integer_above_INT_MAX_rejected_in_validation);
    RUN_TEST(test_integer_below_INT_MIN_rejected_in_validation);
    RUN_TEST(test_integer_in_range_passes_validation);

    // wifi_perm_disable NVS failure handling
    RUN_TEST(test_wifi_perm_disable_nvs_failure_returns_success_false);
    RUN_TEST(test_wifi_perm_disable_nvs_success_sets_flag);

    // Cache server TOCTOU fix
    RUN_TEST(test_cache_server_started_when_enabled_and_not_running);
    RUN_TEST(test_cache_server_stopped_when_disabled_and_running);
    RUN_TEST(test_cache_server_no_op_when_already_running_and_enabled);

    return UNITY_END();
}
