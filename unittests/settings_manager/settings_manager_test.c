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
extern int       mock_cache_modbus_server_init_fail_port;   // per-port init failure injection (0 = off)
extern esp_err_t mock_cache_modbus_server_init_fail_error;  // error returned for the failing port
extern int       mock_cache_modbus_server_init_ports[];     // ordered log of init() ports since reset
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

// When NVS rejects a write for an RS485 bridge-subgroup field, the response must
// be success:false. This exercises the rs485_1.bridge.port save loop, which the
// base-field test does not reach.
void test_nvs_write_failure_on_rs485_bridge_setting_returns_success_false(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "NVS write failure on RS485 bridge setting → response success:false");
    LOG_MESSAGE();

    cJSON *req = cJSON_CreateObject();
    cJSON *rs485 = cJSON_CreateObject();
    cJSON *bridge = cJSON_CreateObject();
    cJSON_AddNumberToObject(bridge, "port", 502);
    cJSON_AddItemToObject(rs485, "bridge", bridge);
    cJSON_AddItemToObject(req, "rs485_1", rs485);

    cJSON *resp = NULL;

    mock_setting_items_save_error = ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK so HTTP layer sends the error JSON");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "success field must be false when an RS485 bridge NVS write fails");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// A successful request must notify the rest of the system via settings_update().
void test_successful_write_notifies_settings_update(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Successful request → settings_update() invoked exactly once");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_settings_update_call_count,
        "Precondition: settings_update must not have been called yet");

    cJSON *req = make_request_string("hostname", "my-device");
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "must return ESP_OK on success");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp), "success must be true");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_settings_update_call_count,
        "settings_update() must be called exactly once on the success path");

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

// The upper INT bound is exclusive: exactly INT_MAX must pass validation and
// reach the save path (the check must be strictly greater-than INT_MAX).
void test_integer_exactly_INT_MAX_passes_validation(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Integer value exactly INT_MAX passes validation (upper bound is exclusive)");
    LOG_MESSAGE();

    cJSON *req = make_request_int("web_port", (double)INT_MAX);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "must return ESP_OK for an INT_MAX value");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp),
        "success must be true: exactly INT_MAX is in range");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, mock_setting_items_save_call_count,
        "An in-range INT_MAX value must reach the NVS save path");

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
// Tests for settings_build_response_json wifi_perm_disable shaping
// ===================================================================

// When wifi_perm_disable is true in storage, the response JSON must carry
// "wifi_perm_disable": true and OMIT the "wifi" sub-object entirely.
// settings_manager.c:402-409
void test_build_response_wifi_perm_disable_true_omits_wifi(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "build_response: wifi_perm_disable=true -> flag true, no wifi object");
    LOG_MESSAGE();

    // Mark Wi-Fi as permanently disabled in mock storage.
    setting_items_save(KEY_WIFI_PERM_DISABLE, "true");
    TEST_ASSERT_TRUE_MESSAGE(setting_items_read_bool(KEY_WIFI_PERM_DISABLE),
        "Precondition: wifi_perm_disable must read true");

    cJSON *resp = NULL;
    esp_err_t ret = settings_build_response_json(&resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_build_response_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");

    cJSON *flag = cJSON_GetObjectItem(resp, "wifi_perm_disable");
    TEST_ASSERT_NOT_NULL_MESSAGE(flag, "wifi_perm_disable flag must be present");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsBool(flag), "wifi_perm_disable must be a boolean");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsTrue(flag),
        "wifi_perm_disable must be true when storage says it is disabled");

    TEST_ASSERT_FALSE_MESSAGE(cJSON_HasObjectItem(resp, "wifi"),
        "wifi sub-object must be omitted when Wi-Fi is permanently disabled");

    cJSON_Delete(resp);
}

// When wifi_perm_disable is false in storage, the response JSON must carry
// "wifi_perm_disable": false AND include the populated "wifi" sub-object.
// settings_manager.c:410-421
void test_build_response_wifi_perm_disable_false_includes_wifi(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "build_response: wifi_perm_disable=false -> flag false, wifi object present");
    LOG_MESSAGE();

    // Default mock storage has wifi_perm_disable=false; assert it explicitly.
    TEST_ASSERT_FALSE_MESSAGE(setting_items_read_bool(KEY_WIFI_PERM_DISABLE),
        "Precondition: wifi_perm_disable must read false");

    cJSON *resp = NULL;
    esp_err_t ret = settings_build_response_json(&resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_build_response_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");

    cJSON *flag = cJSON_GetObjectItem(resp, "wifi_perm_disable");
    TEST_ASSERT_NOT_NULL_MESSAGE(flag, "wifi_perm_disable flag must be present");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsBool(flag), "wifi_perm_disable must be a boolean");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsFalse(flag),
        "wifi_perm_disable must be false when storage says Wi-Fi is enabled");

    cJSON *wifi = cJSON_GetObjectItem(resp, "wifi");
    TEST_ASSERT_NOT_NULL_MESSAGE(wifi,
        "wifi sub-object must be present when Wi-Fi is not permanently disabled");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsObject(wifi), "wifi must be a JSON object");
    // The wifi group must actually be populated (mode comes from the wifi_mappings table).
    TEST_ASSERT_TRUE_MESSAGE(cJSON_HasObjectItem(wifi, "mode"),
        "wifi object must contain the mapped fields (e.g. mode)");

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

// When the cache server is enabled and the port changes, the server must be
// restarted: deinit followed by init on the NEW port.
void test_cache_server_restarted_on_port_change_when_enabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Cache server enabled, port changed → deinit+init on the new port");
    LOG_MESSAGE();

    // Use ports distinct from the RS-485 bridge gateway defaults (502/503): the cache
    // server cannot share a port with a bridge gateway (validate_port_collisions), so a
    // port-change restart test must move between two non-bridge ports.
    mock_cache_modbus_server_set_running_port(1502);
    setting_items_save(KEY_CACHE_MODBUS_SERVER_ENABLED, "true");
    setting_items_save(KEY_CACHE_MODBUS_PORT, "1502");

    cJSON *req = make_request_int("cache_modbus_port", 1503);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp), "success must be true");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_deinit_call_count,
        "cache_modbus_server_deinit must be called exactly once when the port changes");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_init_call_count,
        "cache_modbus_server_init must be called exactly once when the port changes");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1503, mock_cache_modbus_server_init_last_port,
        "cache_modbus_server_init must be called with the NEW port");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// When the cache server port changes and init(new_port) fails, the code must roll
// back by calling init(old_port). Drives per-port failure injection so init fails on
// the new port and succeeds on the old port. settings_manager.c:684-695
void test_cache_server_port_change_init_failure_rolls_back_to_old_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Cache server port change, init(new) fails -> rollback init(old)");
    LOG_MESSAGE();

    // Server currently running on old port 1502; request moves it to 1503.
    // Ports avoid the RS-485 bridge defaults (502/503) to clear validate_port_collisions.
    mock_cache_modbus_server_set_running_port(1502);
    setting_items_save(KEY_CACHE_MODBUS_SERVER_ENABLED, "true");
    setting_items_save(KEY_CACHE_MODBUS_PORT, "1502");

    // Arm per-port failure: init(1503) fails, init(1502) (rollback) succeeds.
    mock_cache_modbus_server_init_fail_port  = 1503;
    mock_cache_modbus_server_init_fail_error = ESP_FAIL;

    cJSON *req = make_request_int("cache_modbus_port", 1503);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");

    // The server is torn down once and re-init attempted on the new port.
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_deinit_call_count,
        "cache_modbus_server_deinit must be called exactly once for the restart");

    // Two init() calls in order: the failing init(new) then the rollback init(old).
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_cache_modbus_server_init_call_count,
        "init must be called twice: failed new-port attempt + rollback to old port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1503, mock_cache_modbus_server_init_ports[0],
        "first init must target the NEW port (1503)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1502, mock_cache_modbus_server_init_ports[1],
        "second init must roll back to the OLD port (1502)");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// When the cache server is enabled with a configured port of 0, init must fall
// back to the default CACHE_MODBUS_SERVER_PORT.
void test_cache_server_uses_default_port_when_configured_port_zero(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Cache server enabled with configured port 0 -> init called with default port");
    LOG_MESSAGE();

    setting_items_save(KEY_CACHE_MODBUS_SERVER_ENABLED, "true");
    setting_items_save(KEY_CACHE_MODBUS_PORT, "0");

    cJSON *req = make_request_bool("cache_modbus_server_enabled", true);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp), "success must be true");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_init_call_count,
        "cache_modbus_server_init must be called exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_MODBUS_SERVER_PORT, mock_cache_modbus_server_init_last_port,
        "configured port 0 must fall back to CACHE_MODBUS_SERVER_PORT");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// -------------------------------------------------------------------
// Port-collision validation (cache Modbus server vs RS-485 bridge gateway)
// -------------------------------------------------------------------

// cache_modbus_port equal to an RS-485 bridge port (taken from NVS) must be rejected:
// the cache server and a bridge gateway cannot listen on the same TCP port.
void test_cache_port_equal_bridge_port_from_nvs_rejected(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cache_modbus_port == bridge_port_1 (from NVS) -> success:false");
    LOG_MESSAGE();

    // NVS defaults seeded by the mock: bridge_port_1 = 502.
    cJSON *req = make_request_int("cache_modbus_port", 502);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK so HTTP layer sends the error JSON");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "cache_modbus_port equal to an RS-485 bridge gateway port must be rejected");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// cache_modbus_port and an RS-485 bridge port set to the SAME value in one request
// must be rejected (the bridge port is taken from the request, not NVS).
void test_cache_and_bridge_same_port_one_request_rejected(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cache_modbus_port == rs485_1.bridge.port in one request -> success:false");
    LOG_MESSAGE();

    cJSON *req = cJSON_CreateObject();
    cJSON *rs485 = cJSON_CreateObject();
    cJSON *bridge = cJSON_CreateObject();
    cJSON_AddNumberToObject(bridge, "port", 1700);
    cJSON_AddItemToObject(rs485, "bridge", bridge);
    cJSON_AddItemToObject(req, "rs485_1", rs485);
    cJSON_AddNumberToObject(req, "cache_modbus_port", 1700);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK so HTTP layer sends the error JSON");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "cache_modbus_port and a bridge port set to the same value in one request must be rejected");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// cache_modbus_port distinct from both RS-485 bridge ports must be accepted.
void test_cache_port_distinct_from_bridge_accepted(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cache_modbus_port distinct from bridge ports -> success:true");
    LOG_MESSAGE();

    // NVS defaults: bridge_port_1 = 502, bridge_port_2 = 503; 1234 collides with neither.
    cJSON *req = make_request_int("cache_modbus_port", 1234);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp),
        "cache_modbus_port distinct from both bridge ports must be accepted");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// A disabled cache Modbus server binds nothing, so its port may equal a bridge port.
void test_cache_port_equal_bridge_when_server_disabled_accepted(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cache_modbus_port == bridge port but cache server disabled -> success:true");
    LOG_MESSAGE();

    // Disable the cache server; bridge_port_1 stays 502 (NVS default).
    setting_items_save(KEY_CACHE_MODBUS_SERVER_ENABLED, "false");
    cJSON *req = make_request_int("cache_modbus_port", 502);  // == bridge_port_1, but server off
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp),
        "cache_modbus_port may equal a bridge port when the cache server is disabled");

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
    RUN_TEST(test_nvs_write_failure_on_rs485_bridge_setting_returns_success_false);
    RUN_TEST(test_successful_write_returns_success_true);
    RUN_TEST(test_successful_write_notifies_settings_update);

    // Integer range validation
    RUN_TEST(test_integer_above_INT_MAX_rejected_in_validation);
    RUN_TEST(test_integer_below_INT_MIN_rejected_in_validation);
    RUN_TEST(test_integer_in_range_passes_validation);
    RUN_TEST(test_integer_exactly_INT_MAX_passes_validation);

    // wifi_perm_disable NVS failure handling
    RUN_TEST(test_wifi_perm_disable_nvs_failure_returns_success_false);
    RUN_TEST(test_wifi_perm_disable_nvs_success_sets_flag);

    // settings_build_response_json wifi_perm_disable shaping
    RUN_TEST(test_build_response_wifi_perm_disable_true_omits_wifi);
    RUN_TEST(test_build_response_wifi_perm_disable_false_includes_wifi);

    // Cache server TOCTOU fix
    RUN_TEST(test_cache_server_started_when_enabled_and_not_running);
    RUN_TEST(test_cache_server_stopped_when_disabled_and_running);
    RUN_TEST(test_cache_server_no_op_when_already_running_and_enabled);
    RUN_TEST(test_cache_server_restarted_on_port_change_when_enabled);
    RUN_TEST(test_cache_server_port_change_init_failure_rolls_back_to_old_port);
    RUN_TEST(test_cache_server_uses_default_port_when_configured_port_zero);

    // Port-collision validation (cache Modbus server vs RS-485 bridge gateway)
    RUN_TEST(test_cache_port_equal_bridge_port_from_nvs_rejected);
    RUN_TEST(test_cache_and_bridge_same_port_one_request_rejected);
    RUN_TEST(test_cache_port_distinct_from_bridge_accepted);
    RUN_TEST(test_cache_port_equal_bridge_when_server_disabled_accepted);

    return UNITY_END();
}
