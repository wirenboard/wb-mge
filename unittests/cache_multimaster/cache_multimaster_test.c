#include "unity.h"
#include "console_log.h"

#include "cache_multimaster.h"
#include "semphr.h"
#include "task.h"
#include "malloc.h"
#include "esp_timer.h"

#include <stdbool.h>

/* Exposed by cache_multimaster.c when built under __unittest_env__ */
void cache_multimaster_test_reset(void);

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

/* ---- main ---------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_cache_multimaster_init_happy_path);

    return UNITY_END();
}
