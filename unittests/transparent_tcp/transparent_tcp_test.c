// Unit tests for transparent_tcp.c
// Tests init/deinit behaviour, edge cases, and resource management (no leaks).

#include "unity.h"
#include "console_log.h"

#include "transparent_tcp.h"
#include "bridge.h"
#include "mock_tcp_server.h"
#include "mock_tcp_client.h"
#include "mock_serial.h"
#include "freertos/semphr.h"

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
    mock_freertos_semaphore_reset();
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
// Test: the port's serial-path mutex cannot be created -> ESP_ERR_NO_MEM
//
// The mutex is what makes "read ctx->serial_desc and send through it" atomic against
// deinit clearing and freeing that descriptor, so running without it is a
// use-after-free waiting to happen: init must refuse, not degrade. It is also created
// before anything else is allocated, precisely so this failure needs no cleanup — which
// is the other half of what this test pins.
// ---------------------------------------------------------------------------
void test_init_serial_lock_create_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: serial-path mutex creation failure aborts init");
    LOG_MESSAGE();

    // Port 1 deliberately: the mutex is created once per port and never deleted (a receiver
    // task may be waiting on it exactly when the port is torn down), so only a port that no
    // other test has initialised still reaches xSemaphoreCreateMutex() at all.
    const unsigned port_index = 1;

    mock_xSemaphoreCreateMutex_return_value = NULL;

    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    esp_err_t result = transparent_tcp_init_port(
        port_index, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_NO_MEM, result,
        "init must fail with ESP_ERR_NO_MEM when the serial-path mutex cannot be created"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        1, mock_xSemaphoreCreateMutex_called,
        "the mutex creation must actually have been attempted on this port"
    );

    // Nothing may have been allocated: the mutex is taken before the TCP and serial layers
    // are touched, so this path has nothing to unwind and must not pretend otherwise.
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_server_calls.init_called, "tcp_server_init must not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_client_calls.init_called, "tcp_client_init must not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_calls.init_called, "serial_init must not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_server_calls.deinit_called, "nothing was allocated, so nothing may be torn down");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_calls.deinit_called, "nothing was allocated, so nothing may be torn down");
    TEST_ASSERT_NULL_MESSAGE(tcp_desc, "tcp_desc must be left untouched on this failure");
    TEST_ASSERT_NULL_MESSAGE(serial_desc, "serial_desc must be left untouched on this failure");

    // The port must be left usable, not latched into the failure: a failed creation is not
    // cached, so a later init with a working allocator brings the port up normally.
    mock_xSemaphoreCreateMutex_return_value = MOCK_SEMAPHORE_HANDLE_T;

    result = transparent_tcp_init_port(
        port_index, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "a retry with a working allocator must succeed");
    TEST_ASSERT_EQUAL_MESSAGE(
        2, mock_xSemaphoreCreateMutex_called,
        "the retry must attempt the mutex again rather than reuse a NULL handle"
    );
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.init_called, "the retry must bring the TCP side up");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.init_called, "the retry must bring the serial side up");

    transparent_tcp_deinit_port(port_index);
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

    // C7: transparent server caps connections at exactly 1 (block-new policy:
    // once one client is served, new connections are rejected).
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.set_max_connections_called,
        "tcp_server_set_max_connections must be called once in server mode");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)mock_tcp_server_calls.set_max_connections_value,
        "transparent server must cap connections at 1");

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
    // C7: the connection cap is a server-only concern; no server is started in client mode.
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_tcp_server_calls.set_max_connections_called,
        "tcp_server_set_max_connections must not be called in client mode");
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

    // ORDER MATTERS: serial first, TCP second — the same order every other branch of
    // port_manager's port_deinit_mode() uses. serial_deinit() joins the UART event task,
    // and that task is a producer into the TCP descriptor: it calls
    // tcp_server_send_to_current_client(), which can be parked for up to
    // TCP_DESC_SEND_LOCK_TIMEOUT_MS inside desc->conn_lock. Freeing the TCP descriptor
    // first lets tcp_server_deinit() delete that mutex and free the memory around a task
    // still waiting on it — its active_connections wait counts receiver tasks only, and
    // the UART task is not one of them.
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(mock_tcp_server_calls.deinit_call_seq,
        mock_serial_calls.deinit_call_seq,
        "serial_deinit must run BEFORE tcp_server_deinit: it joins the UART event task, "
        "which produces into the TCP descriptor being freed");

    // The other direction (TCP receiver task -> serial_desc) is closed by the port's serial
    // lock, not by that order: deinit must clear the descriptor under the lock, so a
    // producer either finishes its send first or observes NULL.
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xSemaphoreTake_called,
        "deinit must clear the descriptors with the port's serial lock held");
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(mock_serial_calls.deinit_call_seq,
        mock_xSemaphoreGive_call_seq,
        "deinit must release the serial lock BEFORE serial_deinit(): that call joins the "
        "UART event task, and a lock must not be held across a task join");

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

    // Same ordering rule as server mode, and if anything it binds harder here:
    // tcp_client_deinit() has no active_connections counter at all, so nothing in it even
    // looks at whether an outside producer might still be inside conn_lock. Joining the
    // UART event task first is what makes that a non-question.
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(mock_tcp_client_calls.deinit_call_seq,
        mock_serial_calls.deinit_call_seq,
        "serial_deinit must run BEFORE tcp_client_deinit: it joins the UART event task, "
        "which produces into the TCP descriptor being freed");

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
// Runtime callback tests: drive the registered serial/TCP handlers directly.
// transparent_tcp registers static process_data_from_serial / process_data_from_tcp
// callbacks via serial_init / tcp_server_init; the mocks capture those pointers so
// the relay paths (which the init/deinit tests never exercise) can be driven here.
// ---------------------------------------------------------------------------

// Helper: init a server-mode port and return the registered handlers/descriptors.
static void init_server_and_capture(serial_desc_t **out_serial_desc, tcp_desc_t **out_tcp_desc)
{
    serial_config_t cfg = make_serial_config();
    serial_desc_t  *serial_desc = NULL;
    tcp_desc_t     *tcp_desc    = NULL;

    esp_err_t result = transparent_tcp_init_port(
        0, &cfg, BRIDGE_MODE_SERVER, 502, 0, &serial_desc, &tcp_desc
    );
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "init in server mode should return ESP_OK");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_serial_registered_handler, "serial handler must be captured");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_tcp_server_registered_handler, "tcp handler must be captured");

    if (out_serial_desc) *out_serial_desc = serial_desc;
    if (out_tcp_desc)     *out_tcp_desc    = tcp_desc;
}

// ---------------------------------------------------------------------------
// M3: serial -> TCP relay is gated by the TCP-connected check (line 81).
// When connected, the packet is forwarded; when not connected, it is dropped.
// ---------------------------------------------------------------------------
void test_serial_to_tcp_forwarding_gated_by_connection(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Test: serial->TCP relay forwards only when TCP is connected (M3 gate)");
    LOG_MESSAGE();

    serial_desc_t *serial_desc = NULL;
    init_server_and_capture(&serial_desc, NULL);

    uint8_t payload[4] = { 0x01, 0x03, 0x00, 0x0A };

    // Connected: packet must be forwarded to TCP exactly once.
    mock_tcp_server_calls.connected_ret = ESP_OK;
    mock_serial_registered_handler(serial_desc, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.connected_called,
        "tcp_server_connected must be queried when relaying serial data");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.send_called,
        "packet must be forwarded to TCP when connection is up");
    TEST_ASSERT_EQUAL_MESSAGE((int)sizeof(payload), (int)mock_tcp_server_calls.send_last_len,
        "forwarded payload length must match");

    // Not connected: packet must be dropped (no extra send).
    mock_tcp_server_calls.connected_ret = ESP_FAIL;
    mock_serial_registered_handler(serial_desc, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_MESSAGE(2, mock_tcp_server_calls.connected_called,
        "tcp_server_connected must be queried again");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.send_called,
        "packet must NOT be forwarded when TCP is not connected");
}

// ---------------------------------------------------------------------------
// M2: TCP -> serial relay accepts client_sock == 0 as a valid fd (line 111).
// The guard is `client_sock < 0`; fd 0 must be relayed, not rejected.
// ---------------------------------------------------------------------------
void test_tcp_to_serial_client_sock_zero_is_valid(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Test: TCP->serial relay accepts client_sock==0 (M2 boundary)");
    LOG_MESSAGE();

    tcp_desc_t *tcp_desc = NULL;
    init_server_and_capture(NULL, &tcp_desc);

    uint8_t payload[3] = { 0xAA, 0xBB, 0xCC };

    // client_sock == 0 is a valid descriptor and must be forwarded to serial.
    mock_tcp_server_registered_handler(tcp_desc, 0, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.send_called,
        "serial_send must be called for valid client_sock == 0");
    TEST_ASSERT_EQUAL_MESSAGE((int)sizeof(payload), (int)mock_serial_calls.send_last_len,
        "relayed payload length must match");

    // A negative client_sock must be rejected (no serial_send).
    mock_tcp_server_registered_handler(tcp_desc, -1, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.send_called,
        "serial_send must NOT be called for negative client_sock");
}

// ---------------------------------------------------------------------------
// M7: TCP -> serial relay forwards only when the context is initialized (line 106).
// Positive direction: with an initialized context the packet must be relayed.
// The mutant inverts `if (!ctx->initialized)` to `if (ctx->initialized)`, which
// would early-return for an initialized context and drop the packet.
// ---------------------------------------------------------------------------
void test_tcp_to_serial_forwards_only_when_context_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Test: TCP->serial relay forwards when context is initialized (M7)");
    LOG_MESSAGE();

    tcp_desc_t *tcp_desc = NULL;
    init_server_and_capture(NULL, &tcp_desc);

    uint8_t payload[3] = { 0x11, 0x22, 0x33 };

    // Initialized context: packet is relayed to serial exactly once.
    mock_tcp_server_registered_handler(tcp_desc, 5, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.send_called,
        "serial_send must be called while context is initialized");

    // ...and it got there through the port's serial lock. Resolving ctx->serial_desc
    // outside that lock is what lets deinit clear and free the descriptor between the read
    // and the send — see test_tcp_to_serial_drops_packet_when_port_is_being_deinitialized.
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xSemaphoreTake_called,
        "the serial descriptor must be resolved with the port's serial lock held");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphore_held_count,
        "the serial lock must be released again after the send");
}

// ---------------------------------------------------------------------------
// The TCP -> serial half of the teardown race.
//
// Deinit does not stop the TCP receiver tasks before it clears and frees the serial
// descriptor: tcp_*_deinit() joins them, and that runs AFTER serial_deinit(). So a receiver
// task can be inside process_data_from_tcp(), already past the context lookup and the
// initialized check, when deinit clears ctx->serial_desc and frees it. Passing ctx->serial_desc
// straight into serial_send() then dereferences NULL (serial_send() reads desc->tx_disabled
// on its first line) or, if the pointer was loaded a moment earlier, freed memory.
//
// The single-threaded harness cannot interleave two tasks, so the semaphore mock's take-hook
// stands in for the scheduler: it fires while the producer is "waiting for" the serial lock
// and runs the whole deinit there, which is the interleaving in which deinit wins the lock.
// What is pinned is the producer's half of the contract — that it re-reads the descriptor
// under the lock and drops the packet when it is gone, instead of using a value read before.
// ---------------------------------------------------------------------------
static int s_race_deinit_calls = 0;

static void deinit_port0_from_inside_the_lock(void)
{
    // Disarm first: transparent_tcp_deinit_port() takes the same mutex, and the hook fires
    // on every take.
    mock_xSemaphoreTake_hook = NULL;
    s_race_deinit_calls++;
    transparent_tcp_deinit_port(0);
}

void test_tcp_to_serial_drops_packet_when_port_is_being_deinitialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Test: TCP->serial drops the packet when the port is being deinitialized");
    LOG_MESSAGE();

    tcp_desc_t *tcp_desc = NULL;
    init_server_and_capture(NULL, &tcp_desc);

    uint8_t payload[3] = { 0x44, 0x55, 0x66 };

    s_race_deinit_calls = 0;
    mock_xSemaphoreTake_hook = deinit_port0_from_inside_the_lock;

    mock_tcp_server_registered_handler(tcp_desc, 5, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_MESSAGE(1, s_race_deinit_calls,
        "the injected deinit must have run inside the producer's lock acquisition");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.deinit_called,
        "the injected deinit must have torn the serial port down, freeing the descriptor");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_calls.send_called,
        "a TCP packet must not reach serial_send() once ctx->serial_desc has been cleared: "
        "that pointer is being freed by serial_deinit()");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphore_held_count,
        "the serial lock must be released on the drop path too");
}

// ---------------------------------------------------------------------------
// #74 / C2: a serial reply must never be routed to a socket that has been retired.
//
// The send target used to be sampled here — process_data_from_serial() read
// desc->last_client_sock and passed the fd into the send function — and transparent_tcp
// installed a close_handler to blank that field on disconnect. Neither half was enough:
// the sampled fd was already in the producer's hands by the time the handler ran, and the
// receiver task closed (and lwIP recycled) the socket right after.
//
// Now the target is resolved by the TCP layer, under its connection lock, at the moment
// of the send — and clearing the field is part of that same locked teardown, so this port
// no longer registers a close_handler at all.
//
// Scope, honestly: "the reply cannot reach the retired fd" is no longer a property a test
// can break, because tcp_send_func_t is (desc, data, len) — there is no fd parameter for
// transparent_tcp to get wrong, so the signature enforces it, not this test. Asserting
// "the send did not go to fd 7" would therefore be unfailable padding. What IS still
// transparent_tcp's own decision, and is what this test pins, is the two halves of the
// delegation: no close_handler of its own, and the serial relay going through the
// resolve-at-send-time entry point so the target it hits is whatever the descriptor holds
// AT SEND TIME. The lock/generation machinery behind that is covered by the tcp_server
// suite against the real implementation.
// ---------------------------------------------------------------------------
void test_serial_reply_target_is_resolved_by_the_tcp_layer(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Test: transparent_tcp delegates send-target resolution to the TCP layer (#74/C2)");
    LOG_MESSAGE();

    tcp_desc_t *tcp_desc = NULL;
    init_server_and_capture(NULL, &tcp_desc);

    TEST_ASSERT_NULL_MESSAGE(tcp_desc->close_handler,
        "transparent_tcp must not clear the send target from a close_handler: tcp_server "
        "clears it, bumps the connection generation and closes the socket in one locked "
        "step, which a callback running before the close cannot do");

    // A client is admitted on fd 7 and talks to us.
    uint8_t payload[3] = { 0x11, 0x22, 0x33 };
    mock_tcp_server_simulate_client_admitted(7);
    mock_tcp_server_registered_handler(tcp_desc, 7, payload, sizeof(payload));

    // That connection is retired by tcp_server: the field goes back to the -1 sentinel
    // while active_connections is still non-zero, because a newcomer is already being
    // admitted — this is exactly the window in which the old code sent to the dead fd.
    tcp_desc->last_client_sock = -1;
    mock_tcp_server_calls.connected_ret = ESP_OK;

    serial_desc_t *serial_desc = mock_serial_get_desc();
    mock_serial_registered_handler(serial_desc, payload, sizeof(payload));

    // The relay must still hand the packet to the send function — the gate in front of it
    // is tcp_connected_func(), which says ESP_OK here, so skipping the send would be a
    // different bug (dropping traffic while a connection is live).
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.send_called,
        "serial->TCP relay must go through the send function while the port reports connected");

    // And the target it reached is the one the DESCRIPTOR held when the send ran, not the
    // fd that was live when the request arrived. The mock resolves it exactly as the real
    // tcp_server_send_to_current_client() does — from desc->last_client_sock under the
    // lock — so this pins that transparent_tcp contributes no target of its own.
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, mock_tcp_server_calls.send_last_client_sock,
        "the send target must be resolved from the descriptor at send time (retired -> -1)");
}

// ---------------------------------------------------------------------------
// B3: serial -> TCP must reach a client that has connected but never sent data.
//
// Reported bug: with MGE#1 port1 as Transparent/Server and a client connected to it,
// data arriving on RS-485 was dropped with "No client connected" until the client
// happened to transmit first — swapping which side spoke first made it work.  The
// receiver only recorded last_client_sock on receive, so a silent-but-connected
// client left it at the -1 sentinel while tcp_server_connected() already reported
// a live connection.
//
// The real registration lives in tcp_server.c (covered by the tcp_server suite,
// which mocks nothing of it); the mock here reproduces that admission contract so
// this test pins transparent_tcp's half: given an admitted client, a serial packet
// must be sent to THAT socket and never to -1.
// ---------------------------------------------------------------------------
void test_serial_to_tcp_reaches_client_that_never_sent_data(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Test: serial->TCP reaches a connected but silent client (B3)");
    LOG_MESSAGE();

    tcp_desc_t *tcp_desc = NULL;
    init_server_and_capture(NULL, &tcp_desc);

    TEST_ASSERT_EQUAL_MESSAGE(-1, tcp_desc->last_client_sock,
        "before any client connects last_client_sock must be the -1 sentinel");

    // A client connects and is admitted; it sends NOTHING. process_data_from_tcp is
    // therefore never invoked — the whole point of the regression.
    mock_tcp_server_simulate_client_admitted(9);

    // RS-485 traffic arrives and must be pushed out to that client.
    uint8_t payload[3] = { 0xDE, 0xAD, 0xBE };
    serial_desc_t *serial_desc = mock_serial_get_desc();
    mock_serial_registered_handler(serial_desc, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_tcp_server_calls.send_called,
        "serial data must be forwarded to the admitted client");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(-1, mock_tcp_server_calls.send_last_client_sock,
        "serial data must not be sent to the -1 sentinel (\"No client connected\")");
    TEST_ASSERT_EQUAL_MESSAGE(9, mock_tcp_server_calls.send_last_client_sock,
        "serial data must go to the socket of the client that is connected");
    TEST_ASSERT_EQUAL_MESSAGE((int)sizeof(payload), (int)mock_tcp_server_calls.send_last_len,
        "the full serial payload must be relayed");
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
    RUN_TEST(test_init_serial_lock_create_fail);
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
    RUN_TEST(test_serial_to_tcp_forwarding_gated_by_connection);
    RUN_TEST(test_tcp_to_serial_client_sock_zero_is_valid);
    RUN_TEST(test_tcp_to_serial_forwards_only_when_context_initialized);
    RUN_TEST(test_tcp_to_serial_drops_packet_when_port_is_being_deinitialized);
    RUN_TEST(test_serial_reply_target_is_resolved_by_the_tcp_layer);
    RUN_TEST(test_serial_to_tcp_reaches_client_that_never_sent_data);

    return UNITY_END();
}
