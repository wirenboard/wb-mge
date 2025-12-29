#include "unity.h"
#include "console_log.h"

#include "mio_control.h"
#include "gpio_expander_mock.h"

#include <stdbool.h>
#include <string.h>

#define MIO_RESET_PIN                   IO_EXPANDER_PIN_NUM_8

void mio_control_test_reset(void);

void setUp(void)
{
    mock_gpio_expander_reset();
}

void tearDown(void)
{

}

// Валидация выполнения функции mio_control_init()
void validate_mio_control_init_run(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_gpio_expander_set_out_dir_and_level_data.called,
        "gpio_expander_set_out_dir_and_level() should be called once"
    );
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        IO_EXPANDER_PIN_NUM_8,
        mock_gpio_expander_set_out_dir_and_level_data.masks[0],
        "pin_num_mask in gpio_expander_set_out_dir_and_level() call should be IO_EXPANDER_PIN_NUM_8"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0,
        mock_gpio_expander_set_out_dir_and_level_data.levels[0],
        "level in in gpio_expander_set_out_dir_and_level() call should be 0"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_gpio_expander_init_data.called,
        "gpio_expander_init() should NOT be called"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_gpio_expander_set_dir_data.called,
        "gpio_expander_set_dir() should NOT be called"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_gpio_expander_set_level_data.called,
        "gpio_expander_set_level() should NOT be called"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_gpio_expander_get_level_data.called,
        "gpio_expander_get_level() should NOT be called"
    );
}

// Тестируем успешное выполнение mio_control_init()
void test_mio_control_init_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_init() - success case");
    LOG_MESSAGE();

    esp_err_t result = mio_control_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "mio_control_init() should return ESP_OK");
    validate_mio_control_init_run();
}

// Тестируем mio_control_init() при ошибке gpio_expander_set_out_dir_and_level()
void test_mio_control_init_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_init() - gpio_expander_set_out_dir_and_level() fail");
    LOG_MESSAGE();

    mock_gpio_expander_set_out_dir_and_level_data.should_fail = true;

    esp_err_t result = mio_control_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "mio_control_init() should return ESP_FAIL");
    validate_mio_control_init_run();
}

// Валидация выполнения mio_control_io_bus_onoff()
void validate_mio_control_io_bus_onoff_run(bool enabled)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_gpio_expander_set_level_data.called,
        "gpio_expander_set_level() should be called once"
    );
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        IO_EXPANDER_PIN_NUM_8,
        mock_gpio_expander_set_level_data.masks[0],
        "pin_num_mask in gpio_expander_set_level() call should be IO_EXPANDER_PIN_NUM_8"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        enabled,
        mock_gpio_expander_set_level_data.levels[0],
        "level in gpio_expander_set_level() call should be equal to provided value"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_gpio_expander_init_data.called,
        "gpio_expander_init() should NOT be called"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_gpio_expander_set_dir_data.called,
        "gpio_expander_set_dir() should NOT be called"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_gpio_expander_set_out_dir_and_level_data.called,
        "gpio_expander_set_out_dir_and_level() should NOT be called"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_gpio_expander_get_level_data.called,
        "gpio_expander_get_level() should NOT be called"
    );
}

// Тестируем успешное выполнение mio_control_io_bus_onoff()
void test_mio_control_io_bus_onoff_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_io_bus_onoff() - success case");
    LOG_MESSAGE();

    bool enabled = true;
    LOG_INFO("Testing with enabled = %s", enabled ? "true" : "false");

    esp_err_t result = mio_control_io_bus_onoff(enabled);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "mio_control_io_bus_onoff() should return ESP_OK");
    validate_mio_control_io_bus_onoff_run(enabled);

    // Second test
    setUp();

    enabled = false;
    LOG_INFO("Testing with enabled = %s", enabled ? "true" : "false");

    result = mio_control_io_bus_onoff(enabled);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "mio_control_io_bus_onoff() should return ESP_OK");
    validate_mio_control_io_bus_onoff_run(enabled);
}

// Тестируем mio_control_io_bus_onoff() с ошибкой выполнения gpio_expander_set_level()
void test_mio_control_io_bus_onoff_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test mio_control_io_bus_onoff() - gpio_expander_set_level() fail");
    LOG_MESSAGE();

    mock_gpio_expander_set_level_data.should_fail = true;

    bool enabled = true;
    LOG_INFO("Testing with enabled = %s", enabled ? "true" : "false");

    esp_err_t result = mio_control_io_bus_onoff(enabled);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "mio_control_io_bus_onoff() should return ESP_FAIL");
    validate_mio_control_io_bus_onoff_run(enabled);

    // Second test
    setUp();
    mock_gpio_expander_set_level_data.should_fail = true;

    enabled = false;
    LOG_INFO("Testing with enabled = %s", enabled ? "true" : "false");

    result = mio_control_io_bus_onoff(enabled);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "mio_control_io_bus_onoff() should return ESP_FAIL");
    validate_mio_control_io_bus_onoff_run(enabled);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_mio_control_init_success);
    RUN_TEST(test_mio_control_init_fail);

    RUN_TEST(test_mio_control_io_bus_onoff_success);
    RUN_TEST(test_mio_control_io_bus_onoff_fail);

    return UNITY_END();
}
