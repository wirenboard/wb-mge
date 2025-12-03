#include "unity.h"
#include "console_log.h"

#include "mio_control.h"
#include "esp_io_expander.h"

#include <stdbool.h>
#include <string.h>

#define MIO_RESET_PIN                   IO_EXPANDER_PIN_NUM_8

void mio_control_test_reset(void);

void setUp(void)
{
    mock_esp_io_expander_reset();
    mio_control_test_reset();
}

void tearDown(void)
{

}

// Тестируем случай успешной инициализации mio_control_init
void test_mio_control_init_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_init - success case");
    LOG_MESSAGE();

    esp_err_t result = mio_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "mio_control_init should return ESP_OK");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_io_expander_set_dir_called,
        "esp_io_expander_set_dir should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_IO_EXPANDER_HANDLE, mock_esp_io_expander_set_dir_handle,
        "Handle passed to set_dir should match");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(MIO_RESET_PIN, mock_esp_io_expander_set_dir_pin_masks[0],
        "Pin mask should be IO_EXPANDER_PIN_NUM_8");
    TEST_ASSERT_EQUAL_MESSAGE(IO_EXPANDER_OUTPUT, mock_esp_io_expander_set_dir_directions[0],
        "Direction should be OUTPUT");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_IO_EXPANDER_HANDLE, mock_esp_io_expander_set_level_handle,
        "Handle passed to set_level should match");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(MIO_RESET_PIN, mock_esp_io_expander_set_level_pin_masks[0],
        "Pin mask should be IO_EXPANDER_PIN_NUM_8");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, mock_esp_io_expander_set_level_levels[0],
        "Initial level should be 0 (disabled)");
}

// Тестируем инициализацию с NULL handle
void test_mio_control_init_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_init - NULL handle");
    LOG_MESSAGE();

    esp_err_t result = mio_control_init(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "mio_control_init should return ESP_FAIL");

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

    esp_err_t result = mio_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "mio_control_init should return ESP_OK");

    result = mio_control_io_bus_onoff(true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "mio_control_io_bus_onoff should return ESP_OK");

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called twice");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_IO_EXPANDER_HANDLE, mock_esp_io_expander_set_level_handle,
        "Handle should match");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(MIO_RESET_PIN, mock_esp_io_expander_set_level_pin_masks[1],
        "Pin mask should be IO_EXPANDER_PIN_NUM_8");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, mock_esp_io_expander_set_level_levels[1],
        "Level should be 1 (enabled)");
}

// Тестируем выключение IO bus
void test_mio_control_io_bus_onoff_disable(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_io_bus_onoff - disable");
    LOG_MESSAGE();

    esp_err_t result = mio_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "mio_control_init should return ESP_OK");

    result = mio_control_io_bus_onoff(false);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "mio_control_io_bus_onoff should return ESP_OK");

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called twice");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_IO_EXPANDER_HANDLE, mock_esp_io_expander_set_level_handle,
        "Handle should match");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(MIO_RESET_PIN, mock_esp_io_expander_set_level_pin_masks[1],
        "Pin mask should be IO_EXPANDER_PIN_NUM_8");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, mock_esp_io_expander_set_level_levels[1],
        "Level should be 0 (disabled)");
}

// Тестируем переключение IO bus
void test_mio_control_io_bus_onoff_toggle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_io_bus_onoff - toggle");
    LOG_MESSAGE();

    esp_err_t result = mio_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "mio_control_init should return ESP_OK");

    result = mio_control_io_bus_onoff(true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "mio_control_io_bus_onoff should return ESP_OK");

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, mock_esp_io_expander_set_level_levels[1],
        "Level should be 1 after enable");

    mio_control_io_bus_onoff(false);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, mock_esp_io_expander_set_level_levels[2],
        "Level should be 0 after disable");

    mio_control_io_bus_onoff(true);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, mock_esp_io_expander_set_level_levels[3],
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

    esp_err_t result = mio_control_io_bus_onoff(true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "mio_control_io_bus_onoff should return ESP_FAIL");

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
