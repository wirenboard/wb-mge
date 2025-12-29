#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "gpio_expander.h"

static const char *TAG = "mio_control";

#define MIO_RESET_PIN   IO_EXPANDER_PIN_NUM_8 // P10 = IO1.0 = IO_EXPANDER_PIN_NUM_8

esp_err_t mio_control_init(void)
{
    esp_err_t ret = gpio_expander_set_out_dir_and_level(MIO_RESET_PIN, 0);
    return ret;
}

esp_err_t mio_control_io_bus_onoff(bool enabled)
{
    esp_err_t ret = ESP_FAIL;

    if (enabled) {
        ret = gpio_expander_set_level(MIO_RESET_PIN, 1);
        ESP_LOGD(TAG, "IO bus enabled");
    } else {
        ret = gpio_expander_set_level(MIO_RESET_PIN, 0);
        ESP_LOGD(TAG, "IO bus disabled");
    }

    return ret;
}
