#include "unity.h"
#include "console_log.h"

#include "cache_multimaster.h"
#include "semphr.h"
#include "task.h"
#include "malloc.h"
#include "esp_timer.h"

#include <stdbool.h>

/* BRIDGES_COUNT is defined in bridge.h, but bridge.h pulls in serial.h and other
 * complex headers that are not mocked. Define locally to avoid dependency hell. */
#ifndef BRIDGES_COUNT
#define BRIDGES_COUNT 2  /* Must match the value in bridge.h */
#endif

/* Exposed by cache_multimaster.c when built under __unittest_env__ */
void cache_multimaster_test_reset(void);
bool cache_multimaster_test_get_pending_valid(uint8_t port);

/* Exposed by mocks/sniffer.c */
extern int  mock_sniffer_set_cache_active_called;
extern bool mock_sniffer_set_cache_active_last_value;
void mock_sniffer_reset(void);

/* ---- setUp / tearDown ---------------------------------------------------- */

void setUp(void)
{
    cache_multimaster_test_reset();
    mock_freertos_semaphore_reset();
    mock_freertos_task_reset();
    reset_malloc_tracking();
    mock_esp_timer_reset();
    mock_sniffer_reset();
}

void tearDown(void)
{
}

/* ---- CM-U-001: cache_multimaster_init() happy path ----------------------- */

/* Verify that cache_multimaster_init() with a valid semaphore handle:
 *   - returns ESP_OK
 *   - calls xSemaphoreCreateMutex exactly once
 *   - leaves cache disabled (cache_multimaster_is_enabled() == false)
 *   - does NOT call sniffer_set_cache_active (init only creates the mutex) */
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

    /* Assert: sniffer_set_cache_active must NOT have been called during init */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_sniffer_set_cache_active_called,
        "sniffer_set_cache_active should not be called during cache_multimaster_init"
    );
}

/* ---- CM-U-002: cache_multimaster_init() OOM path ------------------------- */

/* Verify that cache_multimaster_init() with a NULL semaphore handle:
 *   - returns ESP_ERR_NO_MEM
 *   - leaves cache disabled (cache_multimaster_is_enabled() == false)
 *   - does NOT call sniffer_set_cache_active */
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

    /* Assert: sniffer_set_cache_active must NOT have been called */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_sniffer_set_cache_active_called,
        "sniffer_set_cache_active should not be called when init fails"
    );
}

/* ---- CM-U-003: cache_multimaster_enable() happy path --------------------- */

/* Verify that cache_multimaster_enable() after a successful init:
 *   - sets cache_multimaster_is_enabled() to true
 *   - calls sniffer_set_cache_active(true) exactly once
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

    /* Assert: sniffer called once with true */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_sniffer_set_cache_active_called,
        "sniffer_set_cache_active should be called exactly once after enable()"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        mock_sniffer_set_cache_active_last_value,
        "sniffer_set_cache_active should be called with true on enable()"
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
 *   - does NOT call sniffer_set_cache_active
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

    /* Assert: sniffer must NOT be activated when allocation fails */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_sniffer_set_cache_active_called,
        "sniffer_set_cache_active should not be called when pool allocation fails"
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

/* ---- CM-U-006: cache_multimaster_disable() -------------------------------- */

/* Verify that cache_multimaster_disable() after enable():
 *   - sets cache_multimaster_is_enabled() to false
 *   - calls sniffer_set_cache_active(false) exactly once
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

    /* Reset sniffer mock so that disable() calls are counted from zero */
    mock_sniffer_reset();

    /* Act */
    cache_multimaster_disable();

    /* Assert: cache must be disabled */
    TEST_ASSERT_FALSE_MESSAGE(
        cache_multimaster_is_enabled(),
        "cache_multimaster_is_enabled() should return false after disable()"
    );

    /* Assert: sniffer called once with false */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_sniffer_set_cache_active_called,
        "sniffer_set_cache_active should be called exactly once after disable()"
    );
    TEST_ASSERT_FALSE_MESSAGE(
        mock_sniffer_set_cache_active_last_value,
        "sniffer_set_cache_active should be called with false on disable()"
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

/* ---- main ---------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_cache_multimaster_init_happy_path);
    RUN_TEST(test_cache_multimaster_init_oom);
    RUN_TEST(test_cache_multimaster_enable_happy_path);
    RUN_TEST(test_cache_multimaster_enable_oom);
    RUN_TEST(test_cache_multimaster_disable);
    RUN_TEST(test_cache_multimaster_on_request_oob_port);

    return UNITY_END();
}
