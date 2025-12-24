#include "rs485_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gpio_expander.h"

static struct {
    bool enabled;
    bool allowed;
} rs485_bus_vout_ctrl = {
    .enabled = false,
    .allowed = true
};

static SemaphoreHandle_t rs485_bus_vout_mutex = NULL;

static const char *TAG = "rs485_control";

#define TERM485_1_PIN        IO_EXPANDER_PIN_NUM_0
#define TERM485_2_PIN        IO_EXPANDER_PIN_NUM_1
#define FAILSAFE_485_1_PIN   IO_EXPANDER_PIN_NUM_2
#define FAILSAFE_485_2_PIN   IO_EXPANDER_PIN_NUM_3
#define VOUT_485_PIN         IO_EXPANDER_PIN_NUM_6

esp_err_t rs485_term_on_off(rs485_port_t port, bool on)
{
    esp_err_t ret = ESP_FAIL;

    if (port == RS485_1) {
        ret = gpio_expander_set_level(TERM485_1_PIN, on);
    } else if (port == RS485_2) {
        ret = gpio_expander_set_level(TERM485_2_PIN, on);
    } else {
        ESP_LOGE(TAG, "Invalid RS485 port number: %d", port);
        ret = ESP_FAIL;
    }

    return ret;
}

esp_err_t rs485_pupd_on_off(rs485_port_t port, bool on)
{
    esp_err_t ret = ESP_FAIL;

    if (port == RS485_1) {
        ret = gpio_expander_set_level(FAILSAFE_485_1_PIN, on);
    } else if (port == RS485_2) {
        ret = gpio_expander_set_level(FAILSAFE_485_2_PIN, on);
    } else {
        ESP_LOGE(TAG, "Invalid RS485 port number: %d", port);
        ret = ESP_FAIL;
    }

    return ret;
}

esp_err_t rs485_bus_vout_on_off(bool on)
{
    if (rs485_bus_vout_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex not initialized");
        return ESP_FAIL;
    }

    xSemaphoreTake(rs485_bus_vout_mutex, portMAX_DELAY);
    rs485_bus_vout_ctrl.enabled = on;

    bool out_level = false;
    if (rs485_bus_vout_ctrl.enabled == true) {
        if (rs485_bus_vout_ctrl.allowed == true) {
            out_level = true;
        }
    }

    esp_err_t ret = gpio_expander_set_level(VOUT_485_PIN, out_level);
    xSemaphoreGive(rs485_bus_vout_mutex);

    return ret;
}

esp_err_t rs485_bus_vout_set_allowed(bool allowed)
{
    if (rs485_bus_vout_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex not initialized");
        return ESP_FAIL;
    }

    xSemaphoreTake(rs485_bus_vout_mutex, portMAX_DELAY);
    rs485_bus_vout_ctrl.allowed = allowed;

    bool out_level = false;
    if (rs485_bus_vout_ctrl.enabled == true) {
        if (rs485_bus_vout_ctrl.allowed == true) {
            out_level = true;
        }
    }

    esp_err_t ret = gpio_expander_set_level(VOUT_485_PIN, out_level);
    xSemaphoreGive(rs485_bus_vout_mutex);

    return ret;
}

static esp_err_t last_error(esp_err_t ret, esp_err_t err)
{
    if (err != ESP_OK) {
        ret = err;
    }
    return ret;
}

esp_err_t rs485_control_init(void)
{
    if (rs485_bus_vout_mutex == NULL) {
        rs485_bus_vout_mutex = xSemaphoreCreateMutex();
        if (rs485_bus_vout_mutex == NULL) {
            ESP_LOGW(TAG, "Failed to initialize RS-485 bus Vout mutex");
            return ESP_FAIL;
        }
    }

    // Don't fail on first error, try to do all initialization steps
    esp_err_t ret = gpio_expander_set_out_dir_and_level(VOUT_485_PIN, 0);

    esp_err_t err = gpio_expander_set_out_dir_and_level(TERM485_1_PIN, 0);
    ret = last_error(ret, err);

    err = gpio_expander_set_out_dir_and_level(TERM485_2_PIN, 0);
    ret = last_error(ret, err);

    err = gpio_expander_set_out_dir_and_level(FAILSAFE_485_1_PIN, 0);
    ret = last_error(ret, err);

    err = gpio_expander_set_out_dir_and_level(FAILSAFE_485_2_PIN, 0);
    ret = last_error(ret, err);

    err = rs485_bus_vout_on_off(false);
    ret = last_error(ret, err);

    err = rs485_term_on_off(RS485_1, false);
    ret = last_error(ret, err);

    err = rs485_term_on_off(RS485_2, false);
    ret = last_error(ret, err);

    err = rs485_pupd_on_off(RS485_1, false);
    ret = last_error(ret, err);

    err = rs485_pupd_on_off(RS485_2, false);
    ret = last_error(ret, err);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "At least one initialization step failed");
    }

    return ret;
}

#ifdef __unittest_env__
    void rs485_control_test_reset(void)
    {
        rs485_bus_vout_mutex = NULL;
        rs485_bus_vout_ctrl.enabled = false;
        rs485_bus_vout_ctrl.allowed = true;
    }
#endif
