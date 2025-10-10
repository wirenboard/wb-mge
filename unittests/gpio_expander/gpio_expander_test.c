#include "unity.h"
#include "console_log.h"

#include "gpio_expander.h"
#include "driver/i2c_master.h"
#include "esp_io_expander_tca95xx_16bit.h"

#include <stdbool.h>
#include <string.h>

void gpio_expander_test_reset(void);

void setUp(void)
{
    mock_i2c_new_master_bus_return = ESP_OK;
    mock_i2c_new_master_bus_called = 0;
    mock_i2c_bus_handle = NULL;
    memset(&mock_i2c_bus_config, 0, sizeof(i2c_master_bus_config_t));

    mock_esp_io_expander_return = ESP_OK;
    mock_esp_io_expander_called = 0;
    mock_esp_io_expander_bus = NULL;
    mock_esp_io_expander_addr = 0;

    mock_esp_io_expander_print_state_return = ESP_OK;
    mock_esp_io_expander_print_state_called = 0;
    mock_esp_io_expander_print_state_handle = NULL;

    gpio_expander_test_reset();
}

void tearDown(void)
{

}

// Тестируем случай успешной инициализации gpio_expander_init
void test_gpio_expander_init_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_init - success case");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_init should return ESP_OK");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(handle, MOCK_EXPANDER_HANDLE, "Handle should not be NULL after successful init");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_new_master_bus_called, "i2c_new_master_bus should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(I2C_NUM_0, mock_i2c_bus_config.i2c_port, "I2C port should be I2C_NUM_0");
    TEST_ASSERT_EQUAL_MESSAGE(GPIO_NUM_32, mock_i2c_bus_config.sda_io_num, "SDA pin should be GPIO_NUM_32");
    TEST_ASSERT_EQUAL_MESSAGE(GPIO_NUM_33, mock_i2c_bus_config.scl_io_num, "SCL pin should be GPIO_NUM_33");
    TEST_ASSERT_EQUAL_MESSAGE(
        I2C_CLK_SRC_DEFAULT, mock_i2c_bus_config.clk_source, "Clock source should be I2C_CLK_SRC_DEFAULT"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        1, mock_i2c_bus_config.flags.enable_internal_pullup, "Internal pullup should be enabled"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should be called once"
    );
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        mock_i2c_bus_handle,
        mock_esp_io_expander_bus,
        "I2C bus handle should be passed to expander creation"
    );
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000,
        mock_esp_io_expander_addr,
        "Device address should be ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_print_state_called, "esp_io_expander_print_state should be called once"
    );
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_EXPANDER_HANDLE, mock_esp_io_expander_print_state_handle,
        "Handle passed to print_state should match the expander handle"
    );
}

// Тестируем повторную инициализацию gpio_expander_init
void test_gpio_expander_init_reinit_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_init - reinit success case");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_init should return ESP_OK");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(handle, MOCK_EXPANDER_HANDLE, "Handle should not be NULL after successful init");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_new_master_bus_called, "i2c_new_master_bus should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should be called once"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_print_state_called, "esp_io_expander_print_state should be called once"
    );

    // Повторная инициализация должна вернуть ESP_OK
    // но без повторных вызовов i2c_new_master_bus, esp_io_expander_new_i2c_tca95xx_16bit и esp_io_expander_print_state
    result = gpio_expander_init(&handle);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_init should return ESP_OK when already initialized");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_new_master_bus_called, "i2c_new_master_bus should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should be called once"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_print_state_called, "esp_io_expander_print_state should be called once"
    );
}

// Тестируем инициализацию gpio_expander_init с NULL handle
void test_gpio_expander_init_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_init - NULL handle");
    LOG_MESSAGE();

    esp_err_t result = gpio_expander_init(NULL);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, result, "gpio_expander_init should return ESP_ERR_INVALID_ARG");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_i2c_new_master_bus_called, "i2c_new_master_bus should not be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should not be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_print_state_called, "esp_io_expander_print_state should not be called");
}

// Тестируем ошибку при создании I2C master bus
void test_gpio_expander_init_i2c_bus_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_init - I2C bus creation failure");
    LOG_MESSAGE();

    mock_i2c_new_master_bus_return = ESP_FAIL;

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_init should return ESP_FAIL when I2C bus creation fails");
    TEST_ASSERT_NULL_MESSAGE(handle, "Handle should be unchanged when init fails");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_new_master_bus_called, "i2c_new_master_bus should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should not be called"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, mock_esp_io_expander_print_state_called, "esp_io_expander_print_state should not be called"
    );
}

// Тестируем ошибку при создании GPIO expander object
void test_gpio_expander_init_expander_creation_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_init - GPIO expander creation failure");
    LOG_MESSAGE();

    mock_esp_io_expander_return = ESP_FAIL;

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_init should return ESP_FAIL when GPIO expander creation fails");
    TEST_ASSERT_NULL_MESSAGE(handle, "Handle should be unchanged when init fails");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_new_master_bus_called, "i2c_new_master_bus should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should be called once"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, mock_esp_io_expander_print_state_called, "esp_io_expander_print_state should not be called"
    );
}

// Тестируем ошибку при печати состояния GPIO expander
void test_gpio_expander_init_print_state_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_init - print state failure");
    LOG_MESSAGE();

    mock_esp_io_expander_print_state_return = ESP_FAIL;

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_init should return ESP_FAIL when print state fails");
    TEST_ASSERT_NULL_MESSAGE(handle, "Handle should be unchanged when init fails");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_new_master_bus_called, "i2c_new_master_bus should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should be called once"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_print_state_called, "esp_io_expander_print_state should be called once"
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_gpio_expander_init_success);
    RUN_TEST(test_gpio_expander_init_reinit_success);
    RUN_TEST(test_gpio_expander_init_null_handle);
    RUN_TEST(test_gpio_expander_init_i2c_bus_failure);
    RUN_TEST(test_gpio_expander_init_expander_creation_failure);
    RUN_TEST(test_gpio_expander_init_print_state_failure);

    return UNITY_END();
}
