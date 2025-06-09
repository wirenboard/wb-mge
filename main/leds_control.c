#include "leds_control.h"

#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_log.h"

#define ETHERNET_LED_PIN    IO_EXPANDER_PIN_NUM_4
#define WIFI_LED_PIN        IO_EXPANDER_PIN_NUM_5
#define UNKNOWN_LED_PIN     IO_EXPANDER_PIN_NUM_7

static const char *TAG = "leds_control";

static esp_io_expander_handle_t io_expander = NULL;

void leds_control_init(esp_io_expander_handle_t io_expander_handle)
{
    if (io_expander_handle == NULL) {
        ESP_LOGE(TAG, "IO expander handle is NULL");
        return;
    }

    esp_io_expander_handle_t io_expander = io_expander_handle;

    esp_io_expander_set_dir(io_expander, ETHERNET_LED_PIN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, ETHERNET_LED_PIN, 0);

    esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_5, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_5, 0);

    esp_io_expander_set_dir(io_expander, UNKNOWN_LED_PIN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, UNKNOWN_LED_PIN, 0);
}

void leds_control_set_eth_led(bool on)
{
    if (io_expander == NULL) {
        ESP_LOGE(TAG, "IO expander handle is NULL");
        return;
    }

    esp_io_expander_set_level(io_expander, ETHERNET_LED_PIN, on ? 1 : 0);
}

void leds_control_set_wifi_led(bool on)
{
    if (io_expander == NULL) {
        ESP_LOGE(TAG, "IO expander handle is NULL");
        return;
    }

    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_5, on ? 1 : 0);
}

void leds_control_set_unknown_led(bool on)
{
    if (io_expander == NULL) {
        ESP_LOGE(TAG, "IO expander handle is NULL");
        return;
    }

    esp_io_expander_set_level(io_expander, UNKNOWN_LED_PIN, on ? 1 : 0);
}
