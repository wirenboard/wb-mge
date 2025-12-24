#include "gpio_expander.h"
#include "esp_log.h"

#define ETHERNET_LED_PIN            IO_EXPANDER_PIN_NUM_5
#define WIFI_LED_PIN                IO_EXPANDER_PIN_NUM_4
#define STATUS_LED_PIN              IO_EXPANDER_PIN_NUM_7

#define ETHERNET_LED_INVERSION      1
#define WIFI_LED_INVERSION          1
#define STATUS_LED_INVERSION        0

static const char *TAG = "leds_control";

static esp_err_t last_error(esp_err_t ret, esp_err_t err)
{
    if (err != ESP_OK) {
        ret = err;
    }
    return ret;
}

esp_err_t leds_control_init(void)
{
    // Don't fail on first error, try to do all initialization steps
    esp_err_t ret = gpio_expander_set_out_dir_and_level(ETHERNET_LED_PIN, ETHERNET_LED_INVERSION);

    esp_err_t err = gpio_expander_set_out_dir_and_level(WIFI_LED_PIN, WIFI_LED_INVERSION);
    ret = last_error(ret, err);

    err = gpio_expander_set_out_dir_and_level(STATUS_LED_PIN, STATUS_LED_INVERSION);
    ret = last_error(ret, err);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "At least one initialization step failed");
    }

    return ret;
}

esp_err_t leds_control_set_eth_led(bool on)
{
    #if (ETHERNET_LED_INVERSION)
        esp_err_t ret = gpio_expander_set_level(ETHERNET_LED_PIN, !on);
    #else
        esp_err_t ret = gpio_expander_set_level(ETHERNET_LED_PIN, on);
    #endif

    return ret;
}

esp_err_t leds_control_set_wifi_led(bool on)
{
    #if (WIFI_LED_INVERSION)
        esp_err_t ret = gpio_expander_set_level(WIFI_LED_PIN, !on);
    #else
        esp_err_t ret = gpio_expander_set_level(WIFI_LED_PIN, on);
    #endif

    return ret;
}

esp_err_t leds_control_set_status_led(bool on)
{
    #if (STATUS_LED_INVERSION)
        esp_err_t ret = gpio_expander_set_level(STATUS_LED_PIN, !on);
    #else
        esp_err_t ret = gpio_expander_set_level(STATUS_LED_PIN, on);
    #endif

    return ret;
}
