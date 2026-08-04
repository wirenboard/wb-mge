#include "unity.h"
#include "console_log.h"

#include "settings_manager.h"
#include "setting_items.h"

#include "cJSON.h"
#include "nvs.h"
#include <string.h>
#include <limits.h>
#include <float.h>

// -------------------------------------------------------------------
// Symbols exported by mocks
// -------------------------------------------------------------------
extern esp_err_t mock_setting_items_save_error;
extern esp_err_t mock_setting_items_validate_error;
extern int       mock_setting_items_save_call_count;
void             mock_setting_items_reset(void);

extern int       mock_settings_update_call_count;
// What settings_update_with_status() reports back as the result of the synchronous runtime
// cache-overlay apply; ESP_OK unless a test asks for a failure.
extern esp_err_t mock_settings_update_cache_apply_result;
void             mock_settings_update_reset(void);

// -------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------

static void reset_all(void)
{
    mock_setting_items_reset();
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

// Extract the optional "warnings" array from a response JSON (returns NULL when absent).
static cJSON *response_warnings(const cJSON *resp)
{
    cJSON *item = cJSON_GetObjectItem(resp, "warnings");
    if (!item || !cJSON_IsArray(item)) {
        return NULL;
    }
    return item;
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

// -------------------------------------------------------------------
// Port-collision validation (web server, cache Modbus server, RS-485 bridge gateways)
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

// The two RS-485 bridge gateways cannot both listen on the same port either — a pair the
// old check never compared.
void test_bridge_ports_equal_rejected(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "bridge_port_1 == bridge_port_2 (both tcp_bridge servers) -> success:false");
    LOG_MESSAGE();

    // NVS defaults: both ports are tcp_bridge + server; move port 2 onto port 1's port.
    cJSON *req = cJSON_CreateObject();
    cJSON *rs485 = cJSON_CreateObject();
    cJSON *bridge = cJSON_CreateObject();
    cJSON_AddNumberToObject(bridge, "port", 502);   // == bridge_port_1 (NVS default)
    cJSON_AddItemToObject(rs485, "bridge", bridge);
    cJSON_AddItemToObject(req, "rs485_2", rs485);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "two RS-485 bridge servers on the same TCP port must be rejected");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// A bridge gateway in CLIENT mode connects out to a remote port; it binds nothing locally,
// so its port may equal another listener's port.
void test_bridge_ports_equal_accepted_when_one_is_client(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "bridge_port_2 == bridge_port_1 but port 2 is a TCP client -> success:true");
    LOG_MESSAGE();

    cJSON *req = cJSON_CreateObject();
    cJSON *rs485 = cJSON_CreateObject();
    cJSON *bridge = cJSON_CreateObject();
    cJSON_AddStringToObject(bridge, "mode", BRIDGE_MODE_CLIENT_STR);
    cJSON_AddNumberToObject(bridge, "port", 502);   // remote port, nothing bound locally
    cJSON_AddItemToObject(rs485, "bridge", bridge);
    cJSON_AddItemToObject(req, "rs485_2", rs485);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp),
        "a TCP client bridge port is remote and must not be treated as a local listener");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// A port that is not in tcp_bridge mode runs no TCP gateway, so its stored bridge port
// cannot collide with anything.
void test_cache_port_equal_bridge_port_of_non_tcp_bridge_port_accepted(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cache_modbus_port == bridge_port_1 but port 1 is in repeater mode -> success:true");
    LOG_MESSAGE();

    setting_items_save(KEY_PORT_MODE1, PORT_MODE_REPEATER_STR);
    cJSON *req = make_request_int("cache_modbus_port", 502);  // == bridge_port_1, gateway not running
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp),
        "the bridge port of a non-tcp_bridge port must not be treated as a local listener");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// The web server port is always bound: a bridge gateway must not be moved onto it.
void test_bridge_port_equal_web_port_rejected(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "bridge_port_1 == web_port -> success:false");
    LOG_MESSAGE();

    // NVS default web_port = 80.
    cJSON *req = cJSON_CreateObject();
    cJSON *rs485 = cJSON_CreateObject();
    cJSON *bridge = cJSON_CreateObject();
    cJSON_AddNumberToObject(bridge, "port", 80);
    cJSON_AddItemToObject(rs485, "bridge", bridge);
    cJSON_AddItemToObject(req, "rs485_1", rs485);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "an RS-485 bridge gateway must not listen on the web server port");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// ... and the web server must not be moved onto the cache Modbus server port either.
void test_web_port_equal_cache_port_rejected(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "web_port == cache_modbus_port -> success:false");
    LOG_MESSAGE();

    // NVS defaults: cache_mb_port = 504, cache server enabled.
    cJSON *req = make_request_int("web_port", 504);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "the web server must not share its port with the cache Modbus server");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// A collision that ALREADY exists in the saved configuration (older firmware validated fewer
// pairs, so such devices are in the field) must not fail a request that touches neither of the
// colliding listeners — otherwise EVERY POST fails, including one that only changes a Wi-Fi
// password, and the device could never be repaired over the REST API field by field.
void test_inherited_collision_does_not_block_unrelated_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "inherited bridge_port_1 == bridge_port_2 + request touches no port -> success:true");
    LOG_MESSAGE();

    // Saved (inherited) broken config: both bridge gateways already listen on 502.
    setting_items_save(KEY_BRIDGE_PORT2, "502");

    // A request that changes something unrelated entirely.
    cJSON *req = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "sta_pass", "new_password");
    cJSON_AddItemToObject(req, "wifi", wifi);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp),
        "a collision already saved in NVS must not block a request that does not touch it");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// An accepted inherited collision leaves one of the two listeners unable to bind. The client gets
// success:true, so the only way it can learn about the dead port is the optional "warnings" array
// of the response — it must be there, and it must name both listeners and the contested port.
void test_inherited_collision_reported_in_response_warnings(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "inherited bridge_port_1 == bridge_port_2 + request touches no port -> success:true + warnings");
    LOG_MESSAGE();

    // Saved (inherited) broken config: both bridge gateways already listen on 502.
    setting_items_save(KEY_BRIDGE_PORT2, "502");

    // A request that changes something unrelated entirely.
    cJSON *req = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "sta_pass", "new_password");
    cJSON_AddItemToObject(req, "wifi", wifi);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp),
        "an inherited collision must not fail a request that does not touch it");

    cJSON *warnings = response_warnings(resp);
    TEST_ASSERT_NOT_NULL_MESSAGE(warnings,
        "the accepted collision must be reported back to the client in a warnings array");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, cJSON_GetArraySize(warnings),
        "exactly one colliding pair exists in the saved configuration");

    cJSON *warning = cJSON_GetArrayItem(warnings, 0);
    cJSON *code = cJSON_GetObjectItem(warning, "code");
    cJSON *message = cJSON_GetObjectItem(warning, "message");

    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsString(code), "warning must carry a machine-readable code");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("port_collision", code->valuestring,
        "the code identifies the warning as a port collision");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsString(message), "warning must carry a human-readable message");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(message->valuestring, "rs485_1"),
        "the message must name the first colliding listener");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(message->valuestring, "rs485_2"),
        "the message must name the second colliding listener");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(message->valuestring, "502"),
        "the message must name the contested TCP port");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// rs485_N.cache_en is written to NVS by the mapping loop and applied to the running ports by
// settings_update(). When that apply fails the settings ARE saved — the request succeeded and a
// reboot will pick the overlay up — but the cache is not where the user just put it, and nothing
// retries it before the next settings write. The response warning is the only way they find out.
void test_failed_cache_apply_reported_in_response_warnings(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cache_en saved but the runtime apply failed -> success:true + cache_apply_failed warning");
    LOG_MESSAGE();

    // The pool would not allocate when settings_update() moved the overlay.
    mock_settings_update_cache_apply_result = ESP_ERR_NO_MEM;

    cJSON *req = cJSON_CreateObject();
    cJSON *rs485 = cJSON_CreateObject();
    cJSON_AddBoolToObject(rs485, "cache_en", true);
    cJSON_AddItemToObject(req, "rs485_2", rs485);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp),
        "the write itself succeeded — a failed RUNTIME apply must not be reported as a failed "
        "save, or the UI would roll the field back and show the opposite of what NVS holds");

    cJSON *warnings = response_warnings(resp);
    TEST_ASSERT_NOT_NULL_MESSAGE(warnings,
        "a cache overlay that was saved but not applied must be reported back to the client");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, cJSON_GetArraySize(warnings), "exactly one warning");

    cJSON *warning = cJSON_GetArrayItem(warnings, 0);
    cJSON *code = cJSON_GetObjectItem(warning, "code");
    cJSON *message = cJSON_GetObjectItem(warning, "message");

    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsString(code), "warning must carry a machine-readable code");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("cache_apply_failed", code->valuestring,
        "the code identifies the warning as a failed runtime cache apply");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsString(message), "warning must carry a human-readable message");
    // The message also carries esp_err_to_name(cache_apply_err), which is what tells a retry
    // from a dead end — but only on the device: CONFIG_ESP_ERR_TO_NAME_LOOKUP is set in
    // sdkconfig and not in the unit-test build, where every code comes back as "UNKNOWN ERROR".
    // So the wording is what is pinned here, not the error name.
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(message->valuestring, "could not be applied"),
        "the message must say the setting was saved but not applied — the client is being told "
        "success:true, so the wording is the only thing that separates the two");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// The mirror image, and what keeps the test above honest: a settings write whose cache apply
// succeeded must carry no warning at all. Every POST /settings runs the apply, so a warning
// raised unconditionally would fire on requests that never mentioned caching.
void test_successful_cache_apply_reports_no_warning(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cache_en saved and applied -> no warnings field");
    LOG_MESSAGE();

    cJSON *req = cJSON_CreateObject();
    cJSON *rs485 = cJSON_CreateObject();
    cJSON_AddBoolToObject(rs485, "cache_en", true);
    cJSON_AddItemToObject(req, "rs485_2", rs485);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp), "the request must succeed");
    TEST_ASSERT_FALSE_MESSAGE(cJSON_HasObjectItem(resp, "warnings"),
        "an applied cache overlay is not something to warn about");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// A request that fails HALFWAY still reports the warnings the validation collected: a warning
// describes the saved configuration, not this request's verdict. The attach point had to move
// below the NVS writes when the cache-apply warning was added (that one is only known after
// settings_update()), so every early return grew an attach of its own — this pins that none of
// them lost its warnings on the way. The chosen exit is a Phase 2 NVS write failure; a Phase 1
// rejection cannot show this, because validate_port_collisions() is the last check in the chain
// and an earlier failure short-circuits it before it has collected anything.
void test_failed_write_still_reports_its_warnings(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "inherited collision + a failed NVS write -> success:false and the warning is still there");
    LOG_MESSAGE();

    // Saved (inherited) broken config: both bridge gateways already listen on 502.
    setting_items_save(KEY_BRIDGE_PORT2, "502");

    // A valid request — it passes every check, including the collision one that files the
    // warning — whose write to NVS then fails.
    cJSON *req = make_request_string("hostname", "my-device");
    cJSON *resp = NULL;
    mock_setting_items_save_error = ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp), "the failed write must be reported");

    cJSON *warnings = response_warnings(resp);
    TEST_ASSERT_NOT_NULL_MESSAGE(warnings,
        "the inherited collision is a fact about the saved configuration and must be reported "
        "whether or not this particular request went through");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, cJSON_GetArraySize(warnings), "exactly one warning");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// The warnings array is an OPTIONAL addition to the response: a request applied onto a clean
// configuration must produce exactly the response it produced before — not even an empty array.
void test_clean_request_reports_no_warnings(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "no collision in the saved configuration -> response carries no warnings field");
    LOG_MESSAGE();

    // NVS defaults (80/502/503/504) do not collide.
    cJSON *req = make_request_string("hostname", "my-device");
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp), "a clean request must succeed");
    TEST_ASSERT_FALSE_MESSAGE(cJSON_HasObjectItem(resp, "warnings"),
        "warnings must be absent when there is nothing to warn about");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// The same inherited collision must still be rejected once the request touches one of the two
// colliding listeners: it is then re-asserting the collision, not merely inheriting it.
void test_inherited_collision_still_rejected_when_request_touches_a_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "inherited bridge_port_1 == bridge_port_2 + request re-sends bridge_port_2 -> success:false");
    LOG_MESSAGE();

    setting_items_save(KEY_BRIDGE_PORT2, "502");   // inherited collision with bridge_port_1

    // The request re-asserts the colliding port instead of leaving it alone.
    cJSON *req = cJSON_CreateObject();
    cJSON *rs485 = cJSON_CreateObject();
    cJSON *bridge = cJSON_CreateObject();
    cJSON_AddNumberToObject(bridge, "port", 502);
    cJSON_AddItemToObject(rs485, "bridge", bridge);
    cJSON_AddItemToObject(req, "rs485_2", rs485);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "a request that carries one of the colliding ports must still be rejected");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// Turning a listener ON counts as touching it: enabling the cache Modbus server on a port that
// already equals a bridge port introduces the collision even though the request carries no port
// value at all.
void test_enabling_cache_server_onto_a_bridge_port_rejected(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cache_modbus_server_enabled:true with cache port == bridge port -> success:false");
    LOG_MESSAGE();

    // Server off and parked on the port bridge 1 listens on (no collision while it is off).
    setting_items_save(KEY_CACHE_MODBUS_SERVER_ENABLED, "false");
    setting_items_save(KEY_CACHE_MODBUS_PORT, "502");   // == bridge_port_1 (NVS default)

    cJSON *req = make_request_bool("cache_modbus_server_enabled", true);
    cJSON *resp = NULL;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "enabling the cache server onto a port a bridge gateway already uses must be rejected");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// -------------------------------------------------------------------
// W8: port_mode / cache_en export & import round-trip
// -------------------------------------------------------------------

// settings_build_response_json (GET /settings, i.e. the settings export) must
// expose each port's transport mode (port_mode, string) and cache overlay flag
// (cache_en, bool) at the TOP LEVEL of rs485_N — next to baudrate/tx_disabled —
// and NOT inside the bridge sub-object. Without this the repeater mode is dropped
// from exported JSON (regression W8).
void test_build_response_includes_port_mode_and_cache_en(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "build_response: rs485_N carries port_mode (string) and cache_en (bool) at top level");
    LOG_MESSAGE();

    cJSON *resp = NULL;
    esp_err_t ret = settings_build_response_json(&resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_build_response_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");

    const char *ports[] = {"rs485_1", "rs485_2"};
    for (int i = 0; i < 2; i++) {
        cJSON *rs = cJSON_GetObjectItem(resp, ports[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(rs, "rs485_N object must be present");

        cJSON *pm = cJSON_GetObjectItem(rs, "port_mode");
        TEST_ASSERT_NOT_NULL_MESSAGE(pm, "port_mode must be present in rs485_N");
        TEST_ASSERT_TRUE_MESSAGE(cJSON_IsString(pm), "port_mode must be a string");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("tcp_bridge", pm->valuestring,
            "port_mode must reflect the stored NVS value (default tcp_bridge)");

        cJSON *ce = cJSON_GetObjectItem(rs, "cache_en");
        TEST_ASSERT_NOT_NULL_MESSAGE(ce, "cache_en must be present in rs485_N");
        TEST_ASSERT_TRUE_MESSAGE(cJSON_IsBool(ce), "cache_en must be a boolean");

        // Both fields must live at the top level, not inside the bridge sub-object.
        cJSON *bridge = cJSON_GetObjectItem(rs, "bridge");
        TEST_ASSERT_NOT_NULL_MESSAGE(bridge, "bridge sub-object must be present");
        TEST_ASSERT_FALSE_MESSAGE(cJSON_HasObjectItem(bridge, "port_mode"),
            "port_mode must NOT live inside the bridge sub-object");
        TEST_ASSERT_FALSE_MESSAGE(cJSON_HasObjectItem(bridge, "cache_en"),
            "cache_en must NOT live inside the bridge sub-object");
    }

    cJSON_Delete(resp);
}

// POST /settings (the settings import) carrying rs485_1.port_mode="repeater" and
// rs485_1.cache_en=true must persist both to NVS. This is the round-trip half that
// makes an exported repeater configuration actually restore on import (W8).
void test_rs485_port_mode_and_cache_en_saved_to_nvs(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "process: rs485_1.port_mode=repeater + cache_en=true -> persisted to NVS");
    LOG_MESSAGE();

    cJSON *req = cJSON_CreateObject();
    cJSON *rs485 = cJSON_CreateObject();
    cJSON_AddStringToObject(rs485, "port_mode", "repeater");
    cJSON_AddBoolToObject(rs485, "cache_en", true);
    cJSON_AddItemToObject(req, "rs485_1", rs485);

    cJSON *resp = NULL;
    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "settings_process_request_json must return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp), "success must be true");

    char pm[SETTING_ITEM_MAX_STR_LEN] = { 0 };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, setting_items_read(KEY_PORT_MODE1, pm),
        "port_mode_1 must be readable from NVS mock after the write");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("repeater", pm,
        "port_mode_1 must be persisted as 'repeater'");
    TEST_ASSERT_TRUE_MESSAGE(setting_items_read_bool(KEY_CACHE_EN_1),
        "cache_en_1 must be persisted as true");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// An invalid port_mode must be rejected in Phase 1 validation (success:false) and
// must not be written to NVS. The mock's validator returns the injected error, so
// this also proves port_mode is actually routed through validate_setting_from_json:
// were it missing from the mapping table, the field would be ignored and the
// request would succeed despite the injected validator error.
void test_rs485_invalid_port_mode_rejected(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "process: rs485_1.port_mode=bogus rejected by validation -> success:false");
    LOG_MESSAGE();

    cJSON *req = cJSON_CreateObject();
    cJSON *rs485 = cJSON_CreateObject();
    cJSON_AddStringToObject(rs485, "port_mode", "bogus");
    cJSON_AddItemToObject(req, "rs485_1", rs485);

    cJSON *resp = NULL;

    // Simulate validate_port_mode rejecting the value.
    mock_setting_items_validate_error = ESP_ERR_INVALID_ARG;

    esp_err_t ret = settings_process_request_json(req, &resp);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "settings_process_request_json must return ESP_OK so HTTP layer sends the error JSON");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "Response JSON must be allocated");
    TEST_ASSERT_FALSE_MESSAGE(response_success(resp),
        "an invalid port_mode must be rejected");

    // Validation happens in Phase 1, before any NVS write.
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_setting_items_save_call_count,
        "No NVS write should occur when port_mode fails validation");

    cJSON_Delete(req);
    cJSON_Delete(resp);
}

// -------------------------------------------------------------------
// T22: settings_manager_check_port_mode_collision (POST /ports/{n}/mode pre-check)
// Mirrors the POST /settings collision check, but keyed by a single port's new mode.
// -------------------------------------------------------------------

// Switching a port to tcp_bridge (saved bridge mode = server) whose saved gateway port equals the
// web server port introduces a new local listener collision → ESP_ERR_INVALID_STATE.
void test_port_mode_collision_tcp_bridge_onto_web_port_rejected(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "switch port 1 to tcp_bridge with bridge_port_1 == web_port -> INVALID_STATE");
    LOG_MESSAGE();

    // web_port defaults to 80; park port 1's saved gateway on the same port.
    setting_items_save(KEY_BRIDGE_PORT1, "80");

    esp_err_t ret = settings_manager_check_port_mode_collision(0, PORT_MODE_TCP_BRIDGE_STR);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_STATE, ret,
        "switching onto a port the web server already owns must be reported as a collision");
}

// No collision (default NVS: bridge_port_1 = 502, distinct from web_port/cache/bridge_port_2) → ESP_OK.
void test_port_mode_collision_no_collision_accepted(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "switch port 1 to tcp_bridge with a distinct gateway port -> ESP_OK");
    LOG_MESSAGE();

    // NVS defaults do not collide (80/502/503/504).
    esp_err_t ret = settings_manager_check_port_mode_collision(0, PORT_MODE_TCP_BRIDGE_STR);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "a tcp_bridge gateway on a free TCP port introduces no collision");
}

// Saved bridge mode = client binds nothing locally, so even a gateway port equal to web_port
// is not a local listener → ESP_OK.
void test_port_mode_collision_client_bridge_accepted(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "switch port 1 to tcp_bridge but saved bridge mode = client -> ESP_OK");
    LOG_MESSAGE();

    setting_items_save(KEY_BRIDGE_MODE1, BRIDGE_MODE_CLIENT_STR);
    setting_items_save(KEY_BRIDGE_PORT1, "80");   // == web_port, but client binds nothing locally

    esp_err_t ret = settings_manager_check_port_mode_collision(0, PORT_MODE_TCP_BRIDGE_STR);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "a client-mode bridge port is remote and must not be treated as a local listener");
}

// A non-tcp_bridge mode (disabled/passive/repeater) contributes no gateway listener, so it can
// never collide even if the saved gateway port equals another listener's → ESP_OK.
void test_port_mode_collision_non_tcp_bridge_modes_never_collide(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "switch port 1 to disabled/passive/repeater with bridge_port_1 == web_port -> ESP_OK");
    LOG_MESSAGE();

    // Park the saved gateway on web_port: it would collide IF the port ran a tcp_bridge server.
    setting_items_save(KEY_BRIDGE_PORT1, "80");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK,
        settings_manager_check_port_mode_collision(0, PORT_MODE_DISABLED_STR),
        "disabled mode binds no gateway, so it cannot collide");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK,
        settings_manager_check_port_mode_collision(0, PORT_MODE_PASSIVE_STR),
        "passive mode binds no gateway, so it cannot collide");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK,
        settings_manager_check_port_mode_collision(0, PORT_MODE_REPEATER_STR),
        "repeater mode binds no gateway, so it cannot collide");
}

// The same collision is detected for port 2 (index 1) — verifies the rs485_2 name/key mapping.
void test_port_mode_collision_port2_onto_web_port_rejected(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "switch port 2 to tcp_bridge with bridge_port_2 == web_port -> INVALID_STATE");
    LOG_MESSAGE();

    setting_items_save(KEY_BRIDGE_PORT2, "80");   // == web_port

    esp_err_t ret = settings_manager_check_port_mode_collision(1, PORT_MODE_TCP_BRIDGE_STR);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_STATE, ret,
        "the collision check must apply to port 2 (rs485_2) as well");
}

// An out-of-range port index has no listener to model — the guard returns ESP_OK.
void test_port_mode_collision_out_of_range_index_ok(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "out-of-range port index -> ESP_OK (nothing to check)");
    LOG_MESSAGE();

    esp_err_t ret = settings_manager_check_port_mode_collision(2, PORT_MODE_TCP_BRIDGE_STR);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "a port index outside the RS-485 range must be a no-op, not a rejection");
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

    // Port-collision validation (cache Modbus server vs RS-485 bridge gateway)
    RUN_TEST(test_cache_port_equal_bridge_port_from_nvs_rejected);
    RUN_TEST(test_cache_and_bridge_same_port_one_request_rejected);
    RUN_TEST(test_cache_port_distinct_from_bridge_accepted);
    RUN_TEST(test_cache_port_equal_bridge_when_server_disabled_accepted);
    RUN_TEST(test_bridge_ports_equal_rejected);
    RUN_TEST(test_bridge_ports_equal_accepted_when_one_is_client);
    RUN_TEST(test_cache_port_equal_bridge_port_of_non_tcp_bridge_port_accepted);
    RUN_TEST(test_bridge_port_equal_web_port_rejected);
    RUN_TEST(test_web_port_equal_cache_port_rejected);
    RUN_TEST(test_inherited_collision_does_not_block_unrelated_request);
    RUN_TEST(test_inherited_collision_reported_in_response_warnings);
    RUN_TEST(test_failed_cache_apply_reported_in_response_warnings);
    RUN_TEST(test_successful_cache_apply_reports_no_warning);
    RUN_TEST(test_failed_write_still_reports_its_warnings);
    RUN_TEST(test_clean_request_reports_no_warnings);
    RUN_TEST(test_inherited_collision_still_rejected_when_request_touches_a_port);
    RUN_TEST(test_enabling_cache_server_onto_a_bridge_port_rejected);

    // W8: port_mode / cache_en export & import round-trip
    RUN_TEST(test_build_response_includes_port_mode_and_cache_en);
    RUN_TEST(test_rs485_port_mode_and_cache_en_saved_to_nvs);
    RUN_TEST(test_rs485_invalid_port_mode_rejected);

    // T22: POST /ports/{n}/mode collision pre-check
    RUN_TEST(test_port_mode_collision_tcp_bridge_onto_web_port_rejected);
    RUN_TEST(test_port_mode_collision_no_collision_accepted);
    RUN_TEST(test_port_mode_collision_client_bridge_accepted);
    RUN_TEST(test_port_mode_collision_non_tcp_bridge_modes_never_collide);
    RUN_TEST(test_port_mode_collision_port2_onto_web_port_rejected);
    RUN_TEST(test_port_mode_collision_out_of_range_index_ok);

    return UNITY_END();
}
