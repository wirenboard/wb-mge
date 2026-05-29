#include "unity.h"
#include "console_log.h"

#include "gpio_expander.h"
#include "driver/i2c_master.h"
#include "esp_io_expander.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "freertos/semphr.h"

#include <stdbool.h>
#include <string.h>
#include <inttypes.h>

void gpio_expander_test_reset(void);
void gpio_expander_test_set_mutex_null(void);

void setUp(void)
{
    mock_i2c_master_reset();
    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    mock_esp_io_expander_return = ESP_OK;
    mock_esp_io_expander_called = 0;
    mock_esp_io_expander_bus = NULL;
    mock_esp_io_expander_addr = 0;

    gpio_expander_test_reset();
}

void tearDown(void)
{

}

void validate_i2c_new_master_bus_config()
{
    TEST_ASSERT_EQUAL_MESSAGE(I2C_NUM_0, mock_i2c_new_master_bus_data.bus_config.i2c_port, "I2C port should be I2C_NUM_0");
    TEST_ASSERT_EQUAL_MESSAGE(GPIO_NUM_32, mock_i2c_new_master_bus_data.bus_config.sda_io_num, "SDA pin should be GPIO_NUM_32");
    TEST_ASSERT_EQUAL_MESSAGE(GPIO_NUM_33, mock_i2c_new_master_bus_data.bus_config.scl_io_num, "SCL pin should be GPIO_NUM_33");
    TEST_ASSERT_EQUAL_MESSAGE(I2C_CLK_SRC_DEFAULT, mock_i2c_new_master_bus_data.bus_config.clk_source, "Clock source should be I2C_CLK_SRC_DEFAULT");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_i2c_new_master_bus_data.bus_config.flags.enable_internal_pullup, "Internal pullup should be enabled");
}

// Validate successful gpio_expander_init startup
void validate_gpio_expander_init_success(esp_err_t result, esp_io_expander_handle_t handle)
{
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_init should return ESP_OK");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_EXPANDER_HANDLE, handle, "Handle should not be NULL after successful init");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex should be called once");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_new_master_bus_data.called, "i2c_new_master_bus should be called once");
    validate_i2c_new_master_bus_config();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should be called once"
    );
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_I2C_MASTER_BUS_HANDLE,
        mock_esp_io_expander_bus,
        "I2C bus handle should be passed to expander creation"
    );
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000,
        mock_esp_io_expander_addr,
        "Device address should be ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should be called once"
    );
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_EXPANDER_HANDLE, mock_esp_io_expander_print_state_data.handle,
        "Handle passed to print_state should match the expander handle"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_vSemaphoreDelete_called, "vSemaphoreDelete should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_i2c_del_master_bus_data.called, "i2c_del_master_bus should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");
}

// Test the successful initialization case of gpio_expander_init
void test_gpio_expander_init_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_init - success case");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);

    validate_gpio_expander_init_success(result, handle);
}

// Test repeated initialization of gpio_expander_init
void test_gpio_expander_init_reinit_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_init - reinit success case");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);

    validate_gpio_expander_init_success(result, handle);

    // Re-initialization must return ESP_OK
    // but without repeated calls to xSemaphoreCreateMutex, i2c_new_master_bus, esp_io_expander_new_i2c_tca95xx_16bit and esp_io_expander_print_state
    // handle must not change either
    esp_io_expander_handle_t bkp_handle = handle;
    result = gpio_expander_init(&handle);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_init should return ESP_OK when already initialized");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(bkp_handle, handle, "Handle should be NOT changed on repeat init call");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex should be called only once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_new_master_bus_data.called, "i2c_new_master_bus should be called only once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should be called only once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should be called only once");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_vSemaphoreDelete_called, "vSemaphoreDelete should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_i2c_del_master_bus_data.called, "i2c_del_master_bus should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");
}

// Test gpio_expander_init with NULL handle
void test_gpio_expander_init_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_init - NULL handle");
    LOG_MESSAGE();

    esp_err_t result = gpio_expander_init(NULL);

    // Initialization should proceed as usual except that handle will not be returned
    validate_gpio_expander_init_success(result, MOCK_EXPANDER_HANDLE);
}

// Test gpio_expander_init with mutex creation failure
void test_gpio_expander_init_mutex_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_init - mutex creation failure");
    LOG_MESSAGE();

    mock_xSemaphoreCreateMutex_return_value = NULL;

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_init should return ESP_FAIL when mutex creation fails");
    TEST_ASSERT_NULL_MESSAGE(handle, "Handle should be unchanged when init fails");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_i2c_new_master_bus_data.called, "i2c_new_master_bus should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should NOT be called");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_vSemaphoreDelete_called, "vSemaphoreDelete should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_i2c_del_master_bus_data.called, "i2c_del_master_bus should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");
}

// Test gpio_expander_init with I2C master bus creation failure
void test_gpio_expander_init_i2c_bus_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_init - I2C bus creation failure");
    LOG_MESSAGE();

    mock_i2c_new_master_bus_data.should_fail = true;

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_init should return ESP_FAIL when I2C bus creation fails");
    TEST_ASSERT_NULL_MESSAGE(handle, "Handle should be unchanged when init fails");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_new_master_bus_data.called, "i2c_new_master_bus should be called once");
    validate_i2c_new_master_bus_config();

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should NOT be called");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_vSemaphoreDelete_called, "vSemaphoreDelete should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_SEMAPHORE_HANDLE_T, mock_vSemaphoreDelete_Handle, "vSemaphoreDelete should be called with correct handle");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_i2c_del_master_bus_data.called, "i2c_del_master_bus should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");
}

// Test gpio_expander_init with GPIO expander object creation failure
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

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_new_master_bus_data.called, "i2c_new_master_bus should be called once");
    validate_i2c_new_master_bus_config();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_I2C_MASTER_BUS_HANDLE, mock_esp_io_expander_bus, "I2C bus handle should be passed to expander creation");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000,
        mock_esp_io_expander_addr,
        "Device address should be ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should not be called"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_vSemaphoreDelete_called, "vSemaphoreDelete should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_SEMAPHORE_HANDLE_T, mock_vSemaphoreDelete_Handle, "vSemaphoreDelete should be called with correct handle");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_del_master_bus_data.called, "i2c_del_master_bus should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_I2C_MASTER_BUS_HANDLE,
        mock_i2c_del_master_bus_data.bus_handle,
        "i2c_del_master_bus should be called with correct handle"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");
}

// Test gpio_expander_init with GPIO expander print state failure
void test_gpio_expander_init_print_state_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_init - print state failure");
    LOG_MESSAGE();

    mock_esp_io_expander_print_state_data.should_fail = true;

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_init should return ESP_FAIL when print state fails");
    TEST_ASSERT_NULL_MESSAGE(handle, "Handle should be unchanged when init fails");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_new_master_bus_data.called, "i2c_new_master_bus should be called once");
    validate_i2c_new_master_bus_config();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_io_expander_called, "esp_io_expander_new_i2c_tca95xx_16bit should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_I2C_MASTER_BUS_HANDLE,
        mock_esp_io_expander_bus,
        "I2C bus handle should be passed to expander creation"
    );
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000,
        mock_esp_io_expander_addr,
        "Device address should be ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_EXPANDER_HANDLE,
        mock_esp_io_expander_print_state_data.handle,
        "Handle passed to print_state should match the expander handle"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_vSemaphoreDelete_called, "vSemaphoreDelete should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MOCK_SEMAPHORE_HANDLE_T, mock_vSemaphoreDelete_Handle, "vSemaphoreDelete should be called with correct handle");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_i2c_del_master_bus_data.called, "i2c_del_master_bus should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_I2C_MASTER_BUS_HANDLE,
        mock_i2c_del_master_bus_data.bus_handle,
        "i2c_del_master_bus should be called with correct handle"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_io_expander_del_data.called, "esp_io_expander_del should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_EXPANDER_HANDLE,
        mock_esp_io_expander_del_data.handle,
        "esp_io_expander_del should be called with correct handle"
    );
}

// Validate execution of the gpio_expander_set() function
void validate_gpio_expander_set_dir_run(uint32_t pin_num_mask, esp_io_expander_dir_t direction, bool validate_unnecessary_calls)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xSemaphoreTake_called, "xSemaphoreTake should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_SEMAPHORE_HANDLE_T,
        mock_xSemaphoreTake_Handle,
        "xSemaphoreTake should be called with correct handle"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        portMAX_DELAY,
        mock_xSemaphoreTake_xTicksToWait,
        "xSemaphoreTake should be called with portMAX_DELAY timeout value"
    );

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_esp_io_expander_set_dir_data.called, "esp_io_expander_set_dir should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_EXPANDER_HANDLE,
        mock_esp_io_expander_set_dir_data.handle,
        "esp_io_expander_set_dir should be called with correct handle"
    );
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        pin_num_mask,
        mock_esp_io_expander_set_dir_data.masks[0],
        "esp_io_expander_set_dir should be called with provided pin_num_mask value"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        direction,
        mock_esp_io_expander_set_dir_data.directions[0],
        "esp_io_expander_set_dir should be called with provided direction value"
    );

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xSemaphoreGive_called, "xSemaphoreGive should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_SEMAPHORE_HANDLE_T,
        mock_xSemaphoreGive_Handle,
        "xSemaphoreGive should be called with correct handle"
    );

    if (validate_unnecessary_calls) {
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should NOT be called");
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_level_data.called, "esp_io_expander_set_level should NOT be called");
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_get_level_data.called, "esp_io_expander_get_level should NOT be called");
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");
    }

    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        mock_xSemaphoreTake_call_seq,
        mock_esp_io_expander_set_dir_data.call_seq,
        "esp_io_expander_set_dir should be called after xSemaphoreTake"
    );
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        mock_esp_io_expander_set_dir_data.call_seq,
        mock_xSemaphoreGive_call_seq,
        "xSemaphoreGive should be called after esp_io_expander_set_dir"
    );
}

// Test successful execution of gpio_expander_set_dir()
void test_gpio_expander_set_dir_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_set_dir - success case");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);
    validate_gpio_expander_init_success(result, handle);

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    uint32_t pin_num_mask = 0x43218765;
    esp_io_expander_dir_t direction = IO_EXPANDER_INPUT;
    LOG_INFO("Testing with pin_num_mask = 0x%08" PRIX32 ", direction = IO_EXPANDER_INPUT", pin_num_mask);

    result = gpio_expander_set_dir(pin_num_mask, direction);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_set_dir should return ESP_OK");
    validate_gpio_expander_set_dir_run(pin_num_mask, direction, true);

    // Second test with another values

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    pin_num_mask = 0x98765432;
    direction = IO_EXPANDER_OUTPUT;
    LOG_INFO("Testing with pin_num_mask = 0x%08" PRIX32 ", direction = IO_EXPANDER_OUTPUT", pin_num_mask);

    result = gpio_expander_set_dir(pin_num_mask, direction);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_set_dir should return ESP_OK");
    validate_gpio_expander_set_dir_run(pin_num_mask, direction, true);
}

// Test gpio_expander_set_dir() when esp_io_expander_set_dir() fails
void test_gpio_expander_set_dir_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_set_dir - esp_io_expander_set_dir fail");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);
    validate_gpio_expander_init_success(result, handle);

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    mock_esp_io_expander_set_dir_data.should_fail = true;

    uint32_t pin_num_mask = 0xABCD1234;
    esp_io_expander_dir_t direction = IO_EXPANDER_INPUT;

    result = gpio_expander_set_dir(pin_num_mask, direction);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_set_dir should return ESP_FAIL");
    validate_gpio_expander_set_dir_run(pin_num_mask, direction, true);
}

// Test gpio_expander_set_dir() without module initialization
void test_gpio_expander_set_dir_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_set_dir - expander not initialized");
    LOG_MESSAGE();

    int32_t pin_num_mask = 0x43218765;
    esp_io_expander_dir_t direction = IO_EXPANDER_INPUT;

    esp_err_t result = gpio_expander_set_dir(pin_num_mask, direction);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_set_dir should return ESP_FAIL");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreTake_called, "xSemaphoreTake should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_dir_data.called, "esp_io_expander_set_dir should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreGive_called, "xSemaphoreGive should NOT be called");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_level_data.called, "esp_io_expander_set_level should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_get_level_data.called, "esp_io_expander_get_level should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");
}

// Test gpio_expander_set_dir() with partial initialization (mutex NULL)
void test_gpio_expander_set_dir_partial_init_mutex_null(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_set_dir - partial init (mutex NULL)");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);
    validate_gpio_expander_init_success(result, handle);

    gpio_expander_test_set_mutex_null();
    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    result = gpio_expander_set_dir(0x43218765, IO_EXPANDER_INPUT);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "should return ESP_FAIL when mutex is NULL");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreTake_called, "xSemaphoreTake should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_dir_data.called, "set_dir should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreGive_called, "xSemaphoreGive should NOT be called");
}

// Validate execution of the gpio_expander_set_level() function
void validate_gpio_expander_set_level_run(uint32_t pin_num_mask, uint8_t level, bool validate_unnecessary_calls)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xSemaphoreTake_called, "xSemaphoreTake should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_SEMAPHORE_HANDLE_T,
        mock_xSemaphoreTake_Handle,
        "xSemaphoreTake should be called with correct handle"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        portMAX_DELAY,
        mock_xSemaphoreTake_xTicksToWait,
        "xSemaphoreTake should be called with portMAX_DELAY timeout value"
    );

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_esp_io_expander_set_level_data.called, "esp_io_expander_set_level should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_EXPANDER_HANDLE,
        mock_esp_io_expander_set_level_data.handle,
        "esp_io_expander_set_level should be called with correct handle"
    );
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        pin_num_mask,
        mock_esp_io_expander_set_level_data.masks[0],
        "esp_io_expander_set_level should be called with provided pin_num_mask value"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        level,
        mock_esp_io_expander_set_level_data.levels[0],
        "esp_io_expander_set_level should be called with provided level value"
    );

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xSemaphoreGive_called, "xSemaphoreGive should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_SEMAPHORE_HANDLE_T,
        mock_xSemaphoreGive_Handle,
        "xSemaphoreGive should be called with correct handle"
    );

    if (validate_unnecessary_calls) {
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should NOT be called");
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_dir_data.called, "esp_io_expander_set_dir should NOT be called");
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_get_level_data.called, "esp_io_expander_get_level should NOT be called");
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");
    }

    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        mock_xSemaphoreTake_call_seq,
        mock_esp_io_expander_set_level_data.call_seq,
        "esp_io_expander_set_level should be called after xSemaphoreTake"
    );
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        mock_esp_io_expander_set_level_data.call_seq,
        mock_xSemaphoreGive_call_seq,
        "xSemaphoreGive should be called after esp_io_expander_set_level"
    );
}

// Test successful execution of gpio_expander_set_level()
void test_gpio_expander_set_level_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_set_level - success case");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);
    validate_gpio_expander_init_success(result, handle);

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    uint32_t pin_num_mask = 0xABC123D4;
    uint8_t level = 1;
    LOG_INFO("Testing with pin_num_mask = 0x%08" PRIX32 ", level = %" PRIu8, pin_num_mask, level);

    result = gpio_expander_set_level(pin_num_mask, level);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_set_level should return ESP_OK");
    validate_gpio_expander_set_level_run(pin_num_mask, level, true);

    // Second test with another values

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    pin_num_mask = 0xBDAC5137;
    level = 0;
    LOG_INFO("Testing with pin_num_mask = 0x%08" PRIX32 ", level = %" PRIu8, pin_num_mask, level);

    result = gpio_expander_set_level(pin_num_mask, level);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_set_level should return ESP_OK");
    validate_gpio_expander_set_level_run(pin_num_mask, level, true);
}

// Test gpio_expander_set_level() when esp_io_expander_set_level() fails
void test_gpio_expander_set_level_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_set_level - esp_io_expander_set_level fail");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);
    validate_gpio_expander_init_success(result, handle);

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    mock_esp_io_expander_set_level_data.should_fail = true;

    uint32_t pin_num_mask = 0xBAFA4231;
    uint8_t level = 1;

    result = gpio_expander_set_level(pin_num_mask, level);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_set_level should return ESP_FAIL");
    validate_gpio_expander_set_level_run(pin_num_mask, level, true);
}

// Test gpio_expander_set_level() without module initialization
void test_gpio_expander_set_level_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_set_level - expander not initialized");
    LOG_MESSAGE();

    uint32_t pin_num_mask = 0xABC123D4;
    uint8_t level = 1;

    esp_err_t result = gpio_expander_set_level(pin_num_mask, level);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_set_level should return ESP_FAIL");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreTake_called, "xSemaphoreTake should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_level_data.called, "esp_io_expander_set_level should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreGive_called, "xSemaphoreGive should NOT be called");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_dir_data.called, "esp_io_expander_set_dir should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_get_level_data.called, "esp_io_expander_get_level should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");
}

// Validate execution of the gpio_expander_set_out_dir_and_level() function
void validate_gpio_expander_set_out_dir_and_level_run(uint32_t pin_num_mask, uint8_t level)
{
    validate_gpio_expander_set_dir_run(pin_num_mask, IO_EXPANDER_OUTPUT, false);
    validate_gpio_expander_set_level_run(pin_num_mask, level, false);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_get_level_data.called, "esp_io_expander_get_level should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");

    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        mock_esp_io_expander_set_dir_data.call_seq,
        mock_esp_io_expander_set_level_data.call_seq,
        "esp_io_expander_set_level should be called after esp_io_expander_set_dir"
    );
}

// Test successful execution of gpio_expander_set_out_dir_and_level()
void test_gpio_expander_set_out_dir_and_level_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_set_out_dir_and_level - success case");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);
    validate_gpio_expander_init_success(result, handle);

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    uint32_t pin_num_mask = 0x43218765;
    uint8_t level = 1;
    LOG_INFO("Testing with pin_num_mask = 0x%08" PRIX32 ", level = %" PRIu8, pin_num_mask, level);

    result = gpio_expander_set_out_dir_and_level(pin_num_mask, level);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_set_out_dir_and_level should return ESP_OK");
    validate_gpio_expander_set_out_dir_and_level_run(pin_num_mask, level);

    // Second test with another values

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    pin_num_mask = 0xBDAC5137;
    level = 0;
    LOG_INFO("Testing with pin_num_mask = 0x%08" PRIX32 ", level = %" PRIu8, pin_num_mask, level);

    result = gpio_expander_set_out_dir_and_level(pin_num_mask, level);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_set_out_dir_and_level should return ESP_OK");
    validate_gpio_expander_set_out_dir_and_level_run(pin_num_mask, level);
}

// Test gpio_expander_set_out_dir_and_level() with esp_io_expander_set_dir() failure
void test_gpio_expander_set_out_dir_and_level_fail_set_dir(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_set_out_dir_and_level - esp_io_expander_set_dir fail");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);
    validate_gpio_expander_init_success(result, handle);

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    mock_esp_io_expander_set_dir_data.should_fail = true;

    uint32_t pin_num_mask = 0xBAFA4231;
    uint8_t level = 1;

    result = gpio_expander_set_out_dir_and_level(pin_num_mask, level);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_set_out_dir_and_level should return ESP_FAIL");
    validate_gpio_expander_set_dir_run(pin_num_mask, level, true);
}

// Test gpio_expander_set_out_dir_and_level() with esp_io_expander_set_level() failure
void test_gpio_expander_set_out_dir_and_level_fail_set_level(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_set_out_dir_and_level - esp_io_expander_set_level fail");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);
    validate_gpio_expander_init_success(result, handle);

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    mock_esp_io_expander_set_level_data.should_fail = true;

    uint32_t pin_num_mask = 0xBAFA4231;
    uint8_t level = 1;

    result = gpio_expander_set_out_dir_and_level(pin_num_mask, level);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_set_out_dir_and_level should return ESP_FAIL");
    validate_gpio_expander_set_out_dir_and_level_run(pin_num_mask, level);
}

// Test gpio_expander_set_out_dir_and_level() without module initialization
void test_gpio_expander_set_out_dir_and_level_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_set_out_dir_and_level - expander not initialized");
    LOG_MESSAGE();

    uint32_t pin_num_mask = 0x43218765;
    uint8_t level = 1;

    esp_err_t result = gpio_expander_set_out_dir_and_level(pin_num_mask, level);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_set_out_dir_and_level should return ESP_FAIL");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreTake_called, "xSemaphoreTake should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_level_data.called, "esp_io_expander_set_level should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreGive_called, "xSemaphoreGive should NOT be called");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_dir_data.called, "esp_io_expander_set_dir should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_get_level_data.called, "esp_io_expander_get_level should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");
}

// Validate execution of the gpio_expander_get_level() function
void validate_gpio_expander_get_level_run(uint32_t pin_num_mask)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xSemaphoreTake_called, "xSemaphoreTake should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_SEMAPHORE_HANDLE_T,
        mock_xSemaphoreTake_Handle,
        "xSemaphoreTake should be called with correct handle"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        portMAX_DELAY,
        mock_xSemaphoreTake_xTicksToWait,
        "xSemaphoreTake should be called with portMAX_DELAY timeout value"
    );

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_esp_io_expander_get_level_data.called, "esp_io_expander_get_level should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_EXPANDER_HANDLE,
        mock_esp_io_expander_get_level_data.handle,
        "esp_io_expander_get_level should be called with correct handle"
    );
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        pin_num_mask,
        mock_esp_io_expander_get_level_data.masks[0],
        "esp_io_expander_get_level should be called with provided pin_num_mask value"
    );

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xSemaphoreGive_called, "xSemaphoreGive should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_SEMAPHORE_HANDLE_T,
        mock_xSemaphoreGive_Handle,
        "xSemaphoreGive should be called with correct handle"
    );

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_dir_data.called, "esp_io_expander_set_dir should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_level_data.called, "esp_io_expander_set_level should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");

    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        mock_xSemaphoreTake_call_seq,
        mock_esp_io_expander_get_level_data.call_seq,
        "esp_io_expander_get_level should be called after xSemaphoreTake"
    );
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        mock_esp_io_expander_get_level_data.call_seq,
        mock_xSemaphoreGive_call_seq,
        "xSemaphoreGive should be called after esp_io_expander_get_level"
    );
}

// Test successful execution of gpio_expander_get_level()
void test_gpio_expander_get_level_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_get_level - success case");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);
    validate_gpio_expander_init_success(result, handle);

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    uint32_t pin_num_mask = 0xA55A5AA5;
    uint32_t level_mask = 0xABCDEF123 & pin_num_mask;
    LOG_INFO("Testing with pin_num_mask = 0x%08" PRIX32 ", level_mask = 0x%08" PRIX32, pin_num_mask, level_mask);

    mock_esp_io_expander_get_level_data.levels_setup = level_mask;

    uint32_t result_level_mask = 0;
    result = gpio_expander_get_level(pin_num_mask, &result_level_mask);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_get_level should return ESP_OK");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        level_mask,
        result_level_mask,
        "esp_io_expander_get_level should return correct level_mask value"
    );
    validate_gpio_expander_get_level_run(pin_num_mask);

    // Second test with another values

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    pin_num_mask = 0xAFAF55AA;
    level_mask = 0x123ABCDEF & pin_num_mask;
    LOG_INFO("Testing with pin_num_mask = 0x%08" PRIX32 ", level_mask = 0x%08" PRIX32, pin_num_mask, level_mask);

    mock_esp_io_expander_get_level_data.levels_setup = level_mask;

    result_level_mask = 0;
    result = gpio_expander_get_level(pin_num_mask, &result_level_mask);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "gpio_expander_get_level should return ESP_OK");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        level_mask,
        result_level_mask,
        "esp_io_expander_get_level should return correct level_mask value"
    );
    validate_gpio_expander_get_level_run(pin_num_mask);
}

// Test gpio_expander_get_level() when esp_io_expander_get_level() fails
void test_gpio_expander_get_level_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_get_level - success case");
    LOG_MESSAGE();

    esp_io_expander_handle_t handle = NULL;
    esp_err_t result = gpio_expander_init(&handle);
    validate_gpio_expander_init_success(result, handle);

    mock_esp_io_expander_reset();
    mock_freertos_semaphore_reset();

    uint32_t pin_num_mask = 0xA55A5AA5;

    mock_esp_io_expander_get_level_data.should_fail = true;

    uint32_t result_level_mask = 0;
    result = gpio_expander_get_level(pin_num_mask, &result_level_mask);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_get_level should return ESP_FAIL");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        0,
        result_level_mask,
        "result level_mask should be unchanged"
    );
    validate_gpio_expander_get_level_run(pin_num_mask);
}

// Test gpio_expander_get_level() without module initialization
void test_gpio_expander_get_level_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test gpio_expander_get_level - expander not initialized");
    LOG_MESSAGE();

    uint32_t pin_num_mask = 0xA55A5AA5;

    uint32_t result_level_mask = 0;
    esp_err_t result = gpio_expander_get_level(pin_num_mask, &result_level_mask);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "gpio_expander_get_level should return ESP_FAIL");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        0,
        result_level_mask,
        "result level_mask should be unchanged"
    );

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreTake_called, "xSemaphoreTake should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_get_level_data.called, "esp_io_expander_get_level should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreGive_called, "xSemaphoreGive should NOT be called");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_print_state_data.called, "esp_io_expander_print_state should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_dir_data.called, "esp_io_expander_set_dir should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_set_level_data.called, "esp_io_expander_set_level should NOT be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_esp_io_expander_del_data.called, "esp_io_expander_del should NOT be called");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_gpio_expander_init_success);
    RUN_TEST(test_gpio_expander_init_reinit_success);
    RUN_TEST(test_gpio_expander_init_null_handle);
    RUN_TEST(test_gpio_expander_init_mutex_failure);
    RUN_TEST(test_gpio_expander_init_i2c_bus_failure);
    RUN_TEST(test_gpio_expander_init_expander_creation_failure);
    RUN_TEST(test_gpio_expander_init_print_state_failure);

    RUN_TEST(test_gpio_expander_set_dir_success);
    RUN_TEST(test_gpio_expander_set_dir_fail);
    RUN_TEST(test_gpio_expander_set_dir_not_initialized);
    RUN_TEST(test_gpio_expander_set_dir_partial_init_mutex_null);

    RUN_TEST(test_gpio_expander_set_level_success);
    RUN_TEST(test_gpio_expander_set_level_fail);
    RUN_TEST(test_gpio_expander_set_level_not_initialized);

    RUN_TEST(test_gpio_expander_set_out_dir_and_level_success);
    RUN_TEST(test_gpio_expander_set_out_dir_and_level_fail_set_dir);
    RUN_TEST(test_gpio_expander_set_out_dir_and_level_fail_set_level);
    RUN_TEST(test_gpio_expander_set_out_dir_and_level_not_initialized);

    RUN_TEST(test_gpio_expander_get_level_success);
    RUN_TEST(test_gpio_expander_get_level_fail);
    RUN_TEST(test_gpio_expander_get_level_not_initialized);

    return UNITY_END();
}
