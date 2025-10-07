#include "unity.h"
#include "console_log.h"

#include "mio_control.h"
#include "esp_io_expander.h"

#include <stdbool.h>

#define MOCK_IO_EXPANDER_HANDLE ((esp_io_expander_handle_t)0xABCDEF00)

extern int mock_esp_io_expander_set_dir_called;
extern esp_io_expander_handle_t mock_esp_io_expander_set_dir_handle;
extern uint32_t mock_esp_io_expander_set_dir_pin_mask;
extern esp_io_expander_dir_t mock_esp_io_expander_set_dir_direction;

extern int mock_esp_io_expander_set_level_called;
extern esp_io_expander_handle_t mock_esp_io_expander_set_level_handle;
extern uint32_t mock_esp_io_expander_set_level_pin_mask;
extern uint8_t mock_esp_io_expander_set_level_level;

extern void mio_control_test_reset(void);

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

    mio_control_test_reset();
}

void tearDown(void)
{

}

// Тестируем успешную инициализацию mio_control_init
void test_mio_control_init_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_init - success case");
    LOG_MESSAGE();

    mio_control_init(MOCK_IO_EXPANDER_HANDLE);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_io_expander_set_dir_called,
        "esp_io_expander_set_dir should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_IO_EXPANDER_HANDLE, mock_esp_io_expander_set_dir_handle,
        "Handle passed to set_dir should match");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(IO_EXPANDER_PIN_NUM_8, mock_esp_io_expander_set_dir_pin_mask,
        "Pin mask should be IO_EXPANDER_PIN_NUM_8");
    TEST_ASSERT_EQUAL_MESSAGE(IO_EXPANDER_OUTPUT, mock_esp_io_expander_set_dir_direction,
        "Direction should be OUTPUT");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_IO_EXPANDER_HANDLE, mock_esp_io_expander_set_level_handle,
        "Handle passed to set_level should match");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(IO_EXPANDER_PIN_NUM_8, mock_esp_io_expander_set_level_pin_mask,
        "Pin mask should be IO_EXPANDER_PIN_NUM_8");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, mock_esp_io_expander_set_level_level,
        "Initial level should be 0 (disabled)");
}

// Тестируем инициализацию с NULL handle
void test_mio_control_init_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_init - NULL handle");
    LOG_MESSAGE();

    mio_control_init(NULL);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_dir_called,
        "esp_io_expander_set_dir should not be called with NULL handle");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should not be called with NULL handle");
}

// Тестируем включение IO bus
void test_mio_control_io_bus_onoff_enable(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_io_bus_onoff - enable");
    LOG_MESSAGE();

    mio_control_init(MOCK_IO_EXPANDER_HANDLE);

    mio_control_io_bus_onoff(true);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called twice");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_IO_EXPANDER_HANDLE, mock_esp_io_expander_set_level_handle,
        "Handle should match");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(IO_EXPANDER_PIN_NUM_8, mock_esp_io_expander_set_level_pin_mask,
        "Pin mask should be IO_EXPANDER_PIN_NUM_8");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, mock_esp_io_expander_set_level_level,
        "Level should be 1 (enabled)");
}

// Тестируем выключение IO bus
void test_mio_control_io_bus_onoff_disable(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_io_bus_onoff - disable");
    LOG_MESSAGE();

    mio_control_init(MOCK_IO_EXPANDER_HANDLE);

    mio_control_io_bus_onoff(false);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called twice");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_IO_EXPANDER_HANDLE, mock_esp_io_expander_set_level_handle,
        "Handle should match");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(IO_EXPANDER_PIN_NUM_8, mock_esp_io_expander_set_level_pin_mask,
        "Pin mask should be IO_EXPANDER_PIN_NUM_8");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, mock_esp_io_expander_set_level_level,
        "Level should be 0 (disabled)");
}

// Тестируем переключение IO bus
void test_mio_control_io_bus_onoff_toggle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_io_bus_onoff - toggle");
    LOG_MESSAGE();

    mio_control_init(MOCK_IO_EXPANDER_HANDLE);

    mio_control_io_bus_onoff(true);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, mock_esp_io_expander_set_level_level,
        "Level should be 1 after enable");

    mio_control_io_bus_onoff(false);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, mock_esp_io_expander_set_level_level,
        "Level should be 0 after disable");

    mio_control_io_bus_onoff(true);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, mock_esp_io_expander_set_level_level,
        "Level should be 1 after re-enable");

    TEST_ASSERT_EQUAL_INT_MESSAGE(4, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called 4 times");
}

// Тестируем вызов io_bus_onoff без инициализации
void test_mio_control_io_bus_onoff_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_io_bus_onoff - not initialized");
    LOG_MESSAGE();

    mio_control_io_bus_onoff(true);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should not be called when not initialized");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_mio_control_init_success);
    RUN_TEST(test_mio_control_init_null_handle);
    RUN_TEST(test_mio_control_io_bus_onoff_enable);
    RUN_TEST(test_mio_control_io_bus_onoff_disable);
    RUN_TEST(test_mio_control_io_bus_onoff_toggle);
    RUN_TEST(test_mio_control_io_bus_onoff_not_initialized);

    return UNITY_END();
}
