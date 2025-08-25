#include "leds_control.h"

#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_log.h"


#define ETHERNET_LED_PIN            IO_EXPANDER_PIN_NUM_5
#define WIFI_LED_PIN                IO_EXPANDER_PIN_NUM_4
#define STATUS_LED_PIN              IO_EXPANDER_PIN_NUM_7

#define ETHERNET_LED_INVERSION      1
#define WIFI_LED_INVERSION          1
#define STATUS_LED_INVERSION        0


static const char *TAG = "leds_control";

static esp_io_expander_handle_t io_expander = NULL;


void leds_control_init(esp_io_expander_handle_t io_expander_handle)
{
    if (io_expander_handle == NULL) {
        ESP_LOGE(TAG, "IO expander handle is NULL");
        return;
    }

    io_expander = io_expander_handle;

    esp_io_expander_set_dir(io_expander, ETHERNET_LED_PIN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, ETHERNET_LED_PIN, ETHERNET_LED_INVERSION);

    esp_io_expander_set_dir(io_expander, WIFI_LED_PIN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, WIFI_LED_PIN, WIFI_LED_INVERSION);

    esp_io_expander_set_dir(io_expander, STATUS_LED_PIN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, STATUS_LED_PIN, STATUS_LED_INVERSION);
}

void leds_control_set_eth_led(bool on)
{
    if (io_expander == NULL) {
        ESP_LOGE(TAG, "IO expander handle is NULL");
        return;
    }

    #if (ETHERNET_LED_INVERSION)
        esp_io_expander_set_level(io_expander, ETHERNET_LED_PIN, !on);
    #else
        esp_io_expander_set_level(io_expander, ETHERNET_LED_PIN, on);
    #endif
}

void leds_control_set_wifi_led(bool on)
{
    if (io_expander == NULL) {
        ESP_LOGE(TAG, "IO expander handle is NULL");
        return;
    }

    #if (WIFI_LED_INVERSION)
        esp_io_expander_set_level(io_expander, WIFI_LED_PIN, !on);
    #else
        esp_io_expander_set_level(io_expander, WIFI_LED_PIN, on);
    #endif
}

void leds_control_set_status_led(bool on)
{
    if (io_expander == NULL) {
        ESP_LOGE(TAG, "IO expander handle is NULL");
        return;
    }

    #if (STATUS_LED_INVERSION)
        esp_io_expander_set_level(io_expander, STATUS_LED_PIN, !on);
    #else
        esp_io_expander_set_level(io_expander, STATUS_LED_PIN, on);
    #endif
}
