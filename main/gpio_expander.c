#include "esp_io_expander_tca95xx_16bit.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "debug_log.h"


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
};

static esp_io_expander_handle_t gpio_expander = NULL;


esp_err_t gpio_expander_init(esp_io_expander_handle_t* handle)
{
    if (gpio_expander) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (i2c_new_master_bus(&bus_config, &i2c_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to allocate I2C master bus");
        return ESP_FAIL;
    }
    if (esp_io_expander_new_i2c_tca95xx_16bit(i2c_handle, GPIO_EXPANDER_ADDR, &gpio_expander) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to create GPIO expander object");
        return ESP_FAIL;
    }

    if (debug_log_is_enabled(TAG)) {
        if (esp_io_expander_print_state(gpio_expander) != ESP_OK) {
            ESP_LOGE(TAG, "Unable to print GPIO expander state");
            return ESP_FAIL;
        }
    }

    if (handle) {
        *handle = gpio_expander;
    }

    ESP_LOGI(TAG, "GPIO expander initialized");
    return ESP_OK;
}
