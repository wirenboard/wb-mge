#include "unity.h"
#include "console_log.h"

#include "cache_multimaster.h"
#include "semphr.h"
#include "task.h"
#include "malloc.h"
#include "esp_timer.h"

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
    RUN_TEST(test_cache_multimaster_on_request_valid);
    RUN_TEST(test_cache_multimaster_on_response_no_pending);
    RUN_TEST(test_cache_multimaster_on_response_fc03_correct);
    RUN_TEST(test_cache_multimaster_on_response_truncated_fc03);
    RUN_TEST(test_cache_multimaster_on_response_malformed_length);
    RUN_TEST(test_cache_multimaster_on_response_fc01_9_coils);
    RUN_TEST(test_cache_multimaster_on_response_fc01_count_overflow);
    RUN_TEST(test_cache_multimaster_on_response_fc02_byte_count_short);
    RUN_TEST(test_cache_multimaster_on_response_null_short_data);
    RUN_TEST(test_cache_multimaster_lookup_timeout_zero);
    RUN_TEST(test_cache_multimaster_lookup_age_check);
    RUN_TEST(test_cache_multimaster_lookup_age_saturation_boundary);

    return UNITY_END();
}
