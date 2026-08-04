#include "unity.h"
#include "console_log.h"

#include "settings_update.h"
#include "bridge.h"
#include "port_manager.h"
#include "network.h"
#include "http_server.h"
#include "update_rs485_mio_gpio_states.h"
#include "cache_modbus_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SETTINGS_UPDATE_TASK_STACK_SIZE         6144
#define SETTINGS_UPDATE_TASK_PRIORITY           5

#define HTTP_NETWORK_UPDATE_DELAY_MS            1000

void settings_update_reset(void);

// Symbols exported by the setting_items / cache_modbus_server mocks
extern bool mock_setting_items_cache_server_enabled;
extern int  mock_setting_items_cache_port;
void        mock_setting_items_reset(void);

extern esp_err_t mock_cache_modbus_server_deinit_error;     // makes deinit() report a failure
extern int       mock_cache_modbus_server_init_call_count;
extern int       mock_cache_modbus_server_deinit_call_count;
extern int       mock_cache_modbus_server_init_last_port;
extern int       mock_cache_modbus_server_init_fail_port;   // per-port init failure injection (0 = off)
extern esp_err_t mock_cache_modbus_server_init_fail_error;  // error returned for the failing port
extern int       mock_cache_modbus_server_init_ports[];     // ordered log of init() ports since reset
extern unsigned  mock_cache_modbus_server_init_call_seq;    // call id of the first init() since reset
extern unsigned  mock_cache_modbus_server_deinit_call_seq;  // call id of the first deinit() since reset
void             mock_cache_modbus_server_reset(void);
void             mock_cache_modbus_server_set_running_port(int port);

void setUp(void)
{
    mock_bridge_reset();
    mock_port_manager_reset();
    mock_network_reset();
    mock_http_server_reset();
    mock_update_rs485_mio_gpio_states_reset();
    mock_freertos_task_reset();
    mock_setting_items_reset();
    mock_cache_modbus_server_reset();
    settings_update_reset();
}

void tearDown(void)
{

}

void execute_task_function()
{
    mock_xTaskCreate_data.pvTaskCode(mock_xTaskCreate_data.pvParameters);
}

static void verify_settings_update_checks(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_update_rs485_control_called, "update_rs485_control should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_update_io_bus_control_called, "update_io_bus_control should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_update_serial_tx_disabled_called,
        "update_serial_tx_disabled should be called once per settings_update() call");

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        TEST_ASSERT_EQUAL_MESSAGE(1, mock_port_manager_check_settings_changed_called[i], "Port manager check_settings_changed should be called once per port");
    }

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_network_check_mdns_settings_changed_called, "mDNS check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_http_server_check_settings_changed_called, "HTTP server check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_network_check_eth_settings_changed_called, "Ethernet check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_network_check_wifi_settings_changed_called, "WiFi check should be called once");
}

static void verify_task_created(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xTaskCreate_data.called, "xTaskCreate should be called once");

    // pvTaskCode is checked inside xTaskCreate()

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "settings_update_task",
        mock_xTaskCreate_data.pcName,
        "Task name should be 'settings_update_task'"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        SETTINGS_UPDATE_TASK_STACK_SIZE,
        mock_xTaskCreate_data.usStackDepth,
        "Task stack depth should be 6144"
    );

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_xTaskCreate_data.pvParameters, "Task parameters should not be NULL");
    TEST_ASSERT_EQUAL_MESSAGE(SETTINGS_UPDATE_TASK_PRIORITY, mock_xTaskCreate_data.uxPriority, "Task priority should be 5");
}

static void verify_updates(
    bool expect_bridge0,
    bool expect_bridge1,
    bool expect_mdns,
    bool expect_http,
    bool expect_eth,
    bool expect_wifi
)
{
    bool bridge_expected[BRIDGES_COUNT] = {expect_bridge0, expect_bridge1};

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        int expected = bridge_expected[i] ? 1 : 0;

        TEST_ASSERT_EQUAL_MESSAGE(expected, mock_port_manager_apply_settings_called[i],
            bridge_expected[i] ? "Port manager apply_settings should be called once" : "Port manager apply_settings should not be called");

        if (bridge_expected[i]) {
            TEST_ASSERT_EQUAL_MESSAGE(i, mock_port_manager_apply_settings_index[i], "Port manager apply_settings index should be correct");
        }
    }

    int expected_mdns = expect_mdns ? 1 : 0;
    int expected_http = expect_http ? 1 : 0;
    int expected_eth = expect_eth ? 1 : 0;
    int expected_wifi = expect_wifi ? 1 : 0;

    TEST_ASSERT_EQUAL_MESSAGE(expected_mdns, mock_network_update_mdns_settings_called,
        expect_mdns ? "mDNS update should be called" : "mDNS update should not be called");

    TEST_ASSERT_EQUAL_MESSAGE(expected_http, mock_http_server_deinit_called,
        expect_http ? "HTTP server deinit should be called" : "HTTP server deinit should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(expected_http, mock_http_server_init_called,
        expect_http ? "HTTP server init should be called" : "HTTP server init should not be called");

    TEST_ASSERT_EQUAL_MESSAGE(expected_eth, mock_network_update_eth_settings_called,
        expect_eth ? "Ethernet update should be called" : "Ethernet update should not be called");

    TEST_ASSERT_EQUAL_MESSAGE(expected_wifi, mock_network_update_wifi_settings_called,
        expect_wifi ? "WiFi update should be called" : "WiFi update should not be called");
}

static void verify_delay_before_network_updates(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_vTaskDelay_data.called, "vTaskDelay should be called once before network updates");
    TEST_ASSERT_EQUAL_MESSAGE(
        pdMS_TO_TICKS(HTTP_NETWORK_UPDATE_DELAY_MS),
        mock_vTaskDelay_data.xTicksToDelay,
        "vTaskDelay should be called with the correct delay before network updates"
    );
}

static void verify_task_deleted(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_vTaskDelete_data.called, "vTaskDelete should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(NULL, mock_vTaskDelete_data.xTaskToDelete, "Task should delete itself (NULL)");
}

// Test the case when no settings have changed
void test_settings_update_no_changes(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - no changes");
    LOG_MESSAGE();

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTaskCreate_data.called, "xTaskCreate should not be called when no changes");
    verify_updates(false, false, false, false, false, false);
}

// Test update of bridge settings
void test_settings_update_bridge_ports_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - bridge ports changed");
    LOG_MESSAGE();

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        setUp();

        mock_port_manager_check_settings_changed_return_value[i] = true;

        esp_err_t result = settings_update();
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

        verify_settings_update_checks();
        verify_task_created();
        execute_task_function();
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelay_data.called, "vTaskDelay should not be called");

        if (i == 0) {
            verify_updates(true, false, false, false, false, false);
        } else if (i == 1) {
            verify_updates(false, true, false, false, false, false);
        }

        verify_task_deleted();
    }
}

// Test update of mDNS settings
void test_settings_update_mdns_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - mDNS changed");
    LOG_MESSAGE();

    mock_network_check_mdns_settings_changed_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    execute_task_function();
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelay_data.called, "vTaskDelay should not be called");
    verify_updates(false, false, true, false, false, false);
    verify_task_deleted();
}

// Test update of HTTP server settings
void test_settings_update_http_server_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - HTTP server changed");
    LOG_MESSAGE();

    mock_http_server_check_settings_changed_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    execute_task_function();
    verify_delay_before_network_updates();
    verify_updates(false, false, false, true, false, false);
    verify_task_deleted();
}

// Test update of Ethernet settings
void test_settings_update_ethernet_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - Ethernet changed");
    LOG_MESSAGE();

    mock_network_check_eth_settings_changed_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    execute_task_function();
    verify_delay_before_network_updates();
    verify_updates(false, false, false, false, true, false);
    verify_task_deleted();
}

// Test update of WiFi settings
void test_settings_update_wifi_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - WiFi changed");
    LOG_MESSAGE();

    mock_network_check_wifi_settings_changed_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    execute_task_function();
    verify_delay_before_network_updates();
    verify_updates(false, false, false, false, false, true);
    verify_task_deleted();
}

// Test update of all settings simultaneously
void test_settings_update_all_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - all settings changed");
    LOG_MESSAGE();

    for (unsigned i = 0; i < BRIDGES_COUNT; ++i) {
        mock_port_manager_check_settings_changed_return_value[i] = true;
    }

    mock_network_check_mdns_settings_changed_return_value = true;
    mock_http_server_check_settings_changed_return_value = true;
    mock_network_check_eth_settings_changed_return_value = true;
    mock_network_check_wifi_settings_changed_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    execute_task_function();
    verify_delay_before_network_updates();
    verify_updates(true, true, true, true, true, true);
    verify_task_deleted();
}

// Test task creation with failure
void test_settings_update_task_creation_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - task creation failure");
    LOG_MESSAGE();

    mock_port_manager_check_settings_changed_return_value[0] = true;
    mock_xTaskCreate_data.should_fail = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "Settings update should fail");

    verify_settings_update_checks();
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xTaskCreate_data.called, "xTaskCreate should be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelay_data.called, "vTaskDelay should not be called");
    verify_updates(false, false, false, false, false, false);
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelete_data.called, "vTaskDelete should not be called when task creation fails");
}

// Test repeated settings_update call when the task has not yet finished
void test_settings_update_task_already_running(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - task already running");
    LOG_MESSAGE();

    mock_http_server_check_settings_changed_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelay_data.called, "vTaskDelay should not be called");

    mock_http_server_check_settings_changed_return_value = false;
    mock_vTaskDelay_data.task_handle_reset_on_count = 3;

    result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(
        mock_vTaskDelay_data.task_handle_reset_on_count, mock_vTaskDelay_data.called, "vTaskDelay should be called 3 times"
    );
}

// The factory clock_out test forces V-out on and drives the TX pins of both ports plus
// the port-1 DE pin with the LEDC. A POST /settings during the test must not undo any of
// that: with the ports frozen, settings_update() must skip update_rs485_control() AND
// update_serial_tx_disabled() — the latter is not the pure software flag it looks like,
// it drives the port's dir_pin, which is the very DE pin the test holds HIGH. wb_test
// re-applies V-out when the test ends, and port_manager_apply_settings() re-applies
// tx_disabled from NVS as it brings each port back up, so nothing written during the test
// is lost.
//
// update_io_bus_control() must still run: the test does not drive the RS-485-2 pair (it
// leaves that transceiver's DE to the hardware pulldown), so the MIO controller is not in
// its way and io_bus_enabled has to reach the hardware right away — wb_test's exit path
// does not re-apply it.
void test_settings_update_ports_frozen_skips_rs485_and_tx_disabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - ports frozen (clock_out test active)");
    LOG_MESSAGE();

    mock_port_manager_ports_frozen_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed while the ports are frozen");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_update_rs485_control_called,
        "update_rs485_control must not run while the ports are frozen (it would restore the configured V-out)");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_update_serial_tx_disabled_called,
        "update_serial_tx_disabled must not run while the ports are frozen (it drives the DE pin the test holds HIGH)");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_update_io_bus_control_called,
        "update_io_bus_control must still run while the ports are frozen (the test does not own the I/O bus)");
}

// ===================================================================
// Runtime apply of the per-port cache overlay (rs485_N.cache_en)
//
// POST /settings maps rs485_N.cache_en onto the NVS keys cache_en_N and stops there, so
// something has to move the RUNTIME overlay to match. That something is settings_update():
// without the call, a device saved "cache on port 2" and went on sniffing port 1 for the rest
// of its uptime — /settings, /info and /cache/status each told a different story and nothing
// reconciled them short of a POST /ports/N/cache or a reboot.
// ===================================================================

// Every settings write reconciles the overlay, whether or not this request mentioned caching:
// the reconcile is a no-op when NVS already matches, and gating it on "did the request carry
// cache_en" would need settings_update() to know what the request said, which it does not.
void test_settings_update_applies_the_cache_overlay(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - cache overlay applied at runtime");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_port_manager_apply_cache_settings_called,
        "settings_update() must apply the stored cache overlay to the running ports — "
        "writing cache_en_N to NVS alone leaves the sniffer overlay wherever it was");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTaskCreate_data.called,
        "and it must do so synchronously: nothing changed, so there is no update task at all");
}

// Synchronously, and BEFORE the async task's release/acquire phases. Both halves matter: the
// result has to reach the POST /settings response (the task runs after it has been sent), and a
// port that is re-initialised in the same request must find the final overlay already in place,
// so port_init_mode() arms SNIFF_REASON_CACHE once rather than for the wrong port first.
void test_settings_update_applies_the_cache_overlay_before_the_ports(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - cache overlay applied before the ports");
    LOG_MESSAGE();

    mock_port_manager_check_settings_changed_return_value[0] = true;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_port_manager_apply_cache_settings_called,
        "the overlay must be applied on the caller's task, before xTaskCreate() returns to it");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_GREATER_THAN_MESSAGE(mock_port_manager_apply_cache_settings_call_seq,
        mock_port_manager_release_call_seq[0],
        "the cache overlay must be reconciled before the port is torn down, not after");
}

// Unlike release()/apply_settings(), the overlay apply is NOT skipped while the factory
// clock_out test owns the ports: it touches no TX/DE pin. Its only live action is
// sniffer_enable/disable on a port whose serial is open, and a frozen port has none — so it
// records the intent, and wb_test's exit path arms the sniffer from it.
void test_settings_update_applies_the_cache_overlay_while_frozen(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - cache overlay applied while frozen");
    LOG_MESSAGE();

    mock_port_manager_ports_frozen_return_value = true;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_port_manager_apply_cache_settings_called,
        "the cache overlay apply must not be gated on the factory-test freeze — it drives no "
        "pin the test owns, and skipping it would lose the setting the user just saved");
}

// A failed apply must reach the caller that can report it. POST /settings turns this into a
// "warnings" entry: the settings ARE saved, but the cache is not where the user just put it,
// and nothing retries it before the next settings write or a reboot.
void test_settings_update_reports_a_failed_cache_apply(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - failed cache apply surfaced");
    LOG_MESSAGE();

    mock_port_manager_apply_cache_settings_return_value = ESP_ERR_NO_MEM;

    esp_err_t cache_err = ESP_OK;
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update_with_status(&cache_err),
        "a cache overlay that would not move is not a reason to report the whole update failed "
        "— the return value means 'the update task could not be created'");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_NO_MEM, cache_err,
        "the failure must be handed to the caller through the out-parameter, not swallowed");
}

// The out-parameter is cleared up front, so a caller that reuses the variable cannot read a
// stale error as this request's result.
void test_settings_update_reports_ok_when_the_cache_apply_succeeds(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - successful cache apply reported as OK");
    LOG_MESSAGE();

    esp_err_t cache_err = ESP_FAIL;   // whatever the caller's variable happened to hold
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update_with_status(&cache_err), "Settings update should succeed");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, cache_err,
        "a successful apply must overwrite the caller's variable, or every settings write after "
        "a failed one would keep reporting the old failure");
}

// ===================================================================
// Cache Modbus TCP server lifecycle (check/release/acquire driven by settings_update)
// ===================================================================

// NVS says the server must run and it is stopped -> init once with the configured port.
void test_settings_update_cache_server_started_when_enabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - cache server enabled, not running -> init");
    LOG_MESSAGE();

    mock_setting_items_cache_server_enabled = true;
    mock_setting_items_cache_port = 504;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_init_call_count,
        "cache_modbus_server_init must be called exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(504, mock_cache_modbus_server_init_last_port,
        "cache_modbus_server_init must be called with the configured port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_cache_modbus_server_deinit_call_count,
        "a stopped server has no socket to release");

    verify_task_deleted();
}

// A configured port of 0 falls back to the default CACHE_MODBUS_SERVER_PORT.
void test_settings_update_cache_server_uses_default_port_when_configured_port_zero(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - cache port 0 -> default port");
    LOG_MESSAGE();

    mock_setting_items_cache_server_enabled = true;
    mock_setting_items_cache_port = 0;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_init_call_count,
        "cache_modbus_server_init must be called exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_MODBUS_SERVER_PORT, mock_cache_modbus_server_init_last_port,
        "configured port 0 must fall back to CACHE_MODBUS_SERVER_PORT");
}

// NVS says the server must be off and it is running -> deinit once, no init.
void test_settings_update_cache_server_stopped_when_disabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - cache server disabled, running -> deinit");
    LOG_MESSAGE();

    mock_cache_modbus_server_set_running_port(504);
    mock_setting_items_cache_server_enabled = false;
    mock_setting_items_cache_port = 504;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_deinit_call_count,
        "cache_modbus_server_deinit must be called exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_cache_modbus_server_init_call_count,
        "cache_modbus_server_init must not be called when stopping the server");
}

// Server already running on the configured port -> nothing to do (no task, no calls).
void test_settings_update_cache_server_no_op_when_already_running(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - cache server already running -> no-op");
    LOG_MESSAGE();

    mock_cache_modbus_server_set_running_port(504);
    mock_setting_items_cache_server_enabled = true;
    mock_setting_items_cache_port = 504;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTaskCreate_data.called,
        "xTaskCreate should not be called when the cache server already matches NVS");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_cache_modbus_server_init_call_count,
        "init must not be called when the server is already running on the configured port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_cache_modbus_server_deinit_call_count,
        "deinit must not be called when the server should keep running");
}

// A port change restarts the server exactly once: deinit + init on the NEW port.
void test_settings_update_cache_server_restarted_on_port_change(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - cache port changed -> deinit+init");
    LOG_MESSAGE();

    mock_cache_modbus_server_set_running_port(1502);
    mock_setting_items_cache_server_enabled = true;
    mock_setting_items_cache_port = 1503;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_deinit_call_count,
        "cache_modbus_server_deinit must be called exactly once when the port changes");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_init_call_count,
        "cache_modbus_server_init must be called exactly once when the port changes");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1503, mock_cache_modbus_server_init_last_port,
        "cache_modbus_server_init must be called with the NEW port");
}

// init(new_port) fails -> roll back by re-initialising the previously running port.
void test_settings_update_cache_server_port_change_init_failure_rolls_back(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - cache init(new) fails -> rollback init(old)");
    LOG_MESSAGE();

    mock_cache_modbus_server_set_running_port(1502);
    mock_setting_items_cache_server_enabled = true;
    mock_setting_items_cache_port = 1503;

    // Arm per-port failure: init(1503) fails, init(1502) (the rollback) succeeds.
    mock_cache_modbus_server_init_fail_port  = 1503;
    mock_cache_modbus_server_init_fail_error = ESP_FAIL;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_deinit_call_count,
        "cache_modbus_server_deinit must be called exactly once for the restart");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_cache_modbus_server_init_call_count,
        "init must be called twice: failed new-port attempt + rollback to the old port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1503, mock_cache_modbus_server_init_ports[0],
        "first init must target the NEW port (1503)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1502, mock_cache_modbus_server_init_ports[1],
        "second init must roll back to the OLD port (1502)");
}

// A deinit that reports a failure leaves the OLD listener up. Starting a second one would orphan it
// (deinit only frees the latest descriptor), so the server must be left exactly as it is.
void test_settings_update_cache_server_failed_release_blocks_acquire(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - failed cache deinit -> no second listener");
    LOG_MESSAGE();

    mock_cache_modbus_server_set_running_port(1502);
    mock_setting_items_cache_server_enabled = true;
    mock_setting_items_cache_port = 1503;
    mock_cache_modbus_server_deinit_error = ESP_FAIL;   // the old listener stays up

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_deinit_call_count,
        "the release phase must have tried to stop the server exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_cache_modbus_server_init_call_count,
        "no second listener may be started while the old one is still up");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1502, cache_modbus_server_get_port(),
        "the server keeps serving the port it could not give up");
}

// Enabling the server and changing its port in one settings write must bring it up ONCE, not start
// it and then restart it — the two concerns used to be applied in separate blocks.
void test_settings_update_cache_server_enable_and_port_change_applied_once(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - enable + port change -> single init");
    LOG_MESSAGE();

    // Server stopped; the request enables it on a new port.
    mock_setting_items_cache_server_enabled = true;
    mock_setting_items_cache_port = 1503;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_init_call_count,
        "the server must be brought up exactly once, not started and then restarted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1503, mock_cache_modbus_server_init_last_port,
        "the server must be brought up on the new port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_cache_modbus_server_deinit_call_count,
        "a stopped server must not be deinitialised before starting it");
}

// The cache server gives its socket up BEFORE the RS-485 ports are re-initialized, so a bridge
// gateway can be moved onto the port the server is vacating without its bind() hitting EADDRINUSE
// (port_manager_apply_settings() has no rollback — the port would just stay dead).
void test_settings_update_cache_server_released_before_ports(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - cache releases its port before the bridges");
    LOG_MESSAGE();

    mock_cache_modbus_server_set_running_port(504);
    mock_setting_items_cache_server_enabled = false;                    // the server must free 504
    mock_port_manager_check_settings_changed_return_value[0] = true;    // RS-485-1 wants 504 now

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_deinit_call_count,
        "the cache server must be stopped exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_cache_modbus_server_init_call_count,
        "a disabled cache server must not be restarted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_port_manager_apply_settings_called[0],
        "the changed port must be re-applied");

    TEST_ASSERT_TRUE_MESSAGE(
        mock_cache_modbus_server_deinit_call_seq < mock_port_manager_apply_settings_call_seq[0],
        "the cache server must give its socket up before a bridge binds that port");
}

// The reverse hand-over: the cache server moves onto a port an RS-485 gateway is vacating, so it
// may only take the socket AFTER the ports have been re-applied.
void test_settings_update_cache_server_acquires_after_ports(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - cache takes its port after the bridges");
    LOG_MESSAGE();

    mock_cache_modbus_server_set_running_port(0);           // server stopped
    mock_setting_items_cache_server_enabled = true;
    mock_setting_items_cache_port = 502;                    // the port RS-485-1 is giving up
    mock_port_manager_check_settings_changed_return_value[0] = true;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cache_modbus_server_init_call_count,
        "the cache server must be started exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(502, mock_cache_modbus_server_init_last_port,
        "the cache server must be started on the port taken over from the bridge");

    TEST_ASSERT_TRUE_MESSAGE(
        mock_port_manager_apply_settings_call_seq[0] < mock_cache_modbus_server_init_call_seq,
        "the cache server must bind its socket only after the ports have been re-applied");
}

// ===================================================================
// Two-phase apply: release every TCP listening socket that must change BEFORE any subsystem
// binds a new one, so a port can be handed over between them.
// ===================================================================

// Both ports change: every port must be released before ANY of them is re-initialized, so the two
// RS-485 gateways can swap their TCP ports without colliding on bind().
void test_settings_update_all_ports_released_before_any_reinit(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - all ports released before any re-init");
    LOG_MESSAGE();

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        mock_port_manager_check_settings_changed_return_value[i] = true;
    }

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_port_manager_release_called[i],
            "every changed port must be released exactly once");
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_port_manager_apply_settings_called[i],
            "every changed port must be re-applied exactly once");
    }

    for (unsigned released = 0; released < BRIDGES_COUNT; released++) {
        for (unsigned applied = 0; applied < BRIDGES_COUNT; applied++) {
            TEST_ASSERT_TRUE_MESSAGE(
                mock_port_manager_release_call_seq[released] <
                    mock_port_manager_apply_settings_call_seq[applied],
                "every port must be released before any port is re-initialized");
        }
    }
}

// The web server owns a TCP listening socket too, so it takes part in the same two-phase apply.
// web_port 80 -> 8080 while RS-485-1 moves its gateway onto 80: the web server must give 80 up
// BEFORE port_manager binds it, otherwise the gateway's bind() hits EADDRINUSE and the RS-485 port
// stays dead (port_manager_apply_settings has no rollback).
void test_settings_update_http_released_before_ports(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - HTTP releases its port before the bridges");
    LOG_MESSAGE();

    mock_http_server_set_running_port(80);
    mock_http_server_configured_port = 8080;                            // web UI moves to 8080
    mock_http_server_check_settings_changed_return_value = true;
    mock_port_manager_check_settings_changed_return_value[0] = true;    // RS-485-1 wants 80 now

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_http_server_deinit_called,
        "the web server must be stopped exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_http_server_init_called,
        "the web server must be restarted exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(8080, mock_http_server_init_last_port,
        "the web server must come back on the newly configured port");

    TEST_ASSERT_TRUE_MESSAGE(
        mock_http_server_deinit_call_seq < mock_port_manager_release_call_seq[0],
        "the web server must give its socket up before the ports are released");
    TEST_ASSERT_TRUE_MESSAGE(
        mock_http_server_deinit_call_seq < mock_port_manager_apply_settings_call_seq[0],
        "the web server must give its socket up before a bridge binds that port");
}

// The reverse hand-over: the web server moves onto the port an RS-485 gateway is vacating, so it
// may only bind its new socket AFTER the ports have been re-applied.
void test_settings_update_http_acquires_after_ports(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - HTTP takes its port after the bridges");
    LOG_MESSAGE();

    mock_http_server_set_running_port(80);
    mock_http_server_configured_port = 8080;                            // the port RS-485-1 gives up
    mock_http_server_check_settings_changed_return_value = true;
    mock_port_manager_check_settings_changed_return_value[0] = true;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(8080, mock_http_server_init_last_port,
        "the web server must be started on the port taken over from the bridge");
    TEST_ASSERT_TRUE_MESSAGE(
        mock_port_manager_release_call_seq[0] < mock_http_server_init_call_seq,
        "the bridge must release its socket before the web server binds it");
    TEST_ASSERT_TRUE_MESSAGE(
        mock_port_manager_apply_settings_call_seq[0] < mock_http_server_init_call_seq,
        "the web server must bind its socket only after the ports have been re-applied");
}

// The client's POST /settings is answered over the very socket the web server is about to give up,
// so the delay that lets the response go out must happen BEFORE the deinit — not, as it used to,
// between the port re-init and the deinit.
void test_settings_update_http_response_sent_before_deinit(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - the response is sent before the socket is dropped");
    LOG_MESSAGE();

    mock_http_server_set_running_port(80);
    mock_http_server_configured_port = 8080;
    mock_http_server_check_settings_changed_return_value = true;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    verify_delay_before_network_updates();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_http_server_delays_before_deinit,
        "the delay that lets the POST /settings response reach the client must precede the deinit");
}

// The factory clock-out test owns the ports' TX/DE pins. The "settings changed" flags are computed
// synchronously in the HTTP task but applied later, in this async task, so a POST /wb_test landing
// in between makes them stale. port_manager has the last word — under the port lock — and must
// neither tear the port down nor bring it back up; the mock enforces that check where the real one
// lives, so the whole two-phase apply has to come out as a no-op for the port.
void test_settings_update_frozen_ports_are_neither_released_nor_reapplied(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - frozen ports survive the two-phase apply");
    LOG_MESSAGE();

    mock_port_manager_check_settings_changed_return_value[0] = true;
    mock_network_check_mdns_settings_changed_return_value = true;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();

    // The factory test starts after the flags were latched but before the task runs.
    mock_port_manager_ports_frozen_return_value = true;
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_port_manager_release_called[0],
        "a port held by the factory clock-out test must not be torn down");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_port_manager_apply_settings_called[0],
        "a port held by the factory clock-out test must not be re-initialized");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_port_manager_release_skipped[0],
        "the release must have been offered to port_manager and refused under the port lock");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_port_manager_apply_settings_skipped[0],
        "the apply must have been offered to port_manager and refused under the port lock");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_network_update_mdns_settings_called,
        "subsystems unrelated to the frozen ports must still be updated");
}

// ===================================================================
// The web server's fallback ladder: configured port -> released port -> default port.
// http_server_check_settings_changed() reports "no change" while the server is stopped, so
// HTTP_SERVER_FLAG is never raised again and a web UI left down here stays down until the device is
// power-cycled — with no way to fix the setting, because the API IS the web server.
// ===================================================================

// Starting the web server on the new port fails (e.g. the port is taken): it must be rolled back
// onto the port it was serving. A web UI down on both ports leaves the user with no way to undo the
// setting that broke it.
void test_settings_update_http_init_failure_rolls_back(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - HTTP init(new) fails -> rollback init(old)");
    LOG_MESSAGE();

    mock_http_server_set_running_port(80);
    mock_http_server_configured_port = 8080;
    mock_http_server_check_settings_changed_return_value = true;

    // Arm per-port failure: init(8080) fails, init(80) (the rollback) succeeds.
    mock_http_server_init_fail_port  = 8080;
    mock_http_server_init_fail_error = ESP_FAIL;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_http_server_init_called,
        "init must be called twice: failed new-port attempt + rollback to the old port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(8080, mock_http_server_init_ports[0],
        "first init must target the NEW port (8080)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(80, mock_http_server_init_ports[1],
        "second init must roll the web UI back to the OLD port (80)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(80, http_server_get_port(),
        "the web UI must end up listening again on the port it was serving");
}

// A web server that was not running has no socket to release, so there is no OLD port to roll back
// to. The default port must still be tried.
void test_settings_update_http_init_failure_without_release_skips_rollback(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - no rollback when nothing was released");
    LOG_MESSAGE();

    mock_http_server_set_running_port(0);                       // server stopped
    mock_http_server_configured_port = 8080;
    mock_http_server_check_settings_changed_return_value = true;
    mock_http_server_init_return_value = ESP_FAIL;              // the start fails
    mock_http_server_init_ok_port = HTTP_SERVER_DEFAULT_PORT;   // ... but the default port binds

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_http_server_init_called,
        "the failed start must be followed by the default-port fallback and nothing else");
    TEST_ASSERT_EQUAL_INT_MESSAGE(8080, mock_http_server_init_ports[0],
        "first init must target the configured port (8080)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(HTTP_SERVER_DEFAULT_PORT, mock_http_server_init_ports[1],
        "the second attempt must be the default port, not a port the server never had");
    TEST_ASSERT_EQUAL_INT_MESSAGE(HTTP_SERVER_DEFAULT_PORT, http_server_get_port(),
        "the web UI must end up on the default port");
}

// Both the new and the old port are unbindable. The default port must still be tried before giving
// up: it is the one port that is not derived from the settings that just broke the server.
void test_settings_update_http_rollback_failure_falls_back_to_default_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - new + old port dead -> default port");
    LOG_MESSAGE();

    mock_http_server_set_running_port(8080);        // web UI was moved off the default port earlier
    mock_http_server_configured_port = 9090;        // and is now asked to move to 9090
    mock_http_server_check_settings_changed_return_value = true;

    // Everything fails except the default port.
    mock_http_server_init_return_value = ESP_FAIL;
    mock_http_server_init_ok_port = HTTP_SERVER_DEFAULT_PORT;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(3, mock_http_server_init_called,
        "init must be tried three times: new port, rollback to the old one, default port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(9090, mock_http_server_init_ports[0], "first init must target the NEW port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(8080, mock_http_server_init_ports[1], "second init must roll back to the OLD port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(HTTP_SERVER_DEFAULT_PORT, mock_http_server_init_ports[2],
        "third init must be the last-resort default port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(HTTP_SERVER_DEFAULT_PORT, http_server_get_port(),
        "the web UI must end up listening on the default port");
}

// Nothing binds — not the new port, not the old one, not the default one. The task must stop there
// and let the gateway run on with a dead web UI. It must NOT reboot: all three attempts fail for the
// same reason — http_server_init_port() reports out-of-memory, LWIP socket exhaustion and a refused
// wifi_scan_init()/auth_init() alike as ESP_FAIL — so a busy gateway would reboot itself
// mid-Modbus-traffic on a plain settings write, and the boot would meet the same shortage anyway.
void test_settings_update_http_all_ports_dead_leaves_web_ui_down(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - no port binds -> web UI down, no reboot");
    LOG_MESSAGE();

    mock_http_server_set_running_port(8080);
    mock_http_server_configured_port = 9090;
    mock_http_server_check_settings_changed_return_value = true;
    mock_http_server_init_return_value = ESP_FAIL;      // every port fails, the default included

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(3, mock_http_server_init_called,
        "all three ports must be tried: the configured one, the released one, the default one");
    TEST_ASSERT_EQUAL_INT_MESSAGE(9090, mock_http_server_init_ports[0], "configured port first");
    TEST_ASSERT_EQUAL_INT_MESSAGE(8080, mock_http_server_init_ports[1], "then the released port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(HTTP_SERVER_DEFAULT_PORT, mock_http_server_init_ports[2],
        "then the default port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, http_server_get_port(),
        "the web server stays down; the rest of the gateway keeps running");

    // The task must run to completion rather than restart the device.
    verify_task_deleted();
}

// The web UI was already serving the default port and cannot be brought back up anywhere: the
// rollback IS the default-port attempt, so it must not be repeated.
void test_settings_update_http_default_port_not_retried_after_failed_rollback(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - the default port is not tried twice");
    LOG_MESSAGE();

    mock_http_server_set_running_port(HTTP_SERVER_DEFAULT_PORT);
    mock_http_server_configured_port = 9090;
    mock_http_server_check_settings_changed_return_value = true;
    mock_http_server_init_return_value = ESP_FAIL;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_http_server_init_called,
        "the default port must not be tried twice: the rollback already targeted it");
    TEST_ASSERT_EQUAL_INT_MESSAGE(9090, mock_http_server_init_ports[0], "new port first");
    TEST_ASSERT_EQUAL_INT_MESSAGE(HTTP_SERVER_DEFAULT_PORT, mock_http_server_init_ports[1],
        "then the rollback, which is the default port here");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, http_server_get_port(),
        "the web server stays down once its last reachable port has failed");
}

// http_server_deinit() drops its handle whatever httpd_stop() answers, so a "failed" deinit still
// leaves the web server stopped and its port unknown. The release phase must therefore NOT offer the
// old port as a rollback target — the acquire phase falls back to the default port instead.
void test_settings_update_http_failed_deinit_offers_no_rollback_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - a failed deinit leaves no rollback target");
    LOG_MESSAGE();

    mock_http_server_set_running_port(8080);
    mock_http_server_configured_port = 9090;
    mock_http_server_check_settings_changed_return_value = true;

    mock_http_server_deinit_return_value = ESP_FAIL;    // httpd_stop() complained...
    mock_http_server_init_fail_port = 9090;             // ... and the new port will not bind

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_http_server_init_called,
        "the failed start must be followed by the default-port fallback only");
    TEST_ASSERT_EQUAL_INT_MESSAGE(9090, mock_http_server_init_ports[0],
        "the configured port is tried first");
    TEST_ASSERT_EQUAL_INT_MESSAGE(HTTP_SERVER_DEFAULT_PORT, mock_http_server_init_ports[1],
        "8080 must NOT be retried: after a failed deinit the server no longer holds it");
    TEST_ASSERT_EQUAL_INT_MESSAGE(HTTP_SERVER_DEFAULT_PORT, http_server_get_port(),
        "the web UI must come back on the default port");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_settings_update_no_changes);
    RUN_TEST(test_settings_update_ports_frozen_skips_rs485_and_tx_disabled);
    RUN_TEST(test_settings_update_bridge_ports_changed);
    RUN_TEST(test_settings_update_mdns_changed);
    RUN_TEST(test_settings_update_http_server_changed);
    RUN_TEST(test_settings_update_ethernet_changed);
    RUN_TEST(test_settings_update_wifi_changed);
    RUN_TEST(test_settings_update_all_changed);
    RUN_TEST(test_settings_update_task_creation_failure);
    RUN_TEST(test_settings_update_task_already_running);

    // Runtime apply of the per-port cache overlay
    RUN_TEST(test_settings_update_applies_the_cache_overlay);
    RUN_TEST(test_settings_update_applies_the_cache_overlay_before_the_ports);
    RUN_TEST(test_settings_update_applies_the_cache_overlay_while_frozen);
    RUN_TEST(test_settings_update_reports_a_failed_cache_apply);
    RUN_TEST(test_settings_update_reports_ok_when_the_cache_apply_succeeds);

    // Cache Modbus TCP server lifecycle
    RUN_TEST(test_settings_update_cache_server_started_when_enabled);
    RUN_TEST(test_settings_update_cache_server_uses_default_port_when_configured_port_zero);
    RUN_TEST(test_settings_update_cache_server_stopped_when_disabled);
    RUN_TEST(test_settings_update_cache_server_no_op_when_already_running);
    RUN_TEST(test_settings_update_cache_server_restarted_on_port_change);
    RUN_TEST(test_settings_update_cache_server_port_change_init_failure_rolls_back);
    RUN_TEST(test_settings_update_cache_server_failed_release_blocks_acquire);
    RUN_TEST(test_settings_update_cache_server_enable_and_port_change_applied_once);
    RUN_TEST(test_settings_update_cache_server_released_before_ports);
    RUN_TEST(test_settings_update_cache_server_acquires_after_ports);

    // Two-phase apply (release everything, then acquire everything)
    RUN_TEST(test_settings_update_all_ports_released_before_any_reinit);
    RUN_TEST(test_settings_update_http_released_before_ports);
    RUN_TEST(test_settings_update_http_acquires_after_ports);
    RUN_TEST(test_settings_update_http_response_sent_before_deinit);
    RUN_TEST(test_settings_update_frozen_ports_are_neither_released_nor_reapplied);

    // The web server's fallback ladder
    RUN_TEST(test_settings_update_http_init_failure_rolls_back);
    RUN_TEST(test_settings_update_http_init_failure_without_release_skips_rollback);
    RUN_TEST(test_settings_update_http_rollback_failure_falls_back_to_default_port);
    RUN_TEST(test_settings_update_http_all_ports_dead_leaves_web_ui_down);
    RUN_TEST(test_settings_update_http_default_port_not_retried_after_failed_rollback);
    RUN_TEST(test_settings_update_http_failed_deinit_offers_no_rollback_port);

    return UNITY_END();
}
