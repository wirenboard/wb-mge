#include "unity.h"
#include "console_log.h"

#include "settings_save_timer.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_bit_defs.h"

#define SETTING_SAVE_TIMER_INTERVAL_MS          1000
#define EVENT_BIT_READY                         BIT0

void settings_save_timer_reset(void);

void setUp(void)
{
    mock_freertos_task_reset();
    mock_freertos_event_groups_reset();
    mock_freertos_timers_reset();
    settings_save_timer_reset();
}

void tearDown(void)
{

}

static void execute_timer_callback()
{
    mock_xTimerCreate_pxCallbackFunction(mock_xTimerCreate_return_value);
}

static void verify_event_group_ready_flag_set(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xEventGroupSetBits_data.called, "xEventGroupSetBits should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        mock_xEventGroupCreate_data.return_value,
        mock_xEventGroupSetBits_data.xEventGroup,
        "Event group handle should match"
    );
    TEST_ASSERT_EQUAL_MESSAGE(EVENT_BIT_READY, mock_xEventGroupSetBits_data.uxBitsToSet, "READY flag should be set");
}

static void verify_timer_created(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xTimerCreate_called, "xTimerCreate should be called once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("settings_save_timer", mock_xTimerCreate_pcTimerName, "Timer name should be 'settings_save_timer'");
    TEST_ASSERT_EQUAL_MESSAGE(
        pdMS_TO_TICKS(SETTING_SAVE_TIMER_INTERVAL_MS),
        mock_xTimerCreate_xTimerPeriod,
        "Timer period should be 1000 ms"
    );
    TEST_ASSERT_EQUAL_MESSAGE(pdFALSE, mock_xTimerCreate_xAutoReload, "Timer should not auto-reload");
    TEST_ASSERT_NULL_MESSAGE(mock_xTimerCreate_pvTimerID, "Timer ID should be NULL");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_xTimerCreate_pxCallbackFunction, "Timer callback should not be NULL");
}

static void verify_timer_and_callback(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xTimerStart_called, "xTimerStart should be called twice");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        mock_xTimerCreate_return_value,
        mock_xTimerStart_xTimer,
        "Timer handle should match"
    );

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTimerStart_xTicksToWait, "Timer start should not wait");

    TEST_ASSERT_EQUAL_MESSAGE(2, mock_xEventGroupSetBits_data.called, "xEventGroupSetBits should be called twice");
    TEST_ASSERT_EQUAL_MESSAGE(EVENT_BIT_READY, mock_xEventGroupSetBits_data.uxBitsToSet, "READY flag should be set");
}

// Тестируем успешную инициализацию таймера
void test_settings_save_timer_auto_init_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_save_timer_auto_init - success");
    LOG_MESSAGE();

    esp_err_t result = settings_save_timer_auto_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Initialization should succeed");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xEventGroupCreate_data.called, "xEventGroupCreate should be called once");
    verify_event_group_ready_flag_set();
    verify_timer_created();
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vEventGroupDelete_data.called, "vEventGroupDelete should not be called");
}

// Тестируем инициализацию при ошибке создания группы событий
void test_settings_save_timer_auto_init_event_group_creation_fails(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_save_timer_auto_init - event group creation fails");
    LOG_MESSAGE();

    mock_xEventGroupCreate_data.should_fail = true;

    esp_err_t result = settings_save_timer_auto_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "Initialization should fail");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xEventGroupCreate_data.called, "xEventGroupCreate should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xEventGroupSetBits_data.called, "xEventGroupSetBits should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTimerCreate_called, "xTimerCreate should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vEventGroupDelete_data.called, "vEventGroupDelete should not be called");
}

// Тестируем инициализацию при ошибке создания таймера
void test_settings_save_timer_auto_init_timer_creation_fails(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_save_timer_auto_init - timer creation fails");
    LOG_MESSAGE();

    mock_xTimerCreate_return_value = NULL;

    esp_err_t result = settings_save_timer_auto_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "Initialization should fail");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xEventGroupCreate_data.called, "xEventGroupCreate should be called once");
    verify_event_group_ready_flag_set();
    verify_timer_created();

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_vEventGroupDelete_data.called, "vEventGroupDelete should be called to cleanup");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        mock_xEventGroupCreate_data.return_value,
        mock_vEventGroupDelete_data.xEventGroup,
        "Deleted event group should match created one"
    );
}

// Тестируем повторную инициализацию (должна быть игнорирована)
void test_settings_save_timer_auto_init_multiple_calls(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_save_timer_auto_init - multiple calls");
    LOG_MESSAGE();

    esp_err_t result1 = settings_save_timer_auto_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result1, "First initialization should succeed");

    mock_freertos_task_reset();
    mock_freertos_event_groups_reset();
    mock_freertos_timers_reset();

    esp_err_t result2 = settings_save_timer_auto_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result2, "Second initialization should succeed");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xEventGroupCreate_data.called, "xEventGroupCreate should not be called again");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTimerCreate_called, "xTimerCreate should not be called again");
}

// Тестируем settings_save_timer_wait с установленным флагом READY
void test_settings_save_timer_wait_ready_flag_set(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_save_timer_wait - READY flag set");
    LOG_MESSAGE();

    settings_save_timer_auto_init();

    mock_xEventGroupWaitBits_data.return_value |= EVENT_BIT_READY;

    esp_err_t result = settings_save_timer_wait();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Wait should succeed");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xEventGroupWaitBits_data.called, "xEventGroupWaitBits should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        mock_xEventGroupCreate_data.return_value,
        mock_xEventGroupWaitBits_data.xEventGroup,
        "Event group handle should match"
    );

    TEST_ASSERT_EQUAL_MESSAGE(EVENT_BIT_READY, mock_xEventGroupWaitBits_data.uxBitsToWaitFor, "Should wait for READY flag");
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, mock_xEventGroupWaitBits_data.xClearOnExit, "Should clear flag on exit");
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, mock_xEventGroupWaitBits_data.xWaitForAllBits, "Should wait for all bits");
    TEST_ASSERT_EQUAL_MESSAGE(
        pdMS_TO_TICKS(SETTING_SAVE_TIMER_INTERVAL_MS),
        mock_xEventGroupWaitBits_data.xTicksToWait,
        "Wait timeout should be correct"
    );

    execute_timer_callback();
    verify_timer_and_callback();
}

// Тестируем settings_save_timer_wait с таймаутом (флаг READY не установлен)
void test_settings_save_timer_wait_timeout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_save_timer_wait - timeout");
    LOG_MESSAGE();

    settings_save_timer_auto_init();

    esp_err_t result = settings_save_timer_wait();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_TIMEOUT, result, "Wait should timeout");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xEventGroupWaitBits_data.called, "xEventGroupWaitBits should be called once");

    execute_timer_callback();
    verify_timer_and_callback();
}

// Тестируем ожидание без инициализации (fail-safe)
void test_settings_save_timer_wait_without_init(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_save_timer_wait - without init (fail-safe)");
    LOG_MESSAGE();

    esp_err_t result = settings_save_timer_wait();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_STATE, result, "Wait should return invalid state");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_vTaskDelay_data.called, "vTaskDelay should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(
        pdMS_TO_TICKS(SETTING_SAVE_TIMER_INTERVAL_MS),
        mock_vTaskDelay_data.xTicksToDelay,
        "Delay should be correct"
    );

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xEventGroupWaitBits_data.called, "xEventGroupWaitBits should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTimerStart_called, "xTimerStart should not be called");
}

// Тестируем timer_callback с event_group == NULL
void test_settings_save_timer_callback_with_null_event_group(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test timer_callback - with NULL event_group");
    LOG_MESSAGE();

    esp_err_t init_result = settings_save_timer_auto_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, init_result, "Initialization should succeed");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_xTimerCreate_pxCallbackFunction, "Callback should not be NULL");

    settings_save_timer_reset();

    execute_timer_callback();

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xEventGroupSetBits_data.called,
        "xEventGroupSetBits should be called once when event_group is NULL");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_settings_save_timer_auto_init_success);
    RUN_TEST(test_settings_save_timer_auto_init_event_group_creation_fails);
    RUN_TEST(test_settings_save_timer_auto_init_timer_creation_fails);
    RUN_TEST(test_settings_save_timer_auto_init_multiple_calls);
    RUN_TEST(test_settings_save_timer_wait_ready_flag_set);
    RUN_TEST(test_settings_save_timer_wait_timeout);
    RUN_TEST(test_settings_save_timer_wait_without_init);
    RUN_TEST(test_settings_save_timer_callback_with_null_event_group);

    return UNITY_END();
}
