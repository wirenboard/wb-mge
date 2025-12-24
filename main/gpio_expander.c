#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_log.h"
#include "debug_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define GPIO_EXPANDER_I2C_PORT      I2C_NUM_0
#define GPIO_EXPANDER_SDA_PIN       GPIO_NUM_32
#define GPIO_EXPANDER_SCL_PIN       GPIO_NUM_33
#define GPIO_EXPANDER_ADDR          ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000

static const char *TAG = "gpio_expander";

static i2c_master_bus_handle_t i2c_handle = NULL;
static const i2c_master_bus_config_t bus_config = {
    .i2c_port = GPIO_EXPANDER_I2C_PORT,
    .sda_io_num = GPIO_EXPANDER_SDA_PIN,
    .scl_io_num = GPIO_EXPANDER_SCL_PIN,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .flags.enable_internal_pullup = 1
};

static esp_io_expander_handle_t gpio_expander = NULL;
static SemaphoreHandle_t gpio_expander_mutex = NULL;

esp_err_t gpio_expander_init(esp_io_expander_handle_t* handle)
{
    if (gpio_expander) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    gpio_expander_mutex = xSemaphoreCreateMutex();
    if (gpio_expander_mutex == NULL) {
        ESP_LOGE(TAG, "Unable to create GPIO expander mutex");
        return ESP_FAIL;
    }

    if (i2c_new_master_bus(&bus_config, &i2c_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to allocate I2C master bus");
        vSemaphoreDelete(gpio_expander_mutex);
        gpio_expander_mutex = NULL;
        return ESP_FAIL;
    }
    if (esp_io_expander_new_i2c_tca95xx_16bit(i2c_handle, GPIO_EXPANDER_ADDR, &gpio_expander) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to create GPIO expander object");
        i2c_del_master_bus(i2c_handle);
        i2c_handle = NULL;
        vSemaphoreDelete(gpio_expander_mutex);
        gpio_expander_mutex = NULL;
        return ESP_FAIL;
    }

    if (debug_log_is_enabled(TAG)) {
        if (esp_io_expander_print_state(gpio_expander) != ESP_OK) {
            ESP_LOGE(TAG, "Unable to print GPIO expander state");
            esp_io_expander_del(gpio_expander);
            gpio_expander = NULL;
            i2c_del_master_bus(i2c_handle);
            i2c_handle = NULL;
            vSemaphoreDelete(gpio_expander_mutex);
            gpio_expander_mutex = NULL;
            return ESP_FAIL;
        }
    }

    if (handle != NULL) {
        *handle = gpio_expander;
    }

    ESP_LOGI(TAG, "GPIO expander initialized");
    return ESP_OK;
}

esp_err_t gpio_expander_set_dir(uint32_t pin_num_mask, esp_io_expander_dir_t direction)
{
    if ((gpio_expander == NULL) || (gpio_expander_mutex == NULL)) {
        ESP_LOGE(TAG, "GPIO expander is not initialized");
        return ESP_FAIL;
    }

    xSemaphoreTake(gpio_expander_mutex, portMAX_DELAY);
    esp_err_t ret = esp_io_expander_set_dir(gpio_expander, pin_num_mask, direction);
    xSemaphoreGive(gpio_expander_mutex);

    return ret;
}

esp_err_t gpio_expander_set_level(uint32_t pin_num_mask, uint8_t level)
{
    if ((gpio_expander == NULL) || (gpio_expander_mutex == NULL)) {
        ESP_LOGE(TAG, "GPIO expander is not initialized");
        return ESP_FAIL;
    }

    xSemaphoreTake(gpio_expander_mutex, portMAX_DELAY);
    esp_err_t ret = esp_io_expander_set_level(gpio_expander, pin_num_mask, level);
    xSemaphoreGive(gpio_expander_mutex);

    return ret;
}

esp_err_t gpio_expander_set_out_dir_and_level(uint32_t pin_num_mask, uint8_t level)
{
    if ((gpio_expander == NULL) || (gpio_expander_mutex == NULL)) {
        ESP_LOGE(TAG, "GPIO expander is not initialized");
        return ESP_FAIL;
    }

    xSemaphoreTake(gpio_expander_mutex, portMAX_DELAY);
    esp_err_t ret = esp_io_expander_set_dir(gpio_expander, pin_num_mask, IO_EXPANDER_OUTPUT);
    if (ret == ESP_OK) {
        ret = esp_io_expander_set_level(gpio_expander, pin_num_mask, level);
    }
    xSemaphoreGive(gpio_expander_mutex);

    return ret;
}

esp_err_t gpio_expander_get_level(uint32_t pin_num_mask, uint32_t *level_mask)
{
    if ((gpio_expander == NULL) || (gpio_expander_mutex == NULL)) {
        ESP_LOGE(TAG, "GPIO expander is not initialized");
        return ESP_FAIL;
    }

    xSemaphoreTake(gpio_expander_mutex, portMAX_DELAY);
    esp_err_t ret = esp_io_expander_get_level(gpio_expander, pin_num_mask, level_mask);
    xSemaphoreGive(gpio_expander_mutex);

    return ret;
}

#ifdef __unittest_env__
    void gpio_expander_test_reset(void)
    {
        gpio_expander = NULL;
        i2c_handle = NULL;
        gpio_expander_mutex = NULL;
    }
#endif
