#include "rs485_control.h"
#include "esp_log.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "freertos/FreeRTOS.h"


static struct {
    bool enabled;
    bool allowed;
} rs485_bus_vout_ctrl = {
    .enabled = false,
    .allowed = true
};

static esp_io_expander_handle_t io_expander = NULL;
static SemaphoreHandle_t rs485_bus_vout_mutex = NULL;

static const char *TAG = "rs485_control";

#define TERM485_1_PIN        IO_EXPANDER_PIN_NUM_0
#define TERM485_2_PIN        IO_EXPANDER_PIN_NUM_1
#define FAILSAFE_485_1_PIN   IO_EXPANDER_PIN_NUM_2
#define FAILSAFE_485_2_PIN   IO_EXPANDER_PIN_NUM_3
#define VOUT_485_PIN         IO_EXPANDER_PIN_NUM_6

void rs485_term_on_off(rs485_port_t port, bool on)
{
    if (io_expander == NULL) {
        ESP_LOGE(TAG, "Not initialized");
        return;
    }

    if (port == RS485_1) {
        esp_io_expander_set_level(io_expander, TERM485_1_PIN, on);
    } else if (port == RS485_2) {
        esp_io_expander_set_level(io_expander, TERM485_2_PIN, on);
    } else {
        ESP_LOGE(TAG, "Invalid RS485 port number: %d", port);
    }
}

void rs485_pupd_on_off(rs485_port_t port, bool on)
{
    if (io_expander == NULL) {
        ESP_LOGE(TAG, "Not initialized");
        return;
    }

    if (port == RS485_1) {
        esp_io_expander_set_level(io_expander, FAILSAFE_485_1_PIN, on);
    } else if (port == RS485_2) {
        esp_io_expander_set_level(io_expander, FAILSAFE_485_2_PIN, on);
    } else {
        ESP_LOGE(TAG, "Invalid RS485 port number: %d", port);
    }
}

void rs485_bus_vout_on_off(bool on)
{
    if ((io_expander == NULL) || (rs485_bus_vout_mutex == NULL)) {
        ESP_LOGE(TAG, "Not initialized");
        return;
    }

    xSemaphoreTake(rs485_bus_vout_mutex, portMAX_DELAY);
    rs485_bus_vout_ctrl.enabled = on;
    bool out_level = rs485_bus_vout_ctrl.enabled && rs485_bus_vout_ctrl.allowed;
    esp_io_expander_set_level(io_expander, VOUT_485_PIN, out_level);
    xSemaphoreGive(rs485_bus_vout_mutex);
}

void rs485_bus_vout_set_allowed(bool allowed)
{
    if ((io_expander == NULL) || (rs485_bus_vout_mutex == NULL)) {
        ESP_LOGE(TAG, "Not initialized");
        return;
    }

    xSemaphoreTake(rs485_bus_vout_mutex, portMAX_DELAY);
    rs485_bus_vout_ctrl.allowed = allowed;
    bool out_level = rs485_bus_vout_ctrl.enabled && rs485_bus_vout_ctrl.allowed;
    esp_io_expander_set_level(io_expander, VOUT_485_PIN, out_level);
    xSemaphoreGive(rs485_bus_vout_mutex);
}

void rs485_control_init(esp_io_expander_handle_t io_expander_handle)
{
    if (io_expander_handle == NULL) {
        ESP_LOGE(TAG, "io_expander is NULL");
        return;
    }

    io_expander = io_expander_handle;

    if (rs485_bus_vout_mutex == NULL) {
        rs485_bus_vout_mutex = xSemaphoreCreateMutex();
        if (rs485_bus_vout_mutex == NULL) {
            ESP_LOGW(TAG, "Failed to initialize RS-485 bus Vout mutex");
            // Don't exit, try to initialize IO expander GPIOs
        }
    }

    esp_io_expander_set_dir(io_expander, TERM485_1_PIN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, TERM485_1_PIN, 0);
    esp_io_expander_set_dir(io_expander, TERM485_2_PIN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, TERM485_2_PIN, 0);

    esp_io_expander_set_dir(io_expander, FAILSAFE_485_1_PIN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, FAILSAFE_485_1_PIN, 0);
    esp_io_expander_set_dir(io_expander, FAILSAFE_485_2_PIN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, FAILSAFE_485_2_PIN, 0);

    esp_io_expander_set_dir(io_expander, VOUT_485_PIN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, VOUT_485_PIN, 0);

    rs485_term_on_off(RS485_1, false);
    rs485_term_on_off(RS485_2, false);
    rs485_pupd_on_off(RS485_1, false);
    rs485_pupd_on_off(RS485_2, false);
    rs485_bus_vout_on_off(false);
}
