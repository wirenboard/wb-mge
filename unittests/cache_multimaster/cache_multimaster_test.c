#include "unity.h"
#include "console_log.h"

#include "cache_multimaster.h"
#include "semphr.h"
#include "task.h"
#include "malloc.h"
#include "esp_timer.h"
#include "esp_http_server.h"

#include <stdbool.h>
#include <string.h>

/* BRIDGES_COUNT is defined in bridge.h, but bridge.h pulls in serial.h and other
 * complex headers that are not mocked. Define locally to avoid dependency hell. */
#ifndef BRIDGES_COUNT
#define BRIDGES_COUNT 2  /* Must match the value in bridge.h */
#endif

/* Exposed by cache_multimaster.c when built under __unittest_env__ */
void cache_multimaster_test_reset(void);
bool cache_multimaster_test_get_pending_valid(uint8_t port);
bool cache_multimaster_test_set_entry_age(uint8_t slave_id, uint8_t function_code,
                                           uint16_t address, uint16_t age_s_val);
void cache_multimaster_test_tick_age(void);
uint32_t cache_multimaster_test_get_entries_dropped(void);

/* HTTP handler shims — test-only wrappers around the static handlers */
esp_err_t cache_multimaster_test_status_handler(httpd_req_t *req);
esp_err_t cache_multimaster_test_csv_handler(httpd_req_t *req);
esp_err_t cache_multimaster_test_json_handler(httpd_req_t *req);

/* ---- setUp / tearDown ---------------------------------------------------- */

void setUp(void)
{
    cache_multimaster_test_reset();
    mock_freertos_semaphore_reset();
    mock_freertos_task_reset();
    reset_malloc_tracking();
    mock_esp_timer_reset();
    mock_http_reset();
}

void tearDown(void)
{
}

/* ---- CM-U-001: cache_multimaster_init() happy path ----------------------- */

/* Verify that cache_multimaster_init() with a valid semaphore handle:
 *   - returns ESP_OK
 *   - calls xSemaphoreCreateMutex exactly once
 *   - leaves cache disabled (cache_multimaster_is_enabled() == false) */
void test_cache_multimaster_init_happy_path(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-001: cache_multimaster_init happy path");
    LOG_MESSAGE();

    /* Pre-condition: mock returns a valid handle (the default) */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_SEMAPHORE_HANDLE_T,
        mock_xSemaphoreCreateMutex_return_value,
        "Pre-condition: mock semaphore handle should be the default valid value"
    );

    /* Act */
    esp_err_t result = cache_multimaster_init();

    /* Assert: must return ESP_OK */
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "cache_multimaster_init should return ESP_OK");

    /* Assert: xSemaphoreCreateMutex called exactly once */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_xSemaphoreCreateMutex_called,
        "xSemaphoreCreateMutex should be called exactly once during init"
    );

    /* Assert: cache must be disabled after init */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_is_enabled(),
        "cache_multimaster_is_enabled() should return false after init"
    );
}

/* ---- CM-U-002: cache_multimaster_init() OOM path ------------------------- */

/* Verify that cache_multimaster_init() with a NULL semaphore handle:
 *   - returns ESP_ERR_NO_MEM
 *   - leaves cache disabled (cache_multimaster_is_enabled() == false) */
void test_cache_multimaster_init_oom(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-002: cache_multimaster_init OOM path");
    LOG_MESSAGE();

    /* Force the semaphore mock to return NULL, simulating OOM */
    mock_xSemaphoreCreateMutex_return_value = NULL;

    /* Act */
    esp_err_t result = cache_multimaster_init();

    /* Assert: must return ESP_ERR_NO_MEM */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_NO_MEM,
        result,
        "cache_multimaster_init should return ESP_ERR_NO_MEM when mutex creation fails"
    );

    /* Assert: cache must be disabled after failed init */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_is_enabled(),
        "cache_multimaster_is_enabled() should return false after failed init"
    );
}

/* ---- CM-U-003: cache_multimaster_enable() happy path --------------------- */

/* Verify that cache_multimaster_enable() after a successful init:
 *   - sets cache_multimaster_is_enabled() to true
 *   - allocates the pool via test_malloc (1 alloc, 0 frees)
 *   - creates the aging task via xTaskCreate exactly once */
void test_cache_multimaster_enable_happy_path(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-003: cache_multimaster_enable happy path");
    LOG_MESSAGE();

    /* Pre-condition: successful init */
    esp_err_t init_result = cache_multimaster_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        init_result,
        "Pre-condition: cache_multimaster_init should succeed"
    );

    /* Act */
    cache_multimaster_enable();

    /* Assert: cache must be enabled */
    TEST_ASSERT_TRUE_MESSAGE(
        cache_multimaster_is_enabled(),
        "cache_multimaster_is_enabled() should return true after enable()"
    );

    /* Assert: pool was allocated exactly once via test_malloc, never freed */
    verify_malloc_tracking(1, 0);

    /* Assert: aging task created exactly once */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_xTaskCreate_data.called,
        "xTaskCreate should be called exactly once to start the aging task"
    );

    /* Assert: mutex is balanced — every take must have a matching give */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        mock_xSemaphoreGive_called,
        mock_xSemaphoreTake_called,
        "Mutex give count must equal take count in happy path (no mutex leak)"
    );
}

/* ---- CM-U-004: cache_multimaster_enable() OOM path ----------------------- */

/* Verify that cache_multimaster_enable() when pool allocation fails:
 *   - leaves cache_multimaster_is_enabled() as false
 *   - releases the mutex after taking it (give count == take count)
 *   - records no allocations */
void test_cache_multimaster_enable_oom(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-004: cache_multimaster_enable OOM path");
    LOG_MESSAGE();

    /* Pre-condition: successful init */
    esp_err_t init_result = cache_multimaster_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        init_result,
        "Pre-condition: cache_multimaster_init should succeed"
    );

    /* Force the pool allocation to fail */
    malloc_should_fail = true;

    /* Act */
    cache_multimaster_enable();

    /* Assert: cache must remain disabled on OOM */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_is_enabled(),
        "cache_multimaster_is_enabled() should return false when pool allocation fails"
    );

    /* Assert: mutex give count must equal take count (no mutex leak) */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        mock_xSemaphoreTake_called,
        mock_xSemaphoreGive_called,
        "Mutex give count must equal take count — no mutex leak on OOM"
    );

    /* Assert: no allocation was recorded */
    verify_malloc_tracking(0, 0);
}

/* ---- CM-U-005: cache_multimaster_enable() called twice -------------------- */

/* Verify that calling cache_multimaster_enable() twice:
 *   - allocates memory only once (pool already exists on second call)
 *   - creates the aging task only once (task already exists on second call)
 *   - clears the pool on each call (memset to 0), so a previously cached entry
 *     becomes NOT_FOUND after the second enable
 *   - resets stats on each call
 *   - leaves cache_multimaster_is_enabled() == true */
void test_cache_multimaster_enable_twice(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-005: cache_multimaster_enable called twice");
    LOG_MESSAGE();

    /* Pre-condition: successful init */
    esp_err_t init_result = cache_multimaster_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        init_result,
        "Pre-condition: cache_multimaster_init should succeed"
    );

    /* First enable: pool is allocated, task is created */
    cache_multimaster_enable();
    TEST_ASSERT_TRUE_MESSAGE(
        cache_multimaster_is_enabled(),
        "Cache must be enabled after first enable()"
    );

    /* Store a register: slave 10, FC03, address 0, value 0xABCD */
    cache_multimaster_on_request(0, 10, 3, 0, 1);
    uint8_t data[] = {
        10,             /* [0] slave_id */
        0x03,           /* [1] FC */
        2,              /* [2] byte_count (1 reg × 2 bytes) */
        0xAB, 0xCD      /* reg 0: 0xABCD */
    };
    cache_multimaster_on_response(0, 10, 3, data, sizeof(data), 0);

    /* Verify entry is FOUND before second enable */
    uint16_t val = 0;
    cache_lookup_result_t r = cache_multimaster_lookup(10, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        CACHE_LOOKUP_FOUND,
        r,
        "Entry must be FOUND after first enable and store"
    );

    /* Second enable: pool is NOT re-allocated (reuses existing), task is NOT re-created,
     * but pool IS cleared via memset(s_pool, 0, ...) */
    cache_multimaster_enable();

    /* Cache must still be enabled after second enable */
    TEST_ASSERT_TRUE_MESSAGE(
        cache_multimaster_is_enabled(),
        "Cache must be enabled after second enable()"
    );

    /* Assert: only 1 allocation total (pool was not freed and re-allocated) */
    verify_malloc_tracking(1, 0);

    /* Assert: mutex is balanced after both enable() calls — no mutex leak */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        mock_xSemaphoreGive_called,
        mock_xSemaphoreTake_called,
        "Mutex give count must equal take count after two enable() calls"
    );

    /* Assert: xTaskCreate called only once (task not re-created on second enable) */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_xTaskCreate_data.called,
        "xTaskCreate should be called only once even after two enable() calls"
    );

    /* Assert: pool was cleared — the previously cached entry must now be NOT_FOUND */
    val = 0xDEAD;
    r = cache_multimaster_lookup(10, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        CACHE_LOOKUP_NOT_FOUND,
        r,
        "Entry must be NOT_FOUND after second enable() clears the pool"
    );
}

/* ---- CM-U-006: cache_multimaster_disable() -------------------------------- */

/* Verify that cache_multimaster_disable() after enable():
 *   - sets cache_multimaster_is_enabled() to false
 *   - deletes the aging task via vTaskDelete exactly once
 *   - frees the pool (re-enable afterwards triggers a new xTaskCreate, call count == 2) */
void test_cache_multimaster_disable(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-006: cache_multimaster_disable");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    esp_err_t init_result = cache_multimaster_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, init_result,
        "Pre-condition: cache_multimaster_init should succeed");
    cache_multimaster_enable();
    TEST_ASSERT_TRUE_MESSAGE(cache_multimaster_is_enabled(),
        "Pre-condition: cache should be enabled before disable()");

    /* Act */
    cache_multimaster_disable();

    /* Assert: cache must be disabled */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_is_enabled(),
        "cache_multimaster_is_enabled() should return false after disable()"
    );

    /* Assert: aging task deleted exactly once */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_vTaskDelete_data.called,
        "vTaskDelete should be called exactly once to stop the aging task"
    );

    /* Assert: re-enable creates a new task (total xTaskCreate calls == 2),
     * which proves the pool was freed and re-allocated on the second enable.
     * Also verify directly: 2 allocations total, 1 freed (by disable). */
    cache_multimaster_enable();
    TEST_ASSERT_TRUE_MESSAGE(
        cache_multimaster_is_enabled(),
        "cache should be re-enabled after second enable()"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        2,
        mock_xTaskCreate_data.called,
        "xTaskCreate should be called a second time after re-enable (pool was freed)"
    );
    verify_malloc_tracking(2, 1);
}

/* ---- CM-U-007: cache_multimaster_clear() with NULL mutex ------------------ */

/* Verify that cache_multimaster_clear() does not crash when called before
 * cache_multimaster_init() (so s_cache_mutex is NULL). The function must
 * return silently without any observable side effects. */
void test_cache_multimaster_clear_null_mutex(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-007: cache_multimaster_clear with NULL mutex");
    LOG_MESSAGE();

    /* Pre-condition: cache_multimaster_test_reset() was called in setUp(), so
     * s_cache_mutex is NULL and s_pool is NULL. Do NOT call init() here. */

    /* Act: must not crash even with NULL mutex */
    cache_multimaster_clear();

    /* Verify the early return fired: no mutex operations should have occurred */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreTake_called,
        "clear() with NULL mutex must not take the semaphore");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreGive_called,
        "clear() with NULL mutex must not give the semaphore");
}

/* ---- CM-U-008: cache_multimaster_clear() with pool ----------------------- */

/* Verify that cache_multimaster_clear() after init + enable:
 *   - removes all cached entries (lookup returns NOT_FOUND after clear)
 *   - resets s_pending (both ports valid=false after clear)
 *   - releases the mutex properly (give count == take count) */
void test_cache_multimaster_clear_with_pool(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-008: cache_multimaster_clear with pool");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store an entry: slave 30, FC03, address 0, value 0x5678 */
    cache_multimaster_on_request(0, 30, 3, 0, 1);
    uint8_t data[] = {
        30,             /* [0] slave_id */
        0x03,           /* [1] FC */
        2,              /* [2] byte_count (1 reg × 2 bytes) */
        0x56, 0x78      /* reg 0: 0x5678 */
    };
    cache_multimaster_on_response(0, 30, 3, data, sizeof(data), 0);

    /* Verify entry is FOUND before clear */
    uint16_t val = 0;
    cache_lookup_result_t r = cache_multimaster_lookup(30, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        CACHE_LOOKUP_FOUND,
        r,
        "Entry must be FOUND before cache_multimaster_clear()"
    );

    /* Set a pending request on port 0 to verify clear() resets it */
    cache_multimaster_on_request(0, 30, 3, 0, 1);
    TEST_ASSERT_TRUE_MESSAGE(
        cache_multimaster_test_get_pending_valid(0),
        "Pending on port 0 must be valid before clear()"
    );

    /* Act: clear the cache */
    cache_multimaster_clear();

    /* Entry must be NOT_FOUND after clear (pool was memset'd to 0) */
    val = 0xFFFF;
    r = cache_multimaster_lookup(30, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        CACHE_LOOKUP_NOT_FOUND,
        r,
        "Entry must be NOT_FOUND after cache_multimaster_clear()"
    );

    /* s_pending must be fully cleared (memset to 0) by clear() */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_test_get_pending_valid(0),
        "Pending port 0 must be valid=false after cache_multimaster_clear()"
    );
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_test_get_pending_valid(1),
        "Pending port 1 must be valid=false after cache_multimaster_clear()"
    );

    /* Mutex must be balanced: give count == take count */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        mock_xSemaphoreGive_called,
        mock_xSemaphoreTake_called,
        "Mutex must be balanced after cache_multimaster_clear()"
    );
}

/* ---- CM-U-009: cache_multimaster_on_request() OOB port ------------------- */

/* Verify that cache_multimaster_on_request() with an out-of-bounds port:
 *   - does NOT crash
 *   - a subsequent valid-port request still works and can be cached */
void test_cache_multimaster_on_request_oob_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-009: cache_multimaster_on_request OOB port");
    LOG_MESSAGE();

    /* Pre-condition: init only (no enable yet — pool not allocated) */
    esp_err_t init_result = cache_multimaster_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, init_result,
        "Pre-condition: cache_multimaster_init should succeed");

    /* OOB calls must not crash (BRIDGES_COUNT == 2, so port 2 is OOB) */
    cache_multimaster_on_request(BRIDGES_COUNT, 1, 3, 0, 10);   /* port == BRIDGES_COUNT — OOB */
    cache_multimaster_on_request(255, 1, 3, 0, 10);              /* port == 255 — OOB */

    /* Verify OOB calls left s_pending[] untouched — both ports must still be valid=false */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_test_get_pending_valid(0),
        "OOB on_request should not modify s_pending[0]"
    );
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_test_get_pending_valid(1),
        "OOB on_request should not modify s_pending[1]"
    );

    /* Valid port: set up a pending request before the pool is available.
     * This call should be accepted (port 0 < BRIDGES_COUNT) without crashing. */
    cache_multimaster_on_request(0, 42, 3, 100, 5);

    /* Now enable so the pool is allocated — required for on_response to store data */
    cache_multimaster_enable();
    TEST_ASSERT_TRUE_MESSAGE(cache_multimaster_is_enabled(),
        "Pre-condition: cache should be enabled before on_response");

    /* Set a fresh pending request on valid port 0 so on_response matches it */
    cache_multimaster_on_request(0, 42, 3, 100, 5);

    /* Build a valid FC03 response for slave 42, 5 registers starting at address 100.
     * Layout: [0]=slave_id [1]=FC [2]=byte_count [3..12]=register values (big-endian)
     * Values: reg100=0x0001, reg101=0x0002, reg102=0x0003, reg103=0x0004, reg104=0x0005 */
    uint8_t data[] = {
        42,                         /* [0] slave_id */
        0x03,                       /* [1] function code */
        10,                         /* [2] byte count (5 registers × 2 bytes) */
        0x00, 0x01,                 /* reg 100: 0x0001 */
        0x00, 0x02,                 /* reg 101: 0x0002 */
        0x00, 0x03,                 /* reg 102: 0x0003 */
        0x00, 0x04,                 /* reg 103: 0x0004 */
        0x00, 0x05                  /* reg 104: 0x0005 */
    };

    cache_multimaster_on_response(0, 42, 3, data, sizeof(data), 0);

    /* Lookup register 100 — must be FOUND with value 0x0001 */
    uint16_t value = 0xFFFF;
    cache_lookup_result_t result = cache_multimaster_lookup(42, 3, 100, &value, 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        CACHE_LOOKUP_FOUND,
        result,
        "cache_multimaster_lookup should return CACHE_LOOKUP_FOUND for register 100 after valid response"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0x0001,
        value,
        "Cached value for register 100 should be 0x0001"
    );
}

/* ---- CM-U-047: cache_multimaster_on_response() OOB port boundary --------- */
void test_cache_multimaster_on_response_oob_port_boundary(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-047: on_response OOB port == BRIDGES_COUNT boundary");
    LOG_MESSAGE();

    cache_multimaster_init();
    cache_multimaster_enable();
    TEST_ASSERT_TRUE_MESSAGE(cache_multimaster_is_enabled(), "cache must be enabled");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xTaskCreate_data.called, "aging task created once");

    uint8_t data[] = { 5, 0x03, 4, 0x00, 0x01, 0x00, 0x02 };
    cache_multimaster_on_response(BRIDGES_COUNT, 5, 3, data, sizeof(data), 0);

    TEST_ASSERT_FALSE_MESSAGE(cache_multimaster_test_get_pending_valid(0), "OOB must not affect s_pending[0]");
    TEST_ASSERT_FALSE_MESSAGE(cache_multimaster_test_get_pending_valid(1), "OOB must not affect s_pending[1]");

    cache_multimaster_disable();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_vTaskDelete_data.called, "aging task deleted once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE((TaskHandle_t)0xCCCCCCCC, mock_vTaskDelete_data.xTaskToDelete,
        "s_age_task must be intact: on_response(port==BRIDGES_COUNT) must not write OOB");
}

/* ---- CM-U-048: on_response() minimum data_len boundary (data_len < 4) ----- */
/* A 3-byte response (slave + FC + byte_count, no payload) must be rejected by
 * the `data_len < 4` guard BEFORE the pending request is consumed. The slave_id
 * and function deliberately MATCH the pending request, so the only thing that
 * keeps the pending alive is the length guard. If the floor is lowered (e.g.
 * `< 3`), the response is processed and the pending request is consumed. */
void test_cache_multimaster_on_response_min_len_boundary(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-048: on_response data_len < 4 minimum-length guard");
    LOG_MESSAGE();

    cache_multimaster_init();
    cache_multimaster_enable();

    /* Pending request that the short response would otherwise match */
    cache_multimaster_on_request(0, 7, 3, 0, 1);
    TEST_ASSERT_TRUE_MESSAGE(
        cache_multimaster_test_get_pending_valid(0),
        "Pending must be valid after on_request()"
    );

    /* 3-byte response: matching slave/FC, but below the 4-byte minimum */
    uint8_t data[] = { 7, 0x03, 2 };
    cache_multimaster_on_response(0, 7, 3, data, sizeof(data), 0);

    /* Original rejects the too-short frame BEFORE consuming the pending request */
    TEST_ASSERT_TRUE_MESSAGE(
        cache_multimaster_test_get_pending_valid(0),
        "Response shorter than 4 bytes must be rejected without consuming the pending request"
    );
}

/* ---- CM-U-010: cache_multimaster_on_request() valid ---------------------- */

/* Verify that cache_multimaster_on_request() stores a pending request and that
 * a subsequent matching on_response() stores the data in the cache so it can
 * be retrieved via cache_multimaster_lookup(). */
void test_cache_multimaster_on_request_valid(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-010: cache_multimaster_on_request valid");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Set up a pending request for slave 42, FC03, 5 registers starting at 100 */
    cache_multimaster_on_request(0, 42, 3, 100, 5);

    /* Pending must be marked valid */
    TEST_ASSERT_TRUE_MESSAGE(
        cache_multimaster_test_get_pending_valid(0),
        "Pending must be valid after on_request()"
    );

    /* Build a valid FC03 response: 5 regs, first reg = 0x0100 */
    uint8_t data[] = {
        42,                 /* [0] slave_id */
        0x03,               /* [1] function code */
        10,                 /* [2] byte count (5 regs × 2 bytes) */
        0x01, 0x00,         /* reg 100: 0x0100 */
        0x00, 0x00,         /* reg 101: 0x0000 */
        0x00, 0x00,         /* reg 102: 0x0000 */
        0x00, 0x00,         /* reg 103: 0x0000 */
        0x00, 0x00          /* reg 104: 0x0000 */
    };
    cache_multimaster_on_response(0, 42, 3, data, sizeof(data), 0);

    /* Pending must be consumed */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_test_get_pending_valid(0),
        "Pending must be cleared after on_response()"
    );

    /* Lookup register 100 — must be FOUND with value 0x0100 */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t result = cache_multimaster_lookup(42, 3, 100, &val, 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        CACHE_LOOKUP_FOUND,
        result,
        "lookup(42, 3, 100) should return CACHE_LOOKUP_FOUND"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0x0100,
        val,
        "Cached value for register 100 should be 0x0100"
    );
}

/* ---- CM-U-011: on_response() with no matching pending request ------------ */

/* Verify that a response with a slave_id that does not match the pending
 * request is discarded: nothing is stored in the cache. */
void test_cache_multimaster_on_response_no_pending(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-011: on_response with no matching pending");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Set up a pending request for slave 1 */
    cache_multimaster_on_request(0, 1, 3, 0, 2);

    /* Respond with a different slave_id (2 instead of 1) — mismatch */
    uint8_t data[] = {
        2,              /* [0] slave_id — does NOT match pending (slave 1) */
        0x03,           /* [1] FC */
        4,              /* [2] byte count (2 regs) */
        0x00, 0x01,     /* reg 0: 1 */
        0x00, 0x02      /* reg 1: 2 */
    };
    cache_multimaster_on_response(0, 2, 3, data, sizeof(data), 0);

    /* Pending must be consumed (cleared on mismatch) */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_test_get_pending_valid(0),
        "Pending must be cleared on slave_id mismatch"
    );

    /* Nothing stored for slave 2 */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t result = cache_multimaster_lookup(2, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        CACHE_LOOKUP_NOT_FOUND,
        result,
        "lookup(2, 3, 0) should return CACHE_LOOKUP_NOT_FOUND after mismatched response"
    );
}

/* ---- CM-U-012: on_response() FC03 — correct register values ------------- */

/* Verify that on_response() for FC03 stores all register values at the correct
 * addresses using big-endian byte ordering. */
void test_cache_multimaster_on_response_fc03_correct(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-012: on_response FC03 correct values");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Request: slave 5, FC03, 2 registers starting at address 200 */
    cache_multimaster_on_request(0, 5, 3, 200, 2);

    /* Response: addr200=0xABCD, addr201=0x1234 */
    uint8_t data[] = {
        5,              /* [0] slave_id */
        0x03,           /* [1] FC */
        4,              /* [2] byte count (2 regs × 2 bytes) */
        0xAB, 0xCD,     /* reg 200: 0xABCD */
        0x12, 0x34      /* reg 201: 0x1234 */
    };
    cache_multimaster_on_response(0, 5, 3, data, sizeof(data), 2000000);

    uint16_t val = 0;

    /* Check register 200 */
    cache_lookup_result_t r = cache_multimaster_lookup(5, 3, 200, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup(5, 3, 200) should return FOUND");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xABCD, val,
        "register 200 value should be 0xABCD");

    /* Check register 201 */
    val = 0;
    r = cache_multimaster_lookup(5, 3, 201, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup(5, 3, 201) should return FOUND");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x1234, val,
        "register 201 value should be 0x1234");
}

/* ---- CM-U-013: on_response() FC03 — truncated byte_count ----------------- */

/* Verify that when byte_count in the response is smaller than what the pending
 * request asked for, only the available registers are stored and the rest are
 * not found. */
void test_cache_multimaster_on_response_truncated_fc03(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-013: on_response FC03 truncated byte_count");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Request 5 registers, but the response only carries 2 */
    cache_multimaster_on_request(0, 7, 3, 0, 5);

    /* byte_count=4 → only 2 registers; data_len=7 covers exactly these 2 regs */
    uint8_t data[] = {
        7,              /* [0] slave_id */
        0x03,           /* [1] FC */
        4,              /* [2] byte count (only 2 regs, not 5) */
        0x00, 0x01,     /* reg 0: 1 */
        0x00, 0x02      /* reg 1: 2 */
    };
    cache_multimaster_on_response(0, 7, 3, data, sizeof(data), 0);

    uint16_t val = 0;

    /* Register 0 must be FOUND with value 1 */
    cache_lookup_result_t r = cache_multimaster_lookup(7, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup(7, 3, 0) should return FOUND");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, val, "register 0 value should be 1");

    /* Register 1 must be FOUND with value 2 */
    val = 0;
    r = cache_multimaster_lookup(7, 3, 1, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup(7, 3, 1) should return FOUND");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, val, "register 1 value should be 2");

    /* Register 2 must NOT be found (byte_count clamped count to 2) */
    val = 0;
    r = cache_multimaster_lookup(7, 3, 2, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND, r,
        "lookup(7, 3, 2) should return NOT_FOUND (clamped by byte_count)");
}

/* ---- CM-U-014: on_response() FC03 — malformed: data_len too short -------- */

/* Verify that on_response() does not take the mutex and does not store data
 * when the byte_count field claims more data than data_len provides. */
void test_cache_multimaster_on_response_malformed_length(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-014: on_response FC03 malformed data_len");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Request 10 registers for slave 9 */
    cache_multimaster_on_request(0, 9, 3, 0, 10);

    /* byte_count=20 claims 10 registers but data_len=5 is far too short:
     * 3 + 10*2 = 23 > 5 → the bounds check must trigger an early return */
    uint8_t data[] = {
        9,              /* [0] slave_id */
        0x03,           /* [1] FC */
        20,             /* [2] byte_count — claims 10 regs but buffer is too small */
        0x00, 0x01      /* only 2 more bytes, nowhere near enough */
    };

    /* Record the mutex take count before the call */
    int take_before = mock_xSemaphoreTake_called;

    cache_multimaster_on_response(0, 9, 3, data, sizeof(data), 0);

    /* The mutex must NOT have been taken (early exit before the mutex section) */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        take_before,
        mock_xSemaphoreTake_called,
        "mutex must not be taken when data_len is too short"
    );

    /* Pending must be consumed (set to false) even on early return:
     * on_response() clears s_pending[port].valid BEFORE the bounds check —
     * a malformed response silently invalidates the pending context. */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_test_get_pending_valid(0),
        "pending must be consumed before the malformed-length early return"
    );

    /* Nothing must be stored */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(9, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND, r,
        "lookup(9, 3, 0) should return NOT_FOUND after malformed response");
}

/* ---- CM-U-015: on_response() FC01 — 9 coils, bit-packed LSB-first -------- */

/* Verify that coil values are correctly unpacked from the bit-packed
 * FC01 response format (LSB of first byte = first coil). */
void test_cache_multimaster_on_response_fc01_9_coils(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-015: on_response FC01 9 coils");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Request 9 coils starting at address 0 for slave 3 */
    cache_multimaster_on_request(0, 3, 1, 0, 9);

    /* byte_count=2: first byte 0xAA (10101010b), second byte 0x01 (00000001b)
     * Coil assignments (bit N = coil N):
     *   data[3]=0xAA: coil[0]=0, coil[1]=1, coil[2]=0, coil[3]=1,
     *                 coil[4]=0, coil[5]=1, coil[6]=0, coil[7]=1
     *   data[4]=0x01: coil[8]=1 */
    uint8_t data[] = {
        3,      /* [0] slave_id */
        0x01,   /* [1] FC */
        2,      /* [2] byte_count */
        0xAA,   /* coils 0-7: 0,1,0,1,0,1,0,1 */
        0x01    /* coil  8:   1                */
    };
    cache_multimaster_on_response(0, 3, 1, data, sizeof(data), 0);

    /* Expected coil values: alternating 0/1 starting at 0, coil[8]=1 */
    uint16_t expected[] = {0, 1, 0, 1, 0, 1, 0, 1, 1};

    for (int i = 0; i < 9; i++) {
        uint16_t val = 0xFFFF;
        cache_lookup_result_t r = cache_multimaster_lookup(3, 1, (uint16_t)i, &val, 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
            "coil lookup should return FOUND");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(expected[i], val,
            "coil value mismatch");
    }
}

/* ---- CM-U-016: on_response() FC01 — count overflow (clamped to 2000) ----- */

/* Verify that a pending request with count > 2000 is safely clamped to 2000
 * and the implementation does not crash. */
void test_cache_multimaster_on_response_fc01_count_overflow(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-016: on_response FC01 count overflow");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Request with count=65535 — will be clamped to 2000 */
    cache_multimaster_on_request(0, 11, 1, 0, 65535);

    /* Build a response with byte_count=250 (covers 2000 coils: (2000+7)/8 = 250).
     * All coil bits are 1 (0xFF). */
    uint8_t response_buf[253];
    response_buf[0] = 11;    /* slave_id */
    response_buf[1] = 0x01;  /* FC01 */
    response_buf[2] = 250;   /* byte_count */
    memset(response_buf + 3, 0xFF, 250); /* all 2000 coils = 1 */

    cache_multimaster_on_response(0, 11, 1, response_buf, sizeof(response_buf), 0);

    /* Coil 0 must be stored and equal 1 */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(11, 1, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "coil 0 should be FOUND after overflow response");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, val, "coil 0 should be 1");

    /* Coil 1999 (last valid after clamping) must be FOUND */
    val = 0xFFFF;
    r = cache_multimaster_lookup(11, 1, 1999, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "coil 1999 should be FOUND (last valid after count clamped to 2000)");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, val, "coil 1999 should be 1");

    /* Coil 2000 must NOT be found (count was clamped to 2000) */
    val = 0xFFFF;
    r = cache_multimaster_lookup(11, 1, 2000, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND, r,
        "coil 2000 should be NOT_FOUND (count clamped to 2000, indices 0-1999 only)");
}

/* ---- CM-U-049: on_response() FC01 — coil count clamp boundary (2001) ----- */

/* Verify the coil-count clamp uses `> 2000` (clamp to 2000), not `> 2001`.
 * A pending request for exactly 2001 coils with a response that carries enough
 * bytes (251) for all of them is the only input that distinguishes the two
 * boundaries: the original clamps to 2000 (coil 2000 is NOT stored), whereas a
 * `> 2001` mutant leaves count at 2001 and stores coil 2000. */
void test_cache_multimaster_on_response_fc01_count_clamp_boundary_2001(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-049: on_response FC01 count clamp boundary (2001 -> 2000)");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Request exactly 2001 coils starting at address 0 for slave 12 */
    cache_multimaster_on_request(0, 12, 1, 0, 2001);

    /* Response carries byte_count=251 (= ceil(2001/8)) so the bounds check
     * passes for both the clamped (2000) and unclamped (2001) counts.
     * data_len = 3 + 251 = 254. All coils = 1 (0xFF). */
    uint8_t response_buf[254];
    response_buf[0] = 12;    /* slave_id */
    response_buf[1] = 0x01;  /* FC01 */
    response_buf[2] = 251;   /* byte_count */
    memset(response_buf + 3, 0xFF, 251);

    cache_multimaster_on_response(0, 12, 1, response_buf, sizeof(response_buf), 0);

    /* Coil 1999 (last valid after clamping to 2000) must be FOUND */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(12, 1, 1999, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "coil 1999 should be FOUND (within the 2000-coil clamp)");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, val, "coil 1999 should be 1");

    /* Coil 2000 must NOT be found: count must be clamped to 2000 (> 2000),
     * so indices 0..1999 only. A `> 2001` mutant would store coil 2000. */
    val = 0xFFFF;
    r = cache_multimaster_lookup(12, 1, 2000, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND, r,
        "coil 2000 should be NOT_FOUND (count clamped to 2000, not 2001)");
}

/* ---- CM-U-017: on_response() FC02 — byte_count shorter than requested ---- */

/* Verify that when byte_count is shorter than the requested count, the
 * implementation stores only byte_count*8 discretes and does not read past
 * the available data. */
void test_cache_multimaster_on_response_fc02_byte_count_short(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-017: on_response FC02 byte_count short");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Request 16 discrete inputs but the response only carries 1 byte (8 discretes) */
    cache_multimaster_on_request(0, 13, 2, 0, 16);

    /* byte_count=1 with all bits set: only 8 discretes available, all value 1 */
    uint8_t data[] = {
        13,     /* [0] slave_id */
        0x02,   /* [1] FC02 */
        1,      /* [2] byte_count = 1 (covers only 8 bits) */
        0xFF    /* bits: discretes 0-7 all = 1 */
    };
    cache_multimaster_on_response(0, 13, 2, data, sizeof(data), 0);

    /* Discretes 0-7 must all be FOUND with value 1 */
    for (int i = 0; i < 8; i++) {
        uint16_t val = 0xFFFF;
        cache_lookup_result_t r = cache_multimaster_lookup(13, 2, (uint16_t)i, &val, 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
            "discrete 0-7 should be FOUND");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, val,
            "discrete 0-7 value should be 1");
    }

    /* Discrete 8 must NOT be found (only 8 entries stored from 1 byte) */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(13, 2, 8, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND, r,
        "discrete 8 should be NOT_FOUND (count clamped by short byte_count)");
}

/* ---- CM-U-018: on_response() — null data and too-short data_len ---------- */

/* Verify that on_response() does not crash when passed NULL data or a data_len
 * that is too short to contain a valid response header. */
void test_cache_multimaster_on_response_null_short_data(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-018: on_response null/short data");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Sub-test A: data == NULL — must return without crash.
     * data=NULL is checked before the pending check, so s_pending stays valid. */
    cache_multimaster_on_request(0, 15, 3, 0, 1);
    cache_multimaster_on_response(0, 15, 3, NULL, 4, 0); /* data=NULL → early return */

    /* Pending must still be valid: NULL-data early return fires before the pending check */
    TEST_ASSERT_TRUE_MESSAGE(
        cache_multimaster_test_get_pending_valid(0),
        "NULL data must NOT consume pending: early exit at line 300 before pending check"
    );

    /* Pending is still valid: NULL check fires before the pending check */
    cache_multimaster_on_request(0, 15, 3, 0, 1); /* re-set pending for sub-test B */

    /* Sub-test B: data_len < 4 — must return without crash */
    uint8_t short_data[] = {15, 0x03, 0}; /* only 3 bytes — too short */
    cache_multimaster_on_response(0, 15, 3, short_data, 3, 0);

    /* Nothing must be stored in either sub-test */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(15, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND, r,
        "lookup(15, 3, 0) should return NOT_FOUND after null/short data");
}

/* ---- CM-U-019: lookup() — timeout=0 disables age check ------------------- */

/* Verify that cache_multimaster_lookup() returns FOUND regardless of age_s
 * when value_timeout_s == 0 (age check disabled). */
void test_cache_multimaster_lookup_timeout_zero(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-019: lookup timeout=0 disables age check");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store an entry: slave 20, FC03, address 0, value 0x1234 */
    cache_multimaster_on_request(0, 20, 3, 0, 1);
    uint8_t data[] = {
        20,             /* [0] slave_id */
        0x03,           /* [1] FC */
        2,              /* [2] byte_count (1 reg × 2 bytes) */
        0x12, 0x34      /* reg 0: 0x1234 */
    };
    cache_multimaster_on_response(0, 20, 3, data, sizeof(data), 0);

    /* Set age_s to the maximum (65535) to ensure an age check would fail */
    bool set_ok = cache_multimaster_test_set_entry_age(20, 3, 0, 65535);
    TEST_ASSERT_TRUE_MESSAGE(set_ok, "cache_multimaster_test_set_entry_age must return true");

    /* Lookup with timeout=0 must still return FOUND (age check disabled) */
    uint16_t val = 0;
    cache_lookup_result_t r = cache_multimaster_lookup(20, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup with timeout=0 should return FOUND even when age_s is at maximum");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x1234, val,
        "value should be 0x1234");
}

/* ---- CM-U-020: lookup() — age check: STALE vs FOUND --------------------- */

/* Verify that cache_multimaster_lookup() correctly returns STALE when
 * age_s exceeds the timeout and FOUND when age_s is within the timeout. */
void test_cache_multimaster_lookup_age_check(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-020: lookup age check STALE vs FOUND");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store an entry: slave 22, FC03, address 0, value 0xBEEF */
    cache_multimaster_on_request(0, 22, 3, 0, 1);
    uint8_t data[] = {
        22,             /* [0] slave_id */
        0x03,           /* [1] FC */
        2,              /* [2] byte_count */
        0xBE, 0xEF      /* reg 0: 0xBEEF */
    };
    cache_multimaster_on_response(0, 22, 3, data, sizeof(data), 0);

    /* Set age_s to 100 */
    bool set_ok = cache_multimaster_test_set_entry_age(22, 3, 0, 100);
    TEST_ASSERT_TRUE_MESSAGE(set_ok, "cache_multimaster_test_set_entry_age must return true");

    /* With timeout=50: age(100) > timeout(50) → STALE */
    uint16_t val = 0;
    cache_lookup_result_t r = cache_multimaster_lookup(22, 3, 0, &val, 50);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_STALE, r,
        "lookup with timeout=50 should return STALE when age_s=100");

    /* With timeout=200: age(100) ≤ timeout(200) → FOUND with correct value */
    val = 0;
    r = cache_multimaster_lookup(22, 3, 0, &val, 200);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup with timeout=200 should return FOUND when age_s=100");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xBEEF, val,
        "value should be 0xBEEF");

    /* Exact boundary case: age_s == timeout must return FOUND (strictly greater-than check) */
    r = cache_multimaster_lookup(22, 3, 0, &val, 100);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "age_s == value_timeout_s must return FOUND (> is strict, not >=)");
}

/* ---- CM-U-021: lookup() — age saturation boundary near CACHE_AGE_MAX_S -- */

/* Verify that the saturating age counter approach works correctly near the
 * maximum boundary (65535), proving there is no wrap-around problem.
 * Also verifies the foundational invariant: on_response() always resets age_s
 * to 0, so a fresh store never inherits a saturated age value. This is the
 * key property that prevents the wrap-around defect. */
void test_cache_multimaster_lookup_age_saturation_boundary(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-021: lookup age saturation boundary");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store an entry: slave 24, FC03, address 0, value 0x4321 */
    cache_multimaster_on_request(0, 24, 3, 0, 1);
    uint8_t data[] = {
        24,             /* [0] slave_id */
        0x03,           /* [1] FC */
        2,              /* [2] byte_count */
        0x43, 0x21      /* reg 0: 0x4321 */
    };
    cache_multimaster_on_response(0, 24, 3, data, sizeof(data), 0);

    /* Set age_s to 65500 (near saturation limit 65535) */
    bool set_ok = cache_multimaster_test_set_entry_age(24, 3, 0, 65500);
    TEST_ASSERT_TRUE_MESSAGE(set_ok, "cache_multimaster_test_set_entry_age must return true");

    /* With timeout=65535: age(65500) ≤ timeout(65535) → FOUND */
    uint16_t val = 0;
    cache_lookup_result_t r = cache_multimaster_lookup(24, 3, 0, &val, 65535);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup with timeout=65535 should return FOUND when age_s=65500");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x4321, val,
        "value should be 0x4321");

    /* With timeout=65499: age(65500) > timeout(65499) → STALE */
    val = 0;
    r = cache_multimaster_lookup(24, 3, 0, &val, 65499);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_STALE, r,
        "lookup with timeout=65499 should return STALE when age_s=65500");

    /* Verify the foundational invariant: on_response() always resets age_s to 0.
     * This proves there is no wrap-around: a fresh store always starts at age 0,
     * never at a value inherited from before the saturation. */
    cache_multimaster_on_request(0, 24, 3, 0, 1);  /* same slave, FC, addr, count as above */
    cache_multimaster_on_response(0, 24, 3, data, sizeof(data), 0); /* store again: age_s reset to 0 */

    /* After re-storing, age_s is reset to 0 by on_response().
     * Use timeout=1 (not 0): this exercises the real age comparison path and proves
     * age_s was truly reset — 0 > 1 is false → FOUND (not the timeout=0 bypass). */
    val = 0;
    r = cache_multimaster_lookup(24, 3, 0, &val, 1); /* timeout=1: age_s=0 ≤ 1 → FOUND */
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "re-storing a saturated entry must reset age_s to 0 → FOUND with timeout=1");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x4321, val, "value must still be 0x4321 after re-store");
}

/* ---- CM-U-022: lookup() NOT_FOUND — value_out not modified ---------------- */

/* Verify that cache_multimaster_lookup() does NOT modify *value_out when the
 * lookup returns NOT_FOUND (cache miss). The sentinel value must be preserved. */
void test_cache_multimaster_lookup_not_found_value_unchanged(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-022: lookup NOT_FOUND: value_out not modified");
    LOG_MESSAGE();

    /* Pre-condition: init + enable (pool allocated, mutex valid) */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Sentinel value — must remain unchanged after a cache miss */
    uint16_t val = 0xDEAD;

    /* Lookup a slave that was never stored: slave 99, FC03, address 0 */
    cache_lookup_result_t r = cache_multimaster_lookup(99, 3, 0, &val, 0);

    /* Must return NOT_FOUND */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        CACHE_LOOKUP_NOT_FOUND,
        r,
        "lookup(99, 3, 0) should return NOT_FOUND for a never-stored slave"
    );

    /* value_out must be unchanged on a cache miss */
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0xDEAD,
        val,
        "value_out must not be modified when lookup returns NOT_FOUND"
    );
}

/* ---- CM-U-023: lookup() with NULL value_out ------------------------------- */

/* Verify that cache_multimaster_lookup() returns CACHE_LOOKUP_NOT_FOUND and
 * does not crash when value_out is NULL. */
void test_cache_multimaster_lookup_null_value_out(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-023: lookup with NULL value_out");
    LOG_MESSAGE();

    /* Pre-condition: init only (mutex created).
     * The NULL check fires before pool check, so enable() is not required. */
    cache_multimaster_init();

    /* Act: pass NULL as value_out — must not crash */
    cache_lookup_result_t r = cache_multimaster_lookup(1, 3, 0, NULL, 0);

    /* Must return NOT_FOUND when value_out is NULL */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        CACHE_LOOKUP_NOT_FOUND,
        r,
        "lookup with NULL value_out must return CACHE_LOOKUP_NOT_FOUND"
    );
}

/* ---- CM-U-024: lookup() with unknown function code ------------------------ */

/* Verify that cache_multimaster_lookup() returns NOT_FOUND and does not crash
 * when an unknown function code is passed (not 0x01, 0x02, 0x03, or 0x04). */
void test_cache_multimaster_lookup_unknown_fc(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-024: lookup with unknown function code");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Act: FC 0x10 is not in the switch (cases are 0x01/02/03/04),
     * so the switch default: return CACHE_LOOKUP_NOT_FOUND */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(1, 0x10, 0, &val, 0);

    /* Must return NOT_FOUND without crash */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        CACHE_LOOKUP_NOT_FOUND,
        r,
        "lookup with unknown FC 0x10 must return CACHE_LOOKUP_NOT_FOUND"
    );

    /* value_out must not be modified — default switch branch returns before any pool scan */
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xFFFF, val,
        "value_out must not be modified when lookup hits unknown FC (default branch)");
}

/* ---- CM-U-025: pool full — on_response() does not crash ------------------- */

/* Verify that cache_multimaster_on_response() does not crash when the pool
 * is completely full, and that:
 *   - an over-capacity entry is dropped (lookup returns NOT_FOUND)
 *   - existing entries in the full pool are unaffected (lookup returns FOUND) */
void test_cache_multimaster_pool_full_no_crash(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-025: pool full: on_response does not crash");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Fill all 4096 pool slots: slave_id=1, FC03, addresses 0..4095.
     * Each address occupies one unique pool entry.
     * Response payload: 1 register with value 0x0001. */
    uint8_t fill_data[5];
    fill_data[0] = 1;       /* slave_id — not read by on_response(); slave_id is matched via parameter */
    fill_data[1] = 0x03;    /* FC03 */
    fill_data[2] = 2;       /* byte_count (1 reg × 2 bytes) */
    fill_data[3] = 0x00;    /* reg value high byte */
    fill_data[4] = 0x01;    /* reg value low byte: value = 0x0001 */

    for (uint16_t addr = 0; addr < 4096; addr++) {
        cache_multimaster_on_request(0, 1, 3, addr, 1);
        cache_multimaster_on_response(0, 1, 3, fill_data, sizeof(fill_data), 0);
    }

    /* Attempt to add one more entry (slave_id=2, FC03, addr=0 — not in pool).
     * The pool is full, so this entry must be dropped gracefully (no crash). */
    uint8_t extra_data[5];
    extra_data[0] = 2;      /* slave_id=2: different from the fill slave_id */
    extra_data[1] = 0x03;   /* FC03 */
    extra_data[2] = 2;      /* byte_count */
    extra_data[3] = 0x00;
    extra_data[4] = 0x02;   /* value = 0x0002 */

    cache_multimaster_on_request(0, 2, 3, 0, 1);
    cache_multimaster_on_response(0, 2, 3, extra_data, sizeof(extra_data), 0);

    /* Over-capacity entry must be dropped: slave 2 is NOT_FOUND */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(2, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        CACHE_LOOKUP_NOT_FOUND,
        r,
        "Over-capacity entry (slave 2) must be NOT_FOUND when pool is full"
    );

    /* The drop must not be silent: the dropped-values counter must reflect it
     * (mem-exhaust-1). One register could not be stored → counter == 1. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        1,
        cache_multimaster_test_get_entries_dropped(),
        "Pool-full drop must increment entries_dropped counter (not silent)"
    );

    /* Existing entries must be unaffected: slave 1, addr 0 must still be FOUND */
    val = 0;
    r = cache_multimaster_lookup(1, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        CACHE_LOOKUP_FOUND,
        r,
        "Existing entry (slave 1, addr 0) must still be FOUND after pool-full drop"
    );
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0x0001,
        val,
        "Existing entry value must still be 0x0001"
    );
}

/* ---- CM-U-026: on_response FC03 address-zero boundary -------------------- */

/* Verify that address 0 is stored correctly — no off-by-one treating address 0
 * as an "unused slot" marker. */
void test_cache_multimaster_on_response_fc03_address_zero(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-026: on_response FC03 address-zero boundary");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Request: slave 5, FC03, 1 register at address 0 */
    cache_multimaster_on_request(0, 5, 3, 0, 1);

    /* Response: byte_count=2, one register at address 0 with value 0x5A5A */
    uint8_t data[] = {
        5,              /* [0] slave_id */
        0x03,           /* [1] FC */
        2,              /* [2] byte_count (1 reg × 2 bytes) */
        0x5A, 0x5A      /* reg 0: 0x5A5A */
    };
    cache_multimaster_on_response(0, 5, 3, data, sizeof(data), 0);

    /* Lookup at address 0 must return FOUND with value 0x5A5A */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(5, 3, 0, &val, 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup(5, 3, 0) should return FOUND: address 0 must be stored correctly");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x5A5A, val,
        "value at address 0 should be 0x5A5A");
}

/* ---- CM-U-027: on_response FC03 odd byte_count (floor division) ----------- */

/* Verify that max_regs = byte_count / 2 uses integer floor division, so an odd
 * byte_count only yields the floor number of full registers and no partial read. */
void test_cache_multimaster_on_response_fc03_odd_byte_count(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-027: on_response FC03 odd byte_count (floor division)");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Request: slave 6, FC03, 3 registers starting at address 10 */
    cache_multimaster_on_request(0, 6, 3, 10, 3);

    /* byte_count=5 (odd) → max_regs = 5/2 = 2 (floor division).
     * Only reg10 and reg11 should be stored; reg12 must NOT be stored.
     * data_len = 3 + 5 = 8 bytes total. */
    uint8_t data[] = {
        6,              /* [0] slave_id */
        0x03,           /* [1] FC */
        5,              /* [2] byte_count = 5 (odd) */
        0x00, 0xAA,     /* reg10: 0x00AA */
        0x00, 0xBB,     /* reg11: 0x00BB */
        0xCC            /* partial byte — must be ignored due to floor division */
    };
    cache_multimaster_on_response(0, 6, 3, data, sizeof(data), 0);

    uint16_t val = 0;

    /* reg10 must be FOUND with value 0x00AA */
    cache_lookup_result_t r = cache_multimaster_lookup(6, 3, 10, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup(6, 3, 10) should return FOUND");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x00AA, val,
        "register 10 value should be 0x00AA");

    /* reg11 must be FOUND with value 0x00BB */
    val = 0;
    r = cache_multimaster_lookup(6, 3, 11, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup(6, 3, 11) should return FOUND");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x00BB, val,
        "register 11 value should be 0x00BB");

    /* reg12 must NOT be found — floor division means the partial byte is dropped */
    val = 0xFFFF;
    r = cache_multimaster_lookup(6, 3, 12, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND, r,
        "lookup(6, 3, 12) should return NOT_FOUND: max_regs = byte_count/2 = 5/2 = 2");
}

/* ---- CM-U-028: on_response FC01 exactly 8 coils (single full byte) -------- */

/* Verify bit-packing at the byte rollover point when count == 8 (exactly one
 * full byte). Only data[3] must be accessed; no out-of-bounds read. */
void test_cache_multimaster_on_response_fc01_exactly_8_coils(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-028: on_response FC01 exactly 8 coils (single full byte)");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Request: slave 7, FC01, 8 coils starting at address 0 */
    cache_multimaster_on_request(0, 7, 1, 0, 8);

    /* Response: byte_count=1, data=0xF0 (11110000b).
     * Bit layout (LSB-first): coil0=0, coil1=0, coil2=0, coil3=0,
     *                         coil4=1, coil5=1, coil6=1, coil7=1 */
    uint8_t data[] = {
        7,      /* [0] slave_id */
        0x01,   /* [1] FC01 */
        1,      /* [2] byte_count = 1 */
        0xF0    /* coils 0-3 = 0, coils 4-7 = 1 */
    };
    cache_multimaster_on_response(0, 7, 1, data, sizeof(data), 0);

    /* Coils 0-3 must be FOUND with value 0 */
    for (int i = 0; i < 4; i++) {
        uint16_t val = 0xFFFF;
        cache_lookup_result_t r = cache_multimaster_lookup(7, 1, (uint16_t)i, &val, 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
            "coil 0-3 should be FOUND");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, val,
            "coil 0-3 value should be 0 (lower nibble of 0xF0 is 0)");
    }

    /* Coils 4-7 must be FOUND with value 1 */
    for (int i = 4; i < 8; i++) {
        uint16_t val = 0xFFFF;
        cache_lookup_result_t r = cache_multimaster_lookup(7, 1, (uint16_t)i, &val, 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
            "coil 4-7 should be FOUND");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, val,
            "coil 4-7 value should be 1 (upper nibble of 0xF0 is set)");
    }
}

/* ---- CM-U-029: on_response FC02 where byte_count > available data_len ----- */

/* Verify the FC01/FC02 bounds check: when data_len is too short to hold the
 * claimed number of coil bytes, no data is stored and pending is consumed. */
void test_cache_multimaster_on_response_fc02_bounds_check(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-029: on_response FC02 byte_count > available data_len");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Request: slave 8, FC02, 16 discrete inputs starting at address 0 */
    cache_multimaster_on_request(0, 8, 2, 0, 16);

    /* Response claims byte_count=10 but data_len=4 (only header, no coil bytes).
     * With count=16 and byte_count=10: bytes_needed = (16+7)/8 = 2.
     * byte_count(10) >= bytes_needed(2), so no clamping.
     * Bounds check: (3 + 2) = 5 > 4 → early return, nothing stored. */
    uint8_t data[] = {
        8,      /* [0] slave_id */
        0x02,   /* [1] FC02 */
        10,     /* [2] byte_count = 10 (claims 10 bytes of coil data) */
        0xFF    /* only 1 extra byte available — far fewer than claimed */
    };
    cache_multimaster_on_response(0, 8, 2, data, sizeof(data), 0);

    /* Pending must be consumed (set to false) after the call */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_test_get_pending_valid(0),
        "Pending must be consumed after on_response() even on bounds-check early return"
    );

    /* No coil data must have been stored */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(8, 2, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND, r,
        "lookup(8, 2, 0) should return NOT_FOUND after bounds-check early return");
}

/* ---- CM-U-030: on_response FC mismatch (request FC03, response FC04) ------- */

/* Verify that the pending.function check in on_response causes a mismatch when
 * the response FC differs from the pending FC, and nothing is stored. */
void test_cache_multimaster_on_response_fc_mismatch(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-030: on_response FC mismatch (request FC03, response FC04)");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Request for FC03, pending is set for slave 9, FC03 */
    cache_multimaster_on_request(0, 9, 3, 0, 1);

    /* Respond with FC04 — function code mismatch triggers pending mismatch path */
    uint8_t data[] = {
        9,              /* [0] slave_id */
        0x04,           /* [1] FC04 — mismatches pending FC03 */
        2,              /* [2] byte_count */
        0x00, 0x01      /* reg 0: 1 */
    };
    cache_multimaster_on_response(0, 9, 4, data, sizeof(data), 0);

    /* Pending must be consumed (mismatch clears it) */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_test_get_pending_valid(0),
        "Pending must be cleared on FC mismatch"
    );

    /* Nothing must be stored for either FC03 or FC04 */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(9, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND, r,
        "lookup(9, 3, 0) should return NOT_FOUND after FC mismatch");

    val = 0xFFFF;
    r = cache_multimaster_lookup(9, 4, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND, r,
        "lookup(9, 4, 0) should return NOT_FOUND after FC mismatch");
}

/* ---- CM-U-031: lookup with slave_id=0 (broadcast address) ----------------- */

/* Verify that slave_id == 0 is treated as a valid key in find_or_alloc_entry()
 * and cache_multimaster_lookup() — no implicit guard against zero. */
void test_cache_multimaster_lookup_slave_id_zero(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-031: lookup with slave_id=0 (broadcast address)");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store slave_id=0, FC03, address=10, value=0x1111 */
    cache_multimaster_on_request(0, 0, 3, 10, 1);
    uint8_t data[] = {
        0,              /* [0] slave_id = 0 */
        0x03,           /* [1] FC */
        2,              /* [2] byte_count (1 reg × 2 bytes) */
        0x11, 0x11      /* reg 10: 0x1111 */
    };
    cache_multimaster_on_response(0, 0, 3, data, sizeof(data), 0);

    /* Lookup slave_id=0 must return FOUND with the correct value */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(0, 3, 10, &val, 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup(0, 3, 10) should return FOUND: slave_id=0 must be a valid cache key");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x1111, val,
        "value at slave_id=0, FC03, address=10 should be 0x1111");
}

/* ---- CM-U-032: lookup FC03 vs FC04 do not collide (same slave, same address) */

/* Verify that CACHE_TYPE_HOLDING (FC03) and CACHE_TYPE_INPUT (FC04) produce
 * separate cache entries even for the same slave_id and address. */
void test_cache_multimaster_lookup_fc03_vs_fc04_no_collision(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-032: lookup FC03 vs FC04 do not collide (same slave, same address)");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store slave=20, FC03, addr=100, value=0xAAAA */
    cache_multimaster_on_request(0, 20, 3, 100, 1);
    uint8_t data_fc03[] = {
        20,             /* [0] slave_id */
        0x03,           /* [1] FC03 */
        2,              /* [2] byte_count */
        0xAA, 0xAA      /* reg 100: 0xAAAA */
    };
    cache_multimaster_on_response(0, 20, 3, data_fc03, sizeof(data_fc03), 0);

    /* Store slave=20, FC04, addr=100, value=0xBBBB */
    cache_multimaster_on_request(0, 20, 4, 100, 1);
    uint8_t data_fc04[] = {
        20,             /* [0] slave_id */
        0x04,           /* [1] FC04 */
        2,              /* [2] byte_count */
        0xBB, 0xBB      /* reg 100: 0xBBBB */
    };
    cache_multimaster_on_response(0, 20, 4, data_fc04, sizeof(data_fc04), 0);

    /* FC03 lookup must return 0xAAAA */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(20, 3, 100, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup(20, 3, 100) should return FOUND for FC03 (HOLDING type)");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xAAAA, val,
        "FC03 value at address 100 should be 0xAAAA");

    /* FC04 lookup must return 0xBBBB — different cache type, no collision */
    val = 0xFFFF;
    r = cache_multimaster_lookup(20, 4, 100, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup(20, 4, 100) should return FOUND for FC04 (INPUT type)");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xBBBB, val,
        "FC04 value at address 100 should be 0xBBBB");
}

/* ---- CM-U-033: on_request pending overwrite (second request before response) */

/* Verify that a second on_request() on the same port overwrites s_pending[port],
 * preventing stale pending context from matching the wrong response. */
void test_cache_multimaster_on_request_pending_overwrite(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-033: on_request pending overwrite (second request before response)");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* First request: slave 30, FC03, address 0, count 1 — pending set for slave 30 */
    cache_multimaster_on_request(0, 30, 3, 0, 1);

    /* Second request: slave 31, FC03, address 5, count 1 — overwrites pending */
    cache_multimaster_on_request(0, 31, 3, 5, 1);

    /* Deliver response for the second pending (slave 31, FC03, reg5 = 0xCCCC) */
    uint8_t data[] = {
        31,             /* [0] slave_id = 31 (matches current pending) */
        0x03,           /* [1] FC03 */
        2,              /* [2] byte_count (1 reg × 2 bytes) */
        0xCC, 0xCC      /* reg 5: 0xCCCC */
    };
    cache_multimaster_on_response(0, 31, 3, data, sizeof(data), 0);

    /* slave 31, reg5 must be FOUND with value 0xCCCC */
    uint16_t val = 0xFFFF;
    cache_lookup_result_t r = cache_multimaster_lookup(31, 3, 5, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND, r,
        "lookup(31, 3, 5) should return FOUND: second request's response was stored");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xCCCC, val,
        "value at slave 31, FC03, address 5 should be 0xCCCC");

    /* slave 30, reg0 must NOT be found — its pending was overwritten before response */
    val = 0xFFFF;
    r = cache_multimaster_lookup(30, 3, 0, &val, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND, r,
        "lookup(30, 3, 0) should return NOT_FOUND: pending was overwritten before response");
}

/* ---- CM-U-034: disable without prior enable does not crash ---------------- */

/* Verify the NULL guards in disable() prevent a crash when called before any
 * enable() — s_pool is NULL, s_age_task is NULL, nothing to free or delete. */
void test_cache_multimaster_disable_without_enable(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-034: disable without prior enable does not crash");
    LOG_MESSAGE();

    /* Pre-condition: init only (no enable) — s_pool and s_age_task remain NULL */
    cache_multimaster_init();

    /* Act: must not crash with NULL pool and NULL task handle */
    cache_multimaster_disable();

    /* Cache must report disabled */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_is_enabled(),
        "cache_multimaster_is_enabled() should return false after disable() without enable()"
    );

    /* mock free must NOT have been called (no pool to free) */
    verify_malloc_tracking(0, 0);
}

/* ---- CM-U-035: cache_age_task saturation — age_s must not exceed CACHE_AGE_MAX_S -- */

/* Verify that calling cache_multimaster_test_tick_age() when age_s is already at
 * CACHE_AGE_MAX_S (65535) does not increment further (no wrap to 0). */
void test_cache_multimaster_age_saturation(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-035: age saturation — age_s must not exceed CACHE_AGE_MAX_S");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store an entry: slave 50, FC03, address 0, value 0xFFFF */
    cache_multimaster_on_request(0, 50, 3, 0, 1);
    uint8_t data[] = {
        50,             /* [0] slave_id */
        0x03,           /* [1] FC */
        2,              /* [2] byte_count (1 reg x 2 bytes) */
        0xFF, 0xFF      /* reg 0: 0xFFFF */
    };
    cache_multimaster_on_response(0, 50, 3, data, sizeof(data), 0);

    /* Force age_s to CACHE_AGE_MAX_S (65535) — the saturation limit */
    bool set_ok = cache_multimaster_test_set_entry_age(50, 3, 0, 65535);
    TEST_ASSERT_TRUE_MESSAGE(set_ok,
        "cache_multimaster_test_set_entry_age must find the entry");

    /* Verify the entry is stale with timeout=65534 (age 65535 > 65534) */
    uint16_t val = 0;
    cache_lookup_result_t r = cache_multimaster_lookup(50, 3, 0, &val, 65534);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_STALE, r,
        "Entry with age_s=65535 and timeout=65534 must be STALE");

    /* Perform one aging tick — age_s is already at CACHE_AGE_MAX_S, must not increment.
     * Use timeout=65534: if saturation works (age_s stayed 65535): 65535>65534 → STALE.
     * If wrap-around occurred (age_s became 0): 0>65534 → FOUND. Only STALE is correct. */
    cache_multimaster_test_tick_age();

    val = 0;
    r = cache_multimaster_lookup(50, 3, 0, &val, 65534);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_STALE, r,
        "After tick at saturation, age_s must still be 65535 → STALE with timeout=65534 (not wrapped to 0)");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xFFFF, val,
        "Value must be intact after saturation tick");

    /* Definitive saturation check: perform another tick, age_s must remain 65535.
     * Verify by checking that with timeout=65534 result is still STALE (age > timeout),
     * not FOUND (which would mean age was reset to 0 by wrap-around). */
    cache_multimaster_test_tick_age();
    val = 0;
    r = cache_multimaster_lookup(50, 3, 0, &val, 65534);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_STALE, r,
        "After second tick at saturation, age_s must still be 65535 — NOT wrapped to 0");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xFFFF, val,
        "Value must remain 0xFFFF — saturation did not corrupt the entry");

    /* Verify no extra allocations were made and mutex is balanced */
    verify_malloc_tracking(1, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        mock_xSemaphoreGive_called,
        mock_xSemaphoreTake_called,
        "Mutex must be balanced after CM-U-035 — tick_age and lookup calls are symmetric");
}

/* ---- CM-U-036: cache_status_handler — disabled, empty pool --------------- */

/* Verify that the status handler with an uninitialised cache (s_pool=NULL,
 * s_cache_enabled=false) returns a valid JSON with "enabled":false and
 * all counters set to zero. */
void test_cache_status_handler_disabled_empty(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-036: cache_status_handler disabled empty pool");
    LOG_MESSAGE();

    /* setUp already called cache_multimaster_test_reset() — no init, no enable */
    httpd_req_t req = {0};

    /* Act */
    esp_err_t ret = cache_multimaster_test_status_handler(&req);

    /* Must succeed */
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, ret,
        "cache_status_handler must return ESP_OK");

    /* Handler must call httpd_resp_send exactly once (non-chunked response) */
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_http_resp_send_called,
        "httpd_resp_send must be called exactly once");

    /* JSON must contain the expected fields */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"enabled\":false"),
        "Response must contain \"enabled\":false");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"entries\":0"),
        "Response must contain \"entries\":0");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"max_entries\":4096"),
        "Response must contain \"max_entries\":4096");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"slaves\":0"),
        "Response must contain \"slaves\":0");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"packets_processed\":0"),
        "Response must contain \"packets_processed\":0");
}

/* ---- CM-U-037: cache_status_handler — enabled, 2 entries, 2 slaves, 2 packets */

/* Verify that the status handler correctly reflects 2 entries belonging to
 * 2 distinct slaves after 2 packets are stored. */
void test_cache_status_handler_enabled_two_entries(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-037: cache_status_handler enabled 2 entries 2 slaves");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store: slave=1, FC03, addr=0, val=0x1111 */
    cache_multimaster_on_request(0, 1, 3, 0, 1);
    uint8_t data1[] = { 1, 0x03, 2, 0x11, 0x11 };
    cache_multimaster_on_response(0, 1, 3, data1, sizeof(data1), 1000000);

    /* Store: slave=2, FC03, addr=0, val=0x2222 */
    cache_multimaster_on_request(0, 2, 3, 0, 1);
    uint8_t data2[] = { 2, 0x03, 2, 0x22, 0x22 };
    cache_multimaster_on_response(0, 2, 3, data2, sizeof(data2), 2000000);

    httpd_req_t req = {0};
    esp_err_t ret = cache_multimaster_test_status_handler(&req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, ret,
        "cache_status_handler must return ESP_OK");

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"enabled\":true"),
        "Response must contain \"enabled\":true");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"entries\":2"),
        "Response must contain \"entries\":2");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"slaves\":2"),
        "Response must contain \"slaves\":2");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"packets_processed\":2"),
        "Response must contain \"packets_processed\":2");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"max_entries\":4096"),
        "Response must contain \"max_entries\":4096");
    /* 2 entries × 8 bytes per cache_entry_t */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"memory_bytes\":16"),
        "Response must contain \"memory_bytes\":16");
}

/* ---- CM-U-038: cache_status_handler — 2 entries same slave (slaves == 1) -- */

/* Verify that two entries belonging to the same slave are counted as one
 * unique slave in the status response. */
void test_cache_status_handler_same_slave(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-038: cache_status_handler 2 entries same slave");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store: slave=5, FC03, addr=0, val=0xAAAA */
    cache_multimaster_on_request(0, 5, 3, 0, 1);
    uint8_t data1[] = { 5, 0x03, 2, 0xAA, 0xAA };
    cache_multimaster_on_response(0, 5, 3, data1, sizeof(data1), 0);

    /* Store: slave=5, FC03, addr=1, val=0xBBBB (same slave, different address) */
    cache_multimaster_on_request(0, 5, 3, 1, 1);
    uint8_t data2[] = { 5, 0x03, 2, 0xBB, 0xBB };
    cache_multimaster_on_response(0, 5, 3, data2, sizeof(data2), 0);

    httpd_req_t req = {0};
    cache_multimaster_test_status_handler(&req);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"entries\":2"),
        "Response must contain \"entries\":2");
    /* Both entries belong to slave 5 — unique slave count must be 1 */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"slaves\":1"),
        "Response must contain \"slaves\":1 (unique slave count uses bitmask)");
}

/* ---- CM-U-039: cache_csv_handler — empty pool (init only, no enable) ------ */

/* Verify that the CSV handler sends only the header line when the pool is
 * NULL (cache initialised but not enabled). */
void test_cache_csv_handler_empty_pool(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-039: cache_csv_handler empty pool (init only)");
    LOG_MESSAGE();

    /* Pre-condition: init only — mutex is valid but pool remains NULL */
    cache_multimaster_init();

    httpd_req_t req = {0};
    esp_err_t ret = cache_multimaster_test_csv_handler(&req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, ret,
        "cache_csv_handler must return ESP_OK when pool is NULL");

    /* The CSV header must be present */
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr(mock_http_resp_buf, "slave_id,type,address,value,age_s\r\n"),
        "CSV response must start with the header line");

    /* Accumulated buffer must not contain any data rows (no comma-separated numeric lines) */
    const char *after_header = strstr(mock_http_resp_buf, "\r\n");
    if (after_header != NULL) {
        after_header += 2; /* skip the \r\n of the header line */
    }
    /* after the header there should be nothing (empty pool) */
    TEST_ASSERT_TRUE_MESSAGE(
        (after_header == NULL || *after_header == '\0'),
        "CSV response must contain only the header — no data rows for empty pool");
}

/* ---- CM-U-040: cache_csv_handler — 2 entries, correct CSV format ---------- */

/* Verify that the CSV handler emits one correct data row per cached entry. */
void test_cache_csv_handler_two_entries(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-040: cache_csv_handler 2 entries correct format");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store: slave=10, FC03 (holding), addr=100, val=0x0042 (decimal 66) */
    cache_multimaster_on_request(0, 10, 3, 100, 1);
    uint8_t data1[] = { 10, 0x03, 2, 0x00, 0x42 };
    cache_multimaster_on_response(0, 10, 3, data1, sizeof(data1), 0);

    /* Store: slave=20, FC01 (coil), addr=0, val=1 */
    cache_multimaster_on_request(0, 20, 1, 0, 1);
    uint8_t data2[] = { 20, 0x01, 1, 0x01 }; /* 1 coil byte: bit0=1 */
    cache_multimaster_on_response(0, 20, 1, data2, sizeof(data2), 0);

    httpd_req_t req = {0};
    esp_err_t ret = cache_multimaster_test_csv_handler(&req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, ret,
        "cache_csv_handler must return ESP_OK");

    /* Header must be present */
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr(mock_http_resp_buf, "slave_id,type,address,value,age_s\r\n"),
        "CSV response must contain the header line");

    /* Holding register row: slave=10, type=holding, addr=100, val=66, age=0 */
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr(mock_http_resp_buf, "10,holding,100,66,"),
        "CSV must contain holding register row '10,holding,100,66,'");

    /* Coil row: slave=20, type=coil, addr=0, val=1, age=0 */
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr(mock_http_resp_buf, "20,coil,0,1,"),
        "CSV must contain coil row '20,coil,0,1,'");
}

/* ---- CM-U-041: cache_csv_handler — type string mapping -------------------- */

/* Verify that each Modbus function code maps to the correct type string
 * in the CSV output. */
void test_cache_csv_handler_type_mapping(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-041: cache_csv_handler type string mapping");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* FC03 → "holding" */
    cache_multimaster_on_request(0, 1, 3, 0, 1);
    uint8_t d03[] = { 1, 0x03, 2, 0x00, 0x01 };
    cache_multimaster_on_response(0, 1, 3, d03, sizeof(d03), 0);

    /* FC04 → "input" */
    cache_multimaster_on_request(0, 2, 4, 0, 1);
    uint8_t d04[] = { 2, 0x04, 2, 0x00, 0x02 };
    cache_multimaster_on_response(0, 2, 4, d04, sizeof(d04), 0);

    /* FC01 → "coil" */
    cache_multimaster_on_request(0, 3, 1, 0, 1);
    uint8_t d01[] = { 3, 0x01, 1, 0x01 };
    cache_multimaster_on_response(0, 3, 1, d01, sizeof(d01), 0);

    /* FC02 → "discrete" */
    cache_multimaster_on_request(0, 4, 2, 0, 1);
    uint8_t d02[] = { 4, 0x02, 1, 0x01 };
    cache_multimaster_on_response(0, 4, 2, d02, sizeof(d02), 0);

    httpd_req_t req = {0};
    cache_multimaster_test_csv_handler(&req);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "holding"),
        "CSV must contain type string 'holding' for FC03");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "input"),
        "CSV must contain type string 'input' for FC04");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "coil"),
        "CSV must contain type string 'coil' for FC01");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "discrete"),
        "CSV must contain type string 'discrete' for FC02");
}

/* ---- CM-U-042: cache_json_handler — null mutex returns `{"d":[]}` --------- */

/* Verify that the JSON handler returns a minimal empty-array JSON object when
 * the cache has never been initialised (s_cache_mutex == NULL). */
void test_cache_json_handler_null_mutex(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-042: cache_json_handler null mutex returns {\"d\":[]}");
    LOG_MESSAGE();

    /* setUp already called cache_multimaster_test_reset() — mutex is NULL, no init */
    httpd_req_t req = {0};

    esp_err_t ret = cache_multimaster_test_json_handler(&req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, ret,
        "cache_json_handler must return ESP_OK even with NULL mutex");

    /* Non-chunked send must be used when the mutex is NULL (single httpd_resp_send call) */
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_http_resp_send_called,
        "httpd_resp_send must be called exactly once for the null-mutex path");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("{\"d\":[]}",
        mock_http_resp_buf,
        "Response must be exactly {\"d\":[]} when mutex is NULL");
}

/* ---- CM-U-043: cache_json_handler — empty pool returns `{"d":[]}` --------- */

/* Verify that the JSON handler produces an empty data array when the cache
 * is initialised but the pool is NULL (not enabled). */
void test_cache_json_handler_empty_pool(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-043: cache_json_handler empty pool returns {\"d\":[]}");
    LOG_MESSAGE();

    /* Pre-condition: init only — mutex valid, pool == NULL */
    cache_multimaster_init();

    httpd_req_t req = {0};
    cache_multimaster_test_json_handler(&req);

    TEST_ASSERT_EQUAL_STRING_MESSAGE("{\"d\":[]}",
        mock_http_resp_buf,
        "Response must be exactly {\"d\":[]} when pool is NULL");
}

/* ---- CM-U-044: cache_json_handler — 1 entry, correct JSON format ---------- */

/* Verify that a single cached entry is serialised to the correct JSON object
 * with the expected field names and values. */
void test_cache_json_handler_one_entry(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-044: cache_json_handler 1 entry correct JSON");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store: slave=7, FC03, addr=50, val=0x1234 (decimal 4660) */
    cache_multimaster_on_request(0, 7, 3, 50, 1);
    uint8_t data[] = { 7, 0x03, 2, 0x12, 0x34 };
    /* on_response() resets age_s to 0 for new entries — no timer manipulation needed */
    cache_multimaster_on_response(0, 7, 3, data, sizeof(data), 0);

    httpd_req_t req = {0};
    cache_multimaster_test_json_handler(&req);

    /* The data array must be opened */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "{\"d\":["),
        "Response must start with {\"d\":[");

    /* The entry must be serialised with the correct field values */
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr(mock_http_resp_buf, "{\"s\":7,\"t\":\"h\",\"a\":50,\"v\":4660,\"age\":0}"),
        "Response must contain {\"s\":7,\"t\":\"h\",\"a\":50,\"v\":4660,\"age\":0}");

    /* The data array must be closed */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "]}"),
        "Response must end with ]}");
}

/* ---- CM-U-045: cache_json_handler — type chars (h/i/c/d) ------------------ */

/* Verify that each Modbus function code maps to the correct single-char type
 * tag in the JSON output ("h", "i", "c", "d"). */
void test_cache_json_handler_type_chars(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-045: cache_json_handler type chars h/i/c/d");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* FC03 → "h" */
    cache_multimaster_on_request(0, 1, 3, 0, 1);
    uint8_t d03[] = { 1, 0x03, 2, 0x00, 0x01 };
    cache_multimaster_on_response(0, 1, 3, d03, sizeof(d03), 0);

    /* FC04 → "i" */
    cache_multimaster_on_request(0, 2, 4, 0, 1);
    uint8_t d04[] = { 2, 0x04, 2, 0x00, 0x02 };
    cache_multimaster_on_response(0, 2, 4, d04, sizeof(d04), 0);

    /* FC01 → "c" */
    cache_multimaster_on_request(0, 3, 1, 0, 1);
    uint8_t d01[] = { 3, 0x01, 1, 0x01 };
    cache_multimaster_on_response(0, 3, 1, d01, sizeof(d01), 0);

    /* FC02 → "d" */
    cache_multimaster_on_request(0, 4, 2, 0, 1);
    uint8_t d02[] = { 4, 0x02, 1, 0x01 };
    cache_multimaster_on_response(0, 4, 2, d02, sizeof(d02), 0);

    httpd_req_t req = {0};
    cache_multimaster_test_json_handler(&req);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"t\":\"h\""),
        "JSON must contain \"t\":\"h\" for FC03 (holding)");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"t\":\"i\""),
        "JSON must contain \"t\":\"i\" for FC04 (input)");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"t\":\"c\""),
        "JSON must contain \"t\":\"c\" for FC01 (coil)");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"t\":\"d\""),
        "JSON must contain \"t\":\"d\" for FC02 (discrete)");
}

/* ---- CM-U-046: cache_json_handler — 2 entries, comma separator ------------ */

/* Verify that two entries in the JSON response are separated by a comma and
 * that the first entry is not preceded by a comma. */
void test_cache_json_handler_two_entries_comma(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-046: cache_json_handler 2 entries comma separator");
    LOG_MESSAGE();

    /* Pre-condition: init + enable */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store 2 entries for different slaves */
    cache_multimaster_on_request(0, 10, 3, 0, 1);
    uint8_t data1[] = { 10, 0x03, 2, 0x00, 0x0A };
    cache_multimaster_on_response(0, 10, 3, data1, sizeof(data1), 0);

    cache_multimaster_on_request(0, 20, 3, 0, 1);
    uint8_t data2[] = { 20, 0x03, 2, 0x00, 0x14 };
    cache_multimaster_on_response(0, 20, 3, data2, sizeof(data2), 0);

    httpd_req_t req = {0};
    cache_multimaster_test_json_handler(&req);

    /* There must be a comma between the two JSON objects */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "},{"),
        "JSON must contain '},{' as separator between 2 objects");

    /* The response must NOT start with a leading comma before the first object
     * (i.e., the first object must not be preceded by a comma). */
    const char *d_array_start = strstr(mock_http_resp_buf, "[");
    if (d_array_start != NULL) {
        /* The character right after '[' must be '{' not ',' */
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('{', *(d_array_start + 1),
            "First element must not be preceded by a leading comma");
    }
}

/* ---- CM-U-050: get_stats — NULL out pointer, no crash, no lock ----------- */

/* Verify that cache_multimaster_get_stats(NULL):
 *   - does not crash
 *   - returns BEFORE taking the cache mutex (the `if (out == NULL) return;`
 *     guard is the very first statement, ahead of the memset and the lock).
 * The mutex take-counter must stay at 0: a regression that moved the NULL
 * check after the lock would take (and on a real target, leak) the mutex. */
void test_cache_multimaster_get_stats_null_out(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-050: get_stats NULL out — early return before lock");
    LOG_MESSAGE();

    /* Pre-condition: a fully initialised+enabled module so a NULL-check
     * regression really would reach (and take) the mutex. */
    cache_multimaster_init();
    cache_multimaster_enable();

    int take_before = mock_xSemaphoreTake_called;

    /* Act: must not crash with a NULL output pointer */
    cache_multimaster_get_stats(NULL);

    /* The mutex must NOT have been taken: the NULL guard returns first */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        take_before,
        mock_xSemaphoreTake_called,
        "get_stats(NULL) must return before taking the cache mutex"
    );
}

/* ---- CM-U-051: get_stats — module not initialised, zeroes output --------- */

/* Verify that cache_multimaster_get_stats() with an uninitialised module
 * (s_cache_mutex == NULL, s_pool == NULL after setUp's reset):
 *   - zeroes all four output fields (proves the memset path runs), and
 *   - does NOT take the cache mutex (NULL-mutex guard returns first).
 * The output struct is pre-filled with a 0xAA sentinel so that "all zero"
 * is a real observation, not the default. */
void test_cache_multimaster_get_stats_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-051: get_stats not initialised — zeroes output, no lock");
    LOG_MESSAGE();

    /* setUp() called cache_multimaster_test_reset(): mutex and pool are NULL.
     * Do NOT call init()/enable() here. */

    cache_multimaster_stats_t stats;
    memset(&stats, 0xAA, sizeof(stats)); /* non-zero sentinel */

    int take_before = mock_xSemaphoreTake_called;

    /* Act */
    cache_multimaster_get_stats(&stats);

    /* All four fields must be zeroed by the memset path */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats.packets_processed,
        "packets_processed must be 0 when module is not initialised");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats.last_packet_age_s,
        "last_packet_age_s must be 0 when module is not initialised");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats.map_age_s,
        "map_age_s must be 0 when module is not initialised");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, stats.devices_on_bus,
        "devices_on_bus must be 0 when module is not initialised");

    /* NULL-mutex guard returns before the lock */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        take_before,
        mock_xSemaphoreTake_called,
        "get_stats must not take the mutex when s_cache_mutex is NULL"
    );
}

/* ---- CM-U-052: get_stats — enabled, empty pool --------------------------- */

/* Verify that cache_multimaster_get_stats() right after init()+enable() with
 * the clock held fixed reports a fully idle state: no devices, no packets,
 * and both ages 0 (reading at the exact clock value enable() used for the
 * reset timestamp, so now - reset_us == 0). */
void test_cache_multimaster_get_stats_enabled_empty(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-052: get_stats enabled empty pool — all zero");
    LOG_MESSAGE();

    /* Fix the clock so enable() records s_reset_us == this value */
    mock_esp_timer_get_time_value = 7000000; /* 7 s */

    cache_multimaster_init();
    cache_multimaster_enable();

    /* Read at the same clock value: now - reset_us == 0 */
    cache_multimaster_stats_t stats;
    memset(&stats, 0xAA, sizeof(stats));
    cache_multimaster_get_stats(&stats);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, stats.devices_on_bus,
        "Empty pool must report devices_on_bus == 0");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats.packets_processed,
        "No responses stored: packets_processed == 0");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats.last_packet_age_s,
        "No packet seen: last_packet_age_s == 0");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats.map_age_s,
        "Read at the enable() clock value: map_age_s == 0");
}

/* ---- CM-U-053: get_stats — unique slave-id counting ---------------------- */

/* Verify devices_on_bus counts DISTINCT slave_ids, not used pool entries.
 * Seed:
 *   - slave 200 at two different addresses (two used entries, ONE id) — this
 *     duplicate is the load-bearing case: a "count used entries" bug would
 *     report >=4 instead of 3,
 *   - slave 0   (low bitmap edge: seen[0] bit 0),
 *   - slave 255 (high bitmap edge: seen[31] bit 7).
 * Four used entries, three distinct ids → devices_on_bus must be 3. */
void test_cache_multimaster_get_stats_unique_slaves(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-053: get_stats unique slave-id counting (dup id, bitmap edges)");
    LOG_MESSAGE();

    cache_multimaster_init();
    cache_multimaster_enable();

    /* slave 200, FC03, address 10 */
    cache_multimaster_on_request(0, 200, 3, 10, 1);
    uint8_t d1[] = { 200, 0x03, 2, 0x00, 0x01 };
    cache_multimaster_on_response(0, 200, 3, d1, sizeof(d1), 1000000);

    /* slave 200 again, FC03, DIFFERENT address 20 — second used entry, same id */
    cache_multimaster_on_request(0, 200, 3, 20, 1);
    uint8_t d2[] = { 200, 0x03, 2, 0x00, 0x02 };
    cache_multimaster_on_response(0, 200, 3, d2, sizeof(d2), 1000000);

    /* slave 0 (low bitmap edge), FC03, address 0 */
    cache_multimaster_on_request(0, 0, 3, 0, 1);
    uint8_t d3[] = { 0, 0x03, 2, 0x00, 0x03 };
    cache_multimaster_on_response(0, 0, 3, d3, sizeof(d3), 1000000);

    /* slave 255 (high bitmap edge: seen[31] bit 7), FC03, address 0 */
    cache_multimaster_on_request(0, 255, 3, 0, 1);
    uint8_t d4[] = { 255, 0x03, 2, 0x00, 0x04 };
    cache_multimaster_on_response(0, 255, 3, d4, sizeof(d4), 1000000);

    /* Sanity: confirm all four entries are really stored (4 used pool slots) */
    uint16_t v = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND,
        cache_multimaster_lookup(200, 3, 10, &v, 0), "slave 200 @10 must be stored");
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND,
        cache_multimaster_lookup(200, 3, 20, &v, 0), "slave 200 @20 must be stored");
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND,
        cache_multimaster_lookup(0, 3, 0, &v, 0), "slave 0 @0 must be stored");
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND,
        cache_multimaster_lookup(255, 3, 0, &v, 0), "slave 255 @0 must be stored");

    cache_multimaster_stats_t stats;
    memset(&stats, 0xAA, sizeof(stats));
    cache_multimaster_get_stats(&stats);

    /* 4 used entries but only 3 distinct ids (200, 0, 255) */
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, stats.devices_on_bus,
        "devices_on_bus must count DISTINCT slave_ids (3), not used entries (4)");

    /* Four successful on_response calls → four stored packets */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, stats.packets_processed,
        "packets_processed must equal the number of successful on_response calls (4)");
}

/* ---- CM-U-054: get_stats — exact age math -------------------------------- */

/* Verify the age arithmetic with values chosen so that the two ages are
 * DIFFERENT integers and neither equals NOW/1e6:
 *   - enable() at T0 = 1_000_000   → s_reset_us = 1_000_000
 *   - on_response timestamp TP = 5_500_000 → s_last_packet_us = 5_500_000
 *   - read at NOW = 12_750_000
 *     map_age_s         = (12_750_000 - 1_000_000) / 1e6 = 11
 *     last_packet_age_s = (12_750_000 - 5_500_000) / 1e6 = 7
 *     NOW/1e6 = 12 (distinct from both)
 * Catches a wrong divisor, swapped fields, wrong subtraction order, or a
 * truncation mistake. */
void test_cache_multimaster_get_stats_age_math(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-054: get_stats exact age math");
    LOG_MESSAGE();

    /* enable() at T0 sets s_reset_us = T0 */
    mock_esp_timer_get_time_value = 1000000; /* T0 = 1 s */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* One response with explicit capture timestamp TP = 5.5 s */
    cache_multimaster_on_request(0, 17, 3, 0, 1);
    uint8_t d[] = { 17, 0x03, 2, 0x12, 0x34 };
    cache_multimaster_on_response(0, 17, 3, d, sizeof(d), 5500000); /* TP */

    /* Read the stats at NOW = 12.75 s */
    mock_esp_timer_get_time_value = 12750000; /* NOW */

    cache_multimaster_stats_t stats;
    memset(&stats, 0xAA, sizeof(stats));
    cache_multimaster_get_stats(&stats);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(11, stats.map_age_s,
        "map_age_s = (NOW - reset_us)/1e6 = (12.75-1.0)s = 11");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(7, stats.last_packet_age_s,
        "last_packet_age_s = (NOW - last_pkt_us)/1e6 = (12.75-5.5)s = 7");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stats.packets_processed,
        "packets_processed must equal the single on_response call");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, stats.devices_on_bus,
        "one distinct slave_id stored → devices_on_bus == 1");
}

/* ---- CM-U-055: get_stats — age guard branches ---------------------------- */

/* Exercise both age guards (timestamp > 0 && now >= timestamp):
 *  (a) now < last-packet timestamp → last_packet_age_s clamps to 0, while a
 *      valid map_age_s is still computed in the same call (now >= reset_us).
 *  (b) a fresh enable() with NO response leaves s_last_packet_us == 0, so even
 *      with a large NOW the `timestamp > 0` guard forces last_packet_age_s == 0
 *      WHILE map_age_s != 0 (reset_us > 0) — proving the two guards act
 *      independently within a single get_stats() call. */
void test_cache_multimaster_get_stats_age_guards(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-055: get_stats age guard branches");
    LOG_MESSAGE();

    /* --- Sub-test (a): now < last_pkt_us → last_packet_age_s == 0 --- */
    mock_esp_timer_get_time_value = 1000000; /* enable at T0 = 1 s → reset_us = 1 s */
    cache_multimaster_init();
    cache_multimaster_enable();

    /* Store a response whose capture timestamp is in the "future" (8 s) */
    cache_multimaster_on_request(0, 9, 3, 0, 1);
    uint8_t d[] = { 9, 0x03, 2, 0x00, 0x07 };
    cache_multimaster_on_response(0, 9, 3, d, sizeof(d), 8000000); /* last_pkt_us = 8 s */

    /* Read at NOW = 3 s: now (3) < last_pkt_us (8) → last_packet_age_s guard fires.
     * map: now (3) >= reset_us (1) → map_age_s = (3-1)/1e6 = 2. */
    mock_esp_timer_get_time_value = 3000000;
    cache_multimaster_stats_t a;
    memset(&a, 0xAA, sizeof(a));
    cache_multimaster_get_stats(&a);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, a.last_packet_age_s,
        "now < last_pkt_us must clamp last_packet_age_s to 0 (now >= timestamp guard)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, a.map_age_s,
        "map_age_s must still compute normally: (3-1)s == 2");

    /* --- Sub-test (b): fresh enable, no response, last_pkt_us == 0 --- */
    cache_multimaster_test_reset();
    mock_freertos_semaphore_reset();
    mock_freertos_task_reset();
    reset_malloc_tracking();

    mock_esp_timer_get_time_value = 1000000; /* enable at T0 = 1 s → reset_us = 1 s */
    cache_multimaster_init();
    cache_multimaster_enable();
    /* No on_response → s_last_packet_us stays 0 */

    /* Large NOW = 20 s: map_age_s = (20-1) = 19 (reset_us > 0),
     * last_packet_age_s == 0 because the `timestamp > 0` guard rejects the
     * zero last_pkt_us — both guards evaluated in the SAME call. */
    mock_esp_timer_get_time_value = 20000000;
    cache_multimaster_stats_t b;
    memset(&b, 0xAA, sizeof(b));
    cache_multimaster_get_stats(&b);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, b.last_packet_age_s,
        "last_pkt_us == 0 must keep last_packet_age_s at 0 (timestamp > 0 guard)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(19, b.map_age_s,
        "map_age_s must be non-zero in the same call: (20-1)s == 19 (guards independent)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, b.packets_processed,
        "no responses stored → packets_processed == 0");
}

/* ---- CM-U-056: FC03 address wrap must not poison low addresses ----------- */

/* Regression for cache-lookup-1 / corr-3: a response whose
 * (start_addr + count) crosses the 16-bit address boundary must NOT write
 * wrapped-around low addresses. on_request(0,5,3,0xFFFE,4) with a 4-register
 * response would, without a wrap guard, compute addr = 0xFFFE, 0xFFFF,
 * 0x0000, 0x0001 — the last two poisoning unrelated low registers of the
 * same slave. Only the two in-range addresses (0xFFFE, 0xFFFF) may be stored. */
void test_cache_multimaster_on_response_fc03_addr_wrap_no_poison(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-056: FC03 address wrap must not poison low addresses");
    LOG_MESSAGE();

    cache_multimaster_init();
    cache_multimaster_enable();

    cache_multimaster_on_request(0, 5, 3, 0xFFFE, 4);
    uint8_t data[] = {
        5, 0x03, 8,
        0xAA, 0xAA,   /* addr 0xFFFE */
        0xBB, 0xBB,   /* addr 0xFFFF */
        0xCC, 0xCC,   /* would wrap to addr 0x0000 */
        0xDD, 0xDD    /* would wrap to addr 0x0001 */
    };
    cache_multimaster_on_response(0, 5, 3, data, sizeof(data), 0);

    uint16_t val = 0x1234;
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND,
        cache_multimaster_lookup(5, 3, 0xFFFE, &val, 0),
        "in-range addr 0xFFFE must be stored");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xAAAA, val, "addr 0xFFFE value");
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND,
        cache_multimaster_lookup(5, 3, 0xFFFF, &val, 0),
        "in-range addr 0xFFFF must be stored");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xBBBB, val, "addr 0xFFFF value");

    val = 0x1234;
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND,
        cache_multimaster_lookup(5, 3, 0x0000, &val, 0),
        "addr 0x0000 must NOT be poisoned by 16-bit wrap");
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND,
        cache_multimaster_lookup(5, 3, 0x0001, &val, 0),
        "addr 0x0001 must NOT be poisoned by 16-bit wrap");
}

/* ---- CM-U-057: FC01 coil address wrap must not poison low coils ---------- */

/* Coil-branch counterpart of CM-U-056. on_request(0,6,1,0xFFFF,4) with a
 * 1-byte coil response would compute addr = 0xFFFF, 0x0000, 0x0001, 0x0002;
 * only 0xFFFF is in range. The low coils must not be written. */
void test_cache_multimaster_on_response_fc01_addr_wrap_no_poison(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-057: FC01 coil address wrap must not poison low coils");
    LOG_MESSAGE();

    cache_multimaster_init();
    cache_multimaster_enable();

    cache_multimaster_on_request(0, 6, 1, 0xFFFF, 4);
    uint8_t data[] = {
        6, 0x01, 1,
        0x0F          /* coils: bit0..bit3 set */
    };
    cache_multimaster_on_response(0, 6, 1, data, sizeof(data), 0);

    uint16_t val = 0x1234;
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND,
        cache_multimaster_lookup(6, 1, 0xFFFF, &val, 0),
        "in-range coil 0xFFFF must be stored");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, val, "coil 0xFFFF value (bit0 set)");

    val = 0x1234;
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND,
        cache_multimaster_lookup(6, 1, 0x0000, &val, 0),
        "coil 0x0000 must NOT be poisoned by 16-bit wrap");
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND,
        cache_multimaster_lookup(6, 1, 0x0001, &val, 0),
        "coil 0x0001 must NOT be poisoned by 16-bit wrap");
}

/* ---- CM-U-058: exact 16-bit boundary must NOT be clamped ----------------- */

/* Strict ">" boundary check: a response that ends exactly at 0xFFFF
 * (start 0xFFFC + 4 regs == 0x10000) must store all four registers and must
 * NOT trigger the wrap clamp. Likewise start 0xFFFF count 1 stores 0xFFFF. */
void test_cache_multimaster_on_response_addr_wrap_exact_boundary(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-058: exact 16-bit boundary not clamped");
    LOG_MESSAGE();

    cache_multimaster_init();
    cache_multimaster_enable();

    /* start 0xFFFC, count 4 -> addresses 0xFFFC..0xFFFF, sum == 0x10000 (allowed) */
    cache_multimaster_on_request(0, 9, 3, 0xFFFC, 4);
    uint8_t data[] = {
        9, 0x03, 8,
        0x00, 0x11,   /* 0xFFFC */
        0x00, 0x22,   /* 0xFFFD */
        0x00, 0x33,   /* 0xFFFE */
        0x00, 0x44    /* 0xFFFF */
    };
    cache_multimaster_on_response(0, 9, 3, data, sizeof(data), 0);

    uint16_t val = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND,
        cache_multimaster_lookup(9, 3, 0xFFFC, &val, 0), "0xFFFC stored");
    TEST_ASSERT_EQUAL_UINT16(0x0011, val);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_FOUND,
        cache_multimaster_lookup(9, 3, 0xFFFF, &val, 0), "0xFFFF stored (exact boundary, no clamp)");
    TEST_ASSERT_EQUAL_UINT16(0x0044, val);
    /* and no low-address poisoning */
    TEST_ASSERT_EQUAL_INT_MESSAGE(CACHE_LOOKUP_NOT_FOUND,
        cache_multimaster_lookup(9, 3, 0x0000, &val, 0), "0x0000 untouched at exact boundary");
}

/* ---- CM-U-059: pool-full drop is observable (counter + status JSON) ------ */

/* mem-exhaust-1: once the 4096-entry pool is full, further unique tuples are
 * dropped. This must not be silent — the entries_dropped counter must count
 * every dropped value and /cache/status must expose it. Here a single
 * multi-register response that does not fit at all bumps the counter by the
 * full register count. */
void test_cache_multimaster_pool_full_drop_counter_and_status(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CM-U-059: pool-full drop counter + status JSON");
    LOG_MESSAGE();

    cache_multimaster_init();
    cache_multimaster_enable();

    /* Fill all 4096 slots (slave 1, FC03, addr 0..4095). */
    uint8_t fill[5] = { 1, 0x03, 2, 0x00, 0x01 };
    for (uint16_t addr = 0; addr < 4096; addr++) {
        cache_multimaster_on_request(0, 1, 3, addr, 1);
        cache_multimaster_on_response(0, 1, 3, fill, sizeof(fill), 0);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, cache_multimaster_test_get_entries_dropped(),
        "No drops while the pool still had room");

    /* New slave 5, 3 registers — none fit: counter must rise by exactly 3. */
    cache_multimaster_on_request(0, 5, 3, 0, 3);
    uint8_t resp[] = { 5, 0x03, 6, 0x00, 0x0A, 0x00, 0x0B, 0x00, 0x0C };
    cache_multimaster_on_response(0, 5, 3, resp, sizeof(resp), 0);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, cache_multimaster_test_get_entries_dropped(),
        "All 3 unfit registers must be counted as dropped");

    /* And the condition must surface in /cache/status JSON. */
    httpd_req_t req = {0};
    TEST_ASSERT_EQUAL_INT(ESP_OK, cache_multimaster_test_status_handler(&req));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"entries_dropped\":3"),
        "status JSON must report entries_dropped:3");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_http_resp_buf, "\"entries\":4096"),
        "status JSON must still report a full pool of 4096 entries");
}

/* ---- main ---------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_cache_multimaster_init_happy_path);
    RUN_TEST(test_cache_multimaster_init_oom);
    RUN_TEST(test_cache_multimaster_enable_happy_path);
    RUN_TEST(test_cache_multimaster_enable_oom);
    RUN_TEST(test_cache_multimaster_enable_twice);
    RUN_TEST(test_cache_multimaster_disable);
    RUN_TEST(test_cache_multimaster_clear_null_mutex);
    RUN_TEST(test_cache_multimaster_clear_with_pool);
    RUN_TEST(test_cache_multimaster_on_request_oob_port);
    RUN_TEST(test_cache_multimaster_on_response_oob_port_boundary);
    RUN_TEST(test_cache_multimaster_on_response_min_len_boundary);
    RUN_TEST(test_cache_multimaster_on_request_valid);
    RUN_TEST(test_cache_multimaster_on_response_no_pending);
    RUN_TEST(test_cache_multimaster_on_response_fc03_correct);
    RUN_TEST(test_cache_multimaster_on_response_truncated_fc03);
    RUN_TEST(test_cache_multimaster_on_response_malformed_length);
    RUN_TEST(test_cache_multimaster_on_response_fc01_9_coils);
    RUN_TEST(test_cache_multimaster_on_response_fc01_count_overflow);
    RUN_TEST(test_cache_multimaster_on_response_fc01_count_clamp_boundary_2001);
    RUN_TEST(test_cache_multimaster_on_response_fc02_byte_count_short);
    RUN_TEST(test_cache_multimaster_on_response_null_short_data);
    RUN_TEST(test_cache_multimaster_lookup_timeout_zero);
    RUN_TEST(test_cache_multimaster_lookup_age_check);
    RUN_TEST(test_cache_multimaster_lookup_age_saturation_boundary);
    RUN_TEST(test_cache_multimaster_lookup_not_found_value_unchanged);
    RUN_TEST(test_cache_multimaster_lookup_null_value_out);
    RUN_TEST(test_cache_multimaster_lookup_unknown_fc);
    RUN_TEST(test_cache_multimaster_pool_full_no_crash);
    RUN_TEST(test_cache_multimaster_on_response_fc03_address_zero);
    RUN_TEST(test_cache_multimaster_on_response_fc03_odd_byte_count);
    RUN_TEST(test_cache_multimaster_on_response_fc01_exactly_8_coils);
    RUN_TEST(test_cache_multimaster_on_response_fc02_bounds_check);
    RUN_TEST(test_cache_multimaster_on_response_fc_mismatch);
    RUN_TEST(test_cache_multimaster_lookup_slave_id_zero);
    RUN_TEST(test_cache_multimaster_lookup_fc03_vs_fc04_no_collision);
    RUN_TEST(test_cache_multimaster_on_request_pending_overwrite);
    RUN_TEST(test_cache_multimaster_disable_without_enable);
    RUN_TEST(test_cache_multimaster_age_saturation);
    RUN_TEST(test_cache_status_handler_disabled_empty);
    RUN_TEST(test_cache_status_handler_enabled_two_entries);
    RUN_TEST(test_cache_status_handler_same_slave);
    RUN_TEST(test_cache_csv_handler_empty_pool);
    RUN_TEST(test_cache_csv_handler_two_entries);
    RUN_TEST(test_cache_csv_handler_type_mapping);
    RUN_TEST(test_cache_json_handler_null_mutex);
    RUN_TEST(test_cache_json_handler_empty_pool);
    RUN_TEST(test_cache_json_handler_one_entry);
    RUN_TEST(test_cache_json_handler_type_chars);
    RUN_TEST(test_cache_json_handler_two_entries_comma);
    RUN_TEST(test_cache_multimaster_get_stats_null_out);
    RUN_TEST(test_cache_multimaster_get_stats_not_initialized);
    RUN_TEST(test_cache_multimaster_get_stats_enabled_empty);
    RUN_TEST(test_cache_multimaster_get_stats_unique_slaves);
    RUN_TEST(test_cache_multimaster_get_stats_age_math);
    RUN_TEST(test_cache_multimaster_get_stats_age_guards);
    RUN_TEST(test_cache_multimaster_on_response_fc03_addr_wrap_no_poison);
    RUN_TEST(test_cache_multimaster_on_response_fc01_addr_wrap_no_poison);
    RUN_TEST(test_cache_multimaster_on_response_addr_wrap_exact_boundary);
    RUN_TEST(test_cache_multimaster_pool_full_drop_counter_and_status);

    return UNITY_END();
}
