// Unit tests for transparent_tcp.c
// Tests init/deinit behaviour, edge cases, and resource management (no leaks).

#include "unity.h"
#include "console_log.h"

#include "transparent_tcp.h"
#include "bridge.h"
#include "mock_tcp_server.h"
#include "mock_tcp_client.h"
#include "mock_serial.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Call deinit on all ports to wipe any module-level state carried between tests.
// After Bug 1 fix, deinit on an un-initialised port returns ESP_OK gracefully.
static void reset_transparent_tcp_state(void)
{
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        transparent_tcp_deinit_port(i);
    }
}

// Build a minimal valid serial_config_t that transparent_tcp passes through to serial_init.
static serial_config_t make_serial_config(void)
{
    serial_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.port_num = 1;
    cfg.tx_pin   = 10;
    cfg.rx_pin   = 9;
    cfg.dir_pin  = 4;
    cfg.baudrate = 9600;
    return cfg;
}

// ---------------------------------------------------------------------------
// setUp / tearDown
// ---------------------------------------------------------------------------

void setUp(void)
{
    // First flush any module state from the previous test, then reset mocks.
    // Deinit is safe on uninitialised ports after Bug 1 fix.
    reset_transparent_tcp_state();

    mock_tcp_server_reset();
    mock_tcp_client_reset();
    mock_serial_reset();
}

void tearDown(void)
{
}

// ---------------------------------------------------------------------------
// Test: deinit on never-initialised context → ESP_OK, no crash, no calls
// ---------------------------------------------------------------------------
void test_deinit_uninitialized_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: deinit on never-initialized port");
    LOG_MESSAGE();

    esp_err_t result = transparent_tcp_deinit_port(0);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "deinit on uninitialized port should return ESP_OK");

    // No TCP or serial teardown should have occurred
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_server_calls.deinit_called, "tcp_server_deinit must not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_client_calls.deinit_called, "tcp_client_deinit must not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_calls.deinit_called, "serial_deinit must not be called");
}

// ---------------------------------------------------------------------------
// Test: deinit with out-of-range index → ESP_ERR_INVALID_ARG
// ---------------------------------------------------------------------------
void test_deinit_invalid_index(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: deinit with invalid index");
    LOG_MESSAGE();

    esp_err_t result = transparent_tcp_deinit_port(BRIDGES_COUNT);
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG, result,
        "deinit with out-of-range index must return ESP_ERR_INVALID_ARG"
    );
}

// ---------------------------------------------------------------------------
// Test: init with out-of-range index → ESP_ERR_INVALID_ARG
// ---------------------------------------------------------------------------
void test_init_invalid_index(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: init with invalid index");
    LOG_MESSAGE();

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    esp_err_t result = transparent_tcp_init_port(
        BRIDGES_COUNT, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG, result,
        "init with out-of-range index must return ESP_ERR_INVALID_ARG"
    );
}

// ---------------------------------------------------------------------------
// Test: successful init in server mode
// ---------------------------------------------------------------------------
void test_init_server_mode_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: init in BRIDGE_MODE_SERVER succeeds");
    LOG_MESSAGE();

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    esp_err_t result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "init in server mode should return ESP_OK");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.init_called, "tcp_server_init must be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.init_called, "serial_init must be called once");

    // No teardown should have happened during a successful init
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_server_calls.deinit_called, "tcp_server_deinit must not be called on success");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_calls.deinit_called, "serial_deinit must not be called on success");

    TEST_ASSERT_NOT_NULL_MESSAGE(serial_desc, "serial_desc must not be NULL after successful init");
    TEST_ASSERT_NOT_NULL_MESSAGE(tcp_desc, "tcp_desc must not be NULL after successful init");

    // Client-side calls must be zero
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_client_calls.init_called, "tcp_client_init must not be called in server mode");
}

// ---------------------------------------------------------------------------
// Test: successful init in client mode
// ---------------------------------------------------------------------------
void test_init_client_mode_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: init in BRIDGE_MODE_CLIENT succeeds");
    LOG_MESSAGE();

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    esp_err_t result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_CLIENT, 502, 0x01020304, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "init in client mode should return ESP_OK");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_client_calls.init_called, "tcp_client_init must be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.init_called, "serial_init must be called once");

    // No teardown should have happened during a successful init
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_client_calls.deinit_called, "tcp_client_deinit must not be called on success");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_calls.deinit_called, "serial_deinit must not be called on success");

    TEST_ASSERT_NOT_NULL_MESSAGE(serial_desc, "serial_desc must not be NULL after successful init");
    TEST_ASSERT_NOT_NULL_MESSAGE(tcp_desc, "tcp_desc must not be NULL after successful init");

    // Server-side calls must be zero
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_server_calls.init_called, "tcp_server_init must not be called in client mode");
}

// ---------------------------------------------------------------------------
// Test: tcp_server_init fails → serial_init is not called
// ---------------------------------------------------------------------------
void test_init_server_tcp_init_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: tcp_server_init failure prevents serial_init");
    LOG_MESSAGE();

    mock_tcp_server_calls.init_should_fail = true;

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    esp_err_t result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "init should return ESP_FAIL when tcp_server_init fails");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.init_called, "tcp_server_init must be attempted once");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_calls.init_called, "serial_init must not be called when TCP init fails");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_server_calls.deinit_called, "tcp_server_deinit must not be called (nothing to free)");
}

// ---------------------------------------------------------------------------
// Test: tcp_client_init fails → serial_init is not called
// ---------------------------------------------------------------------------
void test_init_client_tcp_init_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: tcp_client_init failure prevents serial_init");
    LOG_MESSAGE();

    mock_tcp_client_calls.init_should_fail = true;

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    esp_err_t result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_CLIENT, 502, 0x01020304, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "init should return ESP_FAIL when tcp_client_init fails");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_client_calls.init_called, "tcp_client_init must be attempted once");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_calls.init_called, "serial_init must not be called when TCP init fails");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_client_calls.deinit_called, "tcp_client_deinit must not be called (nothing to free)");
}

// ---------------------------------------------------------------------------
// Test: serial_init fails after tcp_server_init → tcp_server_deinit must be called (no leak)
// ---------------------------------------------------------------------------
void test_init_serial_fail_after_server_tcp(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: serial_init failure after tcp_server_init triggers TCP cleanup");
    LOG_MESSAGE();

    mock_serial_calls.init_should_fail = true;

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    esp_err_t result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "init should return ESP_FAIL when serial_init fails");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.init_called, "tcp_server_init must have been called");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.init_called, "serial_init must have been attempted");

    // Bug 2 fix: tcp_server_deinit must be called to free the TCP resource
    TEST_ASSERT_EQUAL_MESSAGE(
        1, mock_tcp_server_calls.deinit_called,
        "tcp_server_deinit must be called to free TCP resource after serial_init fails"
    );
    // Dangling pointer fix: tcp_desc must be NULL so caller cannot use freed memory
    TEST_ASSERT_NULL_MESSAGE(tcp_desc, "tcp_desc must be NULL after serial_init failure");
}

// ---------------------------------------------------------------------------
// Test: serial_init fails after tcp_client_init → tcp_client_deinit must be called (no leak)
// ---------------------------------------------------------------------------
void test_init_serial_fail_after_client_tcp(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: serial_init failure after tcp_client_init triggers TCP cleanup");
    LOG_MESSAGE();

    mock_serial_calls.init_should_fail = true;

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    esp_err_t result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_CLIENT, 502, 0x01020304, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "init should return ESP_FAIL when serial_init fails");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_client_calls.init_called, "tcp_client_init must have been called");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.init_called, "serial_init must have been attempted");

    // Bug 2 fix: tcp_client_deinit must be called to free the TCP resource
    TEST_ASSERT_EQUAL_MESSAGE(
        1, mock_tcp_client_calls.deinit_called,
        "tcp_client_deinit must be called to free TCP resource after serial_init fails"
    );
    // Dangling pointer fix: tcp_desc must be NULL so caller cannot use freed memory
    TEST_ASSERT_NULL_MESSAGE(tcp_desc, "tcp_desc must be NULL after serial_init failure");
}

// ---------------------------------------------------------------------------
// Test: calling init on an already-initialized port returns ESP_OK without re-initialising
// ---------------------------------------------------------------------------
void test_init_already_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: second init call on already-initialized port is a no-op");
    LOG_MESSAGE();

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    // First init
    esp_err_t result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "first init should return ESP_OK");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.init_called, "tcp_server_init called once after first init");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.init_called, "serial_init called once after first init");

    // Reset counters to detect extra calls
    mock_tcp_server_calls.init_called = 0;
    mock_serial_calls.init_called = 0;

    // Second init on the same port
    result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "second init should return ESP_OK");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_server_calls.init_called, "tcp_server_init must NOT be called on second init");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_calls.init_called, "serial_init must NOT be called on second init");
}

// ---------------------------------------------------------------------------
// Test: init then deinit in server mode
// ---------------------------------------------------------------------------
void test_deinit_after_init_server(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: deinit after init in server mode");
    LOG_MESSAGE();

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    esp_err_t result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "init should return ESP_OK");

    result = transparent_tcp_deinit_port(0);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "deinit should return ESP_OK");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.deinit_called, "tcp_server_deinit must be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.deinit_called, "serial_deinit must be called once");

    // Client functions must not have been involved
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_client_calls.deinit_called, "tcp_client_deinit must not be called in server mode");
}

// ---------------------------------------------------------------------------
// Test: init then deinit in client mode
// ---------------------------------------------------------------------------
void test_deinit_after_init_client(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: deinit after init in client mode");
    LOG_MESSAGE();

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    esp_err_t result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_CLIENT, 502, 0x01020304, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "init should return ESP_OK");

    result = transparent_tcp_deinit_port(0);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "deinit should return ESP_OK");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_client_calls.deinit_called, "tcp_client_deinit must be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.deinit_called, "serial_deinit must be called once");

    // Server functions must not have been involved
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_server_calls.deinit_called, "tcp_server_deinit must not be called in client mode");
}

// ---------------------------------------------------------------------------
// Test: init then deinit twice — second deinit must be a no-op (no double-free)
// ---------------------------------------------------------------------------
void test_deinit_twice(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: second deinit after init+deinit is a no-op");
    LOG_MESSAGE();

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    esp_err_t result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "init should return ESP_OK");

    // First deinit
    result = transparent_tcp_deinit_port(0);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "first deinit should return ESP_OK");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.deinit_called, "tcp_server_deinit called once after first deinit");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.deinit_called, "serial_deinit called once after first deinit");

    // Second deinit — must NOT call tcp/serial teardown again
    result = transparent_tcp_deinit_port(0);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "second deinit should return ESP_OK");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.deinit_called, "tcp_server_deinit must NOT be called a second time");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.deinit_called, "serial_deinit must NOT be called a second time");
}

// ---------------------------------------------------------------------------
// Test: init → deinit → init cycle (re-initialization must succeed)
// ---------------------------------------------------------------------------
void test_reinit_after_deinit(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: re-initialization after deinit succeeds");
    LOG_MESSAGE();

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    // First init
    esp_err_t result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "first init should return ESP_OK");

    // Deinit
    result = transparent_tcp_deinit_port(0);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "deinit should return ESP_OK");

    // Reset only the call counters, not the failure flags
    mock_tcp_server_calls.init_called   = 0;
    mock_tcp_server_calls.deinit_called = 0;
    mock_serial_calls.init_called       = 0;
    mock_serial_calls.deinit_called     = 0;

    serial_desc = NULL;
    tcp_desc    = NULL;

    // Second init on same port — must succeed
    result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "re-init after deinit should return ESP_OK");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.init_called, "tcp_server_init must be called on re-init");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.init_called, "serial_init must be called on re-init");
    TEST_ASSERT_NOT_NULL_MESSAGE(serial_desc, "serial_desc must not be NULL after re-init");
    TEST_ASSERT_NOT_NULL_MESSAGE(tcp_desc, "tcp_desc must not be NULL after re-init");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_deinit_uninitialized_port);
    RUN_TEST(test_deinit_invalid_index);
    RUN_TEST(test_init_invalid_index);
    RUN_TEST(test_init_server_mode_success);
    RUN_TEST(test_init_client_mode_success);
    RUN_TEST(test_init_server_tcp_init_fail);
    RUN_TEST(test_init_client_tcp_init_fail);
    RUN_TEST(test_init_serial_fail_after_server_tcp);
    RUN_TEST(test_init_serial_fail_after_client_tcp);
    RUN_TEST(test_init_already_initialized);
    RUN_TEST(test_deinit_after_init_server);
    RUN_TEST(test_deinit_after_init_client);
    RUN_TEST(test_deinit_twice);
    RUN_TEST(test_reinit_after_deinit);

    return UNITY_END();
}
