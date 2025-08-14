#include "mio_control.h"
#include "esp_log.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "mio_control";
static esp_io_expander_handle_t io_expander = NULL;

#define MIO_RESET_PIN   IO_EXPANDER_PIN_NUM_8 // P10 = IO1.0 = IO_EXPANDER_PIN_NUM_8

void mio_control_init(esp_io_expander_handle_t io_expander_handle)
{
    // Initialize the IO expander
    if (io_expander_handle == NULL) {
        ESP_LOGE(TAG, "IO expander is not initialized");
        return;
    }

    io_expander = io_expander_handle;

    esp_io_expander_set_dir(io_expander, MIO_RESET_PIN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, MIO_RESET_PIN, 0);
}

void mio_control_io_bus_onoff(bool enabled)
{
    if (io_expander == NULL) {
        ESP_LOGE(TAG, "IO expander is not initialized");
        return;
    }

    if (enabled) {
        esp_io_expander_set_level(io_expander, MIO_RESET_PIN, 1);
        ESP_LOGI(TAG, "IO bus enabled");
    } else {
        esp_io_expander_set_level(io_expander, MIO_RESET_PIN, 0);
        ESP_LOGI(TAG, "IO bus disabled");
    }
}
