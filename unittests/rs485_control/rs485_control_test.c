#include "unity.h"
#include "console_log.h"

#include "rs485_control.h"
#include "freertos/semphr.h"

#include <stdbool.h>
#include <string.h>

extern SemaphoreHandle_t mock_xSemaphoreCreateMutex_return;
extern bool mock_xSemaphoreCreateMutex_should_fail;
extern int mock_xSemaphoreCreateMutex_called;
extern int mock_xSemaphoreTake_called;
extern int mock_xSemaphoreGive_called;

extern void rs485_control_test_reset(void);

void setUp(void)
{
    mock_esp_io_expander_set_dir_called = 0;
    mock_esp_io_expander_set_dir_handle = NULL;
    mock_esp_io_expander_set_dir_pin_mask = 0;
    mock_esp_io_expander_set_dir_direction = IO_EXPANDER_INPUT;

    mock_esp_io_expander_set_level_called = 0;
    mock_esp_io_expander_set_level_handle = NULL;
    mock_esp_io_expander_set_level_pin_mask = 0;
    mock_esp_io_expander_set_level_level = 0;

    mock_xSemaphoreCreateMutex_return = NULL;
    mock_xSemaphoreCreateMutex_should_fail = false;
    mock_xSemaphoreCreateMutex_called = 0;
    mock_xSemaphoreTake_called = 0;
    mock_xSemaphoreGive_called = 0;

    rs485_control_test_reset();
}

void tearDown(void)
{

}

// Тестируем успешную инициализацию rs485_control_init
void test_rs485_control_init_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init - success case");
    LOG_MESSAGE();

    rs485_control_init(MOCK_IO_EXPANDER_HANDLE);

    // Verify semaphore was created
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called,
        "xSemaphoreCreateMutex should be called once");

    // Verify all 5 pins were configured as OUTPUT
    // VOUT_485_PIN, TERM485_1_PIN, TERM485_2_PIN, FAILSAFE_485_1_PIN, FAILSAFE_485_2_PIN
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, mock_esp_io_expander_set_dir_called,
        "esp_io_expander_set_dir should be called 5 times");

    // Verify all pins are set to OUTPUT direction
    TEST_ASSERT_EQUAL_MESSAGE(IO_EXPANDER_OUTPUT, mock_esp_io_expander_set_dir_direction,
        "All pins should be configured as OUTPUT");

    // Verify initial levels are set (5 direct calls + 5 from on_off functions)
    // Direct: VOUT_485, TERM485_1, TERM485_2, FAILSAFE_485_1, FAILSAFE_485_2
    // From functions: rs485_bus_vout_on_off(false), rs485_term_on_off x2, rs485_pupd_on_off x2
    TEST_ASSERT_EQUAL_INT_MESSAGE(10, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called 10 times (5 init + 5 from on_off functions)");

    // Verify semaphore operations from rs485_bus_vout_on_off
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreTake_called,
        "xSemaphoreTake should be called once (from rs485_bus_vout_on_off)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreGive_called,
        "xSemaphoreGive should be called once (from rs485_bus_vout_on_off)");
}

// Тестируем инициализацию с NULL handle
void test_rs485_control_init_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init - NULL handle");
    LOG_MESSAGE();

    rs485_control_init(NULL);

    // Verify no initialization happened
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreCreateMutex_called,
        "xSemaphoreCreateMutex should not be called with NULL handle");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_dir_called,
        "esp_io_expander_set_dir should not be called with NULL handle");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should not be called with NULL handle");
}

// Тестируем повторную инициализацию (мьютекс уже создан)
void test_rs485_control_init_mutex_already_created(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init - mutex already created");
    LOG_MESSAGE();

    // First initialization
    rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called,
        "xSemaphoreCreateMutex should be called on first init");

    // Reset test state but keep mutex
    mock_xSemaphoreCreateMutex_called = 0;
    mock_esp_io_expander_set_dir_called = 0;
    mock_esp_io_expander_set_level_called = 0;

    // Second initialization
    rs485_control_init(MOCK_IO_EXPANDER_HANDLE);

    // Mutex should not be created again
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreCreateMutex_called,
        "xSemaphoreCreateMutex should not be called on second init (mutex already exists)");

    // But GPIO configuration should still happen
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, mock_esp_io_expander_set_dir_called,
        "esp_io_expander_set_dir should still be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(10, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should still be called");
}

// Тестируем инициализацию с ошибкой создания мьютекса
void test_rs485_control_init_mutex_creation_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init - mutex creation failure");
    LOG_MESSAGE();

    // Simulate mutex creation failure
    mock_xSemaphoreCreateMutex_should_fail = true;

    rs485_control_init(MOCK_IO_EXPANDER_HANDLE);

    // Verify mutex creation was attempted
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called,
        "xSemaphoreCreateMutex should be called");

    // Initialization should continue despite mutex failure
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, mock_esp_io_expander_set_dir_called,
        "esp_io_expander_set_dir should still be called (init continues on mutex failure)");

    // The on_off functions that require mutex will fail because mutex is NULL
    // Direct set_level calls: 5 (VOUT_485, TERM485_1, TERM485_2, FAILSAFE_485_1, FAILSAFE_485_2)
    // rs485_bus_vout_on_off will fail (no mutex), so no call
    // rs485_term_on_off(RS485_1) - 1 call
    // rs485_term_on_off(RS485_2) - 1 call  
    // rs485_pupd_on_off(RS485_1) - 1 call
    // rs485_pupd_on_off(RS485_2) - 1 call
    // Total: 5 + 4 = 9
    TEST_ASSERT_EQUAL_INT_MESSAGE(9, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called 9 times (5 init + 4 from term/pupd, bus_vout fails)");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_rs485_control_init_success);
    RUN_TEST(test_rs485_control_init_null_handle);
    RUN_TEST(test_rs485_control_init_mutex_already_created);
    RUN_TEST(test_rs485_control_init_mutex_creation_failure);

    return UNITY_END();
}
