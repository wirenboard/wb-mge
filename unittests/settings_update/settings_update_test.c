#include "unity.h"
#include "console_log.h"

#include "settings_update.h"
#include "network.h"
#include "http_server.h"
#include "update_rs485_mio_gpio_states.h"
#include "mb_slave.h"
#include "wb_test.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define SETTINGS_UPDATE_TASK_STACK_SIZE         6144
#define SETTINGS_UPDATE_TASK_PRIORITY           5

#define HTTP_NETWORK_UPDATE_DELAY_MS            1000

void settings_update_reset(void);

void setUp(void)
{
    mock_network_reset();
    mock_http_server_reset();
    mock_update_rs485_mio_gpio_states_reset();
    mock_mb_slave_reset();
    mock_wb_test_reset();
    mock_freertos_task_reset();
    mock_freertos_semaphore_reset();
    settings_update_reset();
    // app_main creates the spawn mutex before any task that calls settings_update() exists;
    // this is that call. It is idempotent, so every test after the first is a no-op — which
    // is also the property being relied on here, since a re-create would strand a holder.
    settings_update_init();
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
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_update_serial_tx_disabled_called,
        "update_serial_tx_disabled should be called once, unconditionally: it is NOT behind "
        "the clock_out guard, because mb_slave_set_tx_disabled() refuses on its own while "
        "the serial half is down");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_update_io_bus_control_called, "update_io_bus_control should be called once");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_network_check_mdns_settings_changed_called, "mDNS check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_http_server_check_settings_changed_called, "HTTP server check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_network_check_eth_settings_changed_called, "Ethernet check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_network_check_wifi_settings_changed_called, "WiFi check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_slave_check_settings_changed_called,
        "Modbus slave check should be called once");
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
    bool expect_mdns,
    bool expect_http,
    bool expect_eth,
    bool expect_wifi
)
{
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
    verify_updates(false, false, false, false);
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
    verify_updates(true, false, false, false);
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
    verify_updates(false, true, false, false);
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
    verify_updates(false, false, true, false);
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
    verify_updates(false, false, false, true);
    verify_task_deleted();
}

// Test update of the Modbus slave settings.
// The slave is stopped and started rather than reconfigured, because the serial
// parameters, the unit id and the TCP port are create-time options of the esp-modbus
// instances — and it happens on the update task, not on the caller's, so the POST
// /settings response is already on its way out when the RS-485 ports go down.
void test_settings_update_modbus_slave_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - Modbus slave changed");
    LOG_MESSAGE();

    mock_mb_slave_check_settings_changed_result = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_mb_slave_stop_called,
        "nothing may be torn down before the update task actually runs");

    execute_task_function();

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelay_data.called,
        "a Modbus-only change releases no socket the POST response travels over");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_slave_stop_called, "mb_slave_stop should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_slave_start_called, "mb_slave_start should be called once");
    verify_updates(false, false, false, false);
    verify_task_deleted();
}

// Test update of all settings simultaneously
void test_settings_update_all_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - all settings changed");
    LOG_MESSAGE();

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
    verify_updates(true, true, true, true);
    verify_task_deleted();
}

// Test task creation with failure
void test_settings_update_task_creation_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - task creation failure");
    LOG_MESSAGE();

    mock_network_check_mdns_settings_changed_return_value = true;
    mock_xTaskCreate_data.should_fail = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "Settings update should fail");

    verify_settings_update_checks();
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xTaskCreate_data.called, "xTaskCreate should be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelay_data.called, "vTaskDelay should not be called");
    verify_updates(false, false, false, false);
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

// The client's POST /settings is answered over the very socket the web server is about to give up,
// so the delay that lets the response go out must happen BEFORE the deinit.
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

// ===================================================================
// The two locks.
//
// settings_update() is reached from three tasks, not one: the POST /settings and
// POST /command handlers on the httpd task, and the factory reset in main.c on
// config_button_task. So neither "only the httpd task spawns an apply" nor "only the httpd
// task stops the Modbus slave" is true, and both have to be enforced rather than assumed.
// ===================================================================

// The Modbus restart is not merely bracketed by the ownership lock — the clock_out guard
// that decides whether to restart at all is read INSIDE it. Splitting the two is the race
// the lock exists for: the factory test raises that guard and stops the slave under the
// same lock, so a check made outside it could be true when read and false when acted on,
// leaving two tasks tearing the same esp-modbus instances down at once.
void test_settings_update_modbus_restart_runs_under_the_lock(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - the Modbus restart runs under the ownership lock");
    LOG_MESSAGE();

    mock_mb_slave_check_settings_changed_result = true;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_slave_lock_take_called,
        "the update task must take the Modbus ownership lock exactly once");
    TEST_ASSERT_EQUAL_MESSAGE(MB_SLAVE_LOCK_WAIT_FOREVER, mock_mb_slave_lock_take_timeout_ms,
        "a background apply waits for the lock rather than skipping the restart");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_slave_stop_called, "mb_slave_stop should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_slave_start_called, "mb_slave_start should be called once");

    TEST_ASSERT_TRUE_MESSAGE(
        mock_mb_slave_lock_take_call_seq < mock_wb_test_clock_out_active_call_seq,
        "the clock_out guard must be read INSIDE the lock, not before it");
    TEST_ASSERT_TRUE_MESSAGE(
        mock_wb_test_clock_out_active_call_seq < mock_mb_slave_stop_call_seq,
        "the guard must be read before the slave is stopped");
    TEST_ASSERT_TRUE_MESSAGE(
        mock_mb_slave_start_call_seq < mock_mb_slave_lock_give_call_seq,
        "the lock must be held until the slave has been brought back");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_mb_slave_lock_held_count,
        "the update task must not leave the ownership lock held");
}

// The clock_out test owns the RS-485 hardware: the restart is deferred, as before — and the
// lock is handed back on that path too, or the next transition would block on it.
void test_settings_update_clock_out_deferral_still_releases_the_lock(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - deferred Modbus restart releases the lock");
    LOG_MESSAGE();

    mock_mb_slave_check_settings_changed_result = true;
    mock_wb_test_clock_out_active_value = true;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    execute_task_function();

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_slave_lock_take_called, "the lock is taken to read the guard");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_slave_lock_give_called, "and given back on the deferral path too");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_mb_slave_lock_held_count, "no holder is left behind");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_mb_slave_stop_called,
        "the slave must not be stopped while the clock_out test owns the pins");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_mb_slave_start_called,
        "nor started: the test's own exit path does that from NVS");
}

// A lock that cannot be taken means the restart does not happen — it must never mean the
// restart happens anyway. Nothing is lost: the running ports still differ from NVS, so
// mb_slave_check_settings_changed() reports the same change on the next settings write.
void test_settings_update_modbus_restart_skipped_when_lock_unavailable(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - no ownership lock, no Modbus restart");
    LOG_MESSAGE();

    mock_mb_slave_check_settings_changed_result = true;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();

    // settings_update() reads the guard itself (for the V-out deferral) before the task ever
    // runs, so the task's own read is the only one that may be missing here.
    int guard_reads_before_task = mock_wb_test_clock_out_active_called;
    mock_mb_slave_lock_take_result = false;
    execute_task_function();

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_slave_lock_take_called, "the lock is attempted once");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_mb_slave_lock_give_called,
        "a lock that was never taken must not be given back");
    TEST_ASSERT_EQUAL_MESSAGE(guard_reads_before_task, mock_wb_test_clock_out_active_called,
        "without the lock the guard is not even read: the decision belongs inside it");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_mb_slave_stop_called, "the slave must be left alone");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_mb_slave_start_called, "the slave must be left alone");
}

// What the hook sees at the moment the spawn lock is being taken. -1 = the hook never ran.
static int spawn_lock_tasks_created_at_take = -1;
static int spawn_lock_delays_at_take = -1;

static void spawn_lock_take_hook(void)
{
    spawn_lock_tasks_created_at_take = mock_xTaskCreate_data.called;
    spawn_lock_delays_at_take = mock_vTaskDelay_data.called;
}

// The xTaskCreate() end of the critical section: no task may be created before the mutex is
// taken, and the mutex must come back afterwards. The drain that precedes the create is
// covered by the next test.
void test_settings_update_spawn_runs_under_the_spawn_mutex(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - the spawn decision runs under a mutex");
    LOG_MESSAGE();

    mock_network_check_mdns_settings_changed_return_value = true;
    spawn_lock_tasks_created_at_take = -1;
    mock_xSemaphoreTake_hook = spawn_lock_take_hook;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xSemaphoreTake_called, "the spawn mutex must be taken once");
    TEST_ASSERT_EQUAL_MESSAGE(portMAX_DELAY, mock_xSemaphoreTake_xTicksToWait,
        "the spawn decision waits for the mutex rather than giving up on it");
    TEST_ASSERT_EQUAL_MESSAGE(0, spawn_lock_tasks_created_at_take,
        "no task may be created before the mutex is taken");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xTaskCreate_data.called, "the task is created inside the mutex");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xSemaphoreGive_called, "and the mutex is given back");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphore_held_count, "settings_update must not return holding it");
}

// The other end: the "is an apply already in flight?" wait is inside the mutex too, so the
// two halves of the decision cannot be split. Two callers on two tasks that both passed the
// wait would both create a task: the second handle overwrites the first, the first clears it
// on the way out, and settings_update_in_progress() then answers "idle" with an apply still
// live — the very answer the factory clock_out test uses to decide it may take the RS-485
// hardware.
//
// Single-threaded, so the property is asserted by ORDER: at the instant the mutex was taken
// the drain had not run a single poll, and the polls happened all the same. A wait placed
// before the take would show delays already counted at hook time.
void test_settings_update_drain_waits_inside_the_spawn_mutex(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - the in-flight wait is inside the mutex");
    LOG_MESSAGE();

    // First call: spawn an apply and leave it "in flight" (the task function is not run).
    mock_http_server_check_settings_changed_return_value = true;
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    verify_task_created();
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelay_data.called, "nothing has waited yet");

    // Second call: the apply is still in flight, so this one drains before deciding.
    // The mock clears the task handle on the third poll.
    mock_http_server_check_settings_changed_return_value = false;
    mock_vTaskDelay_data.task_handle_reset_on_count = 3;
    spawn_lock_delays_at_take = -1;
    spawn_lock_tasks_created_at_take = -1;
    mock_xSemaphoreTake_hook = spawn_lock_take_hook;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");

    TEST_ASSERT_EQUAL_MESSAGE(2, mock_xSemaphoreTake_called, "each call takes the mutex once");
    TEST_ASSERT_EQUAL_MESSAGE(0, spawn_lock_delays_at_take,
        "the wait for the in-flight apply must start AFTER the mutex is taken, not before");
    TEST_ASSERT_EQUAL_MESSAGE(1, spawn_lock_tasks_created_at_take,
        "the hook ran on the second call, with the first call's task still in flight");
    TEST_ASSERT_EQUAL_MESSAGE(3, mock_vTaskDelay_data.called,
        "and the drain really did poll, inside the mutex");
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_xSemaphoreGive_called, "both calls give the mutex back");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphore_held_count, "no holder is left behind");
}

// Every exit path gives the mutex back, the failed-spawn one included: a leaked spawn mutex
// would wedge every later settings write, from every task.
void test_settings_update_spawn_mutex_released_on_every_path(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - the spawn mutex is never leaked");
    LOG_MESSAGE();

    // Nothing changed: no task to create, and the mutex still comes back.
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTaskCreate_data.called, "no task when nothing changed");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphore_held_count, "the mutex is released on the no-change path");

    // And on the path where xTaskCreate() itself refuses.
    mock_network_check_mdns_settings_changed_return_value = true;
    mock_xTaskCreate_data.should_fail = true;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, settings_update(), "Settings update should fail");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphore_held_count,
        "the mutex is released when the task could not be created");
}

// settings_update_restarts_web_server() is what keeps an HTTP handler from waiting on an
// apply that cannot finish until that handler returns: such an apply calls httpd_stop(),
// which blocks until the httpd thread leaves its handler. It must report the flags of the
// apply that is ACTUALLY in flight, and nothing once it has finished.
void test_settings_update_restarts_web_server_tracks_the_apply_in_flight(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - the web-server restart is visible while in flight");
    LOG_MESSAGE();

    TEST_ASSERT_FALSE_MESSAGE(settings_update_in_progress(), "nothing is in flight to begin with");
    TEST_ASSERT_FALSE_MESSAGE(settings_update_restarts_web_server(),
        "with no apply in flight there is no web server restart to report");

    // A Modbus-only apply: in flight, but it never touches the web server, so an HTTP
    // handler may safely wait for it.
    mock_mb_slave_check_settings_changed_result = true;
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    TEST_ASSERT_TRUE_MESSAGE(settings_update_in_progress(), "the Modbus apply is in flight");
    TEST_ASSERT_FALSE_MESSAGE(settings_update_restarts_web_server(),
        "a Modbus-only apply does not restart the web server");
    execute_task_function();
    TEST_ASSERT_FALSE_MESSAGE(settings_update_in_progress(), "the apply has finished");

    // An apply that carries the web server: an HTTP handler must be refused on the spot.
    setUp();
    mock_http_server_check_settings_changed_return_value = true;
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, settings_update(), "Settings update should succeed");
    TEST_ASSERT_TRUE_MESSAGE(settings_update_restarts_web_server(),
        "an apply holding HTTP_SERVER_FLAG must be reported while it is in flight");

    execute_task_function();
    TEST_ASSERT_FALSE_MESSAGE(settings_update_in_progress(), "the apply has finished");
    TEST_ASSERT_FALSE_MESSAGE(settings_update_restarts_web_server(),
        "a finished apply restarts nothing: the flags are cleared with the handle");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_settings_update_no_changes);
    RUN_TEST(test_settings_update_mdns_changed);
    RUN_TEST(test_settings_update_http_server_changed);
    RUN_TEST(test_settings_update_ethernet_changed);
    RUN_TEST(test_settings_update_wifi_changed);
    RUN_TEST(test_settings_update_modbus_slave_changed);
    RUN_TEST(test_settings_update_all_changed);
    RUN_TEST(test_settings_update_task_creation_failure);
    RUN_TEST(test_settings_update_task_already_running);

    RUN_TEST(test_settings_update_http_response_sent_before_deinit);

    // The web server's fallback ladder
    RUN_TEST(test_settings_update_http_init_failure_rolls_back);
    RUN_TEST(test_settings_update_http_init_failure_without_release_skips_rollback);
    RUN_TEST(test_settings_update_http_rollback_failure_falls_back_to_default_port);
    RUN_TEST(test_settings_update_http_all_ports_dead_leaves_web_ui_down);
    RUN_TEST(test_settings_update_http_default_port_not_retried_after_failed_rollback);
    RUN_TEST(test_settings_update_http_failed_deinit_offers_no_rollback_port);

    // The two locks
    RUN_TEST(test_settings_update_modbus_restart_runs_under_the_lock);
    RUN_TEST(test_settings_update_clock_out_deferral_still_releases_the_lock);
    RUN_TEST(test_settings_update_modbus_restart_skipped_when_lock_unavailable);
    RUN_TEST(test_settings_update_spawn_runs_under_the_spawn_mutex);
    RUN_TEST(test_settings_update_drain_waits_inside_the_spawn_mutex);
    RUN_TEST(test_settings_update_spawn_mutex_released_on_every_path);
    RUN_TEST(test_settings_update_restarts_web_server_tracks_the_apply_in_flight);

    return UNITY_END();
}
