#include "bridge.h"

#include "esp_check.h"
#include "serial.h"
#include "setting_items.h"
#include "tcp_client.h"
#include "tcp_server.h"

static const char *TAG = "bridge";

static bridge_mode_t bridge_mode = 0;
static bool bridge_ready = false;

void process_data_from_serial(uint8_t *data, size_t len)
{
    if (!bridge_ready) {
        ESP_LOGW(TAG, "%s: bridge not ready", __func__);
        return;
    }

    esp_err_t err = ESP_OK;

    // TODO: Изменить уровень логирования на DEBUG или отключить
    ESP_LOGI(TAG, "received %d bytes from serial", len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);

    switch (bridge_mode) {
        case BRIDGE_MODE_SERVER:
            err = tcp_server_send(data, len);
            break;
        case BRIDGE_MODE_CLIENT:
            err = tcp_client_send(data, len);
            break;
        default:
            ESP_LOGE(TAG, "%s: unknown bridge mode", __func__);
            return;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: error sending data to tcp", __func__);
    }
}

void process_data_from_tcp(uint8_t *data, size_t len)
{
    if (!bridge_ready) {
        ESP_LOGW(TAG, "%s: bridge not ready", __func__);
        return;
    }

    esp_err_t err = ESP_OK;

    // TODO: Изменить уровень логирования на DEBUG или отключить
    ESP_LOGI(TAG, "received %d bytes from tcp", len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);

    err = serial_send(data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: error sending data to serial", __func__);
    }
}

esp_err_t bridge_init(void)
{
    serial_config_t serial_config = {0};
    int bridge_port = 0;
    uint32_t bridge_ip = 0;

    ESP_RETURN_ON_ERROR(setting_items_read_raw("bridge_mode", &bridge_mode, SETTING_ITEM_TYPE_NUM),
        TAG, "error reading bridge_mode");
    ESP_RETURN_ON_ERROR(setting_items_read_raw("bridge_port", &bridge_port, SETTING_ITEM_TYPE_NUM),
        TAG, "error reading bridge_port");
    ESP_RETURN_ON_ERROR(setting_items_read_raw("bridge_ip", &bridge_ip, SETTING_ITEM_TYPE_NUM),
        TAG, "error reading bridge_ip");

    ESP_RETURN_ON_ERROR(setting_items_read_raw("baudrate", &serial_config.baudrate, SETTING_ITEM_TYPE_NUM),
        TAG, "error reading baudrate");
    ESP_RETURN_ON_ERROR(setting_items_read_raw("stopbits", &serial_config.stopbits, SETTING_ITEM_TYPE_NUM),
        TAG, "error reading stopbits");
    ESP_RETURN_ON_ERROR(setting_items_read_raw("parity", &serial_config.parity, SETTING_ITEM_TYPE_NUM),
        TAG, "error reading parity");
    ESP_RETURN_ON_ERROR(setting_items_read_raw("databits", &serial_config.databits, SETTING_ITEM_TYPE_NUM),
        TAG, "error reading databits");

    switch (bridge_mode) {
        case BRIDGE_MODE_SERVER:
            ESP_RETURN_ON_ERROR(tcp_server_init(bridge_port, process_data_from_tcp),
                TAG, "error initializing tcp server");
            break;
        case BRIDGE_MODE_CLIENT:
            ESP_RETURN_ON_ERROR(tcp_client_init(bridge_ip, bridge_port, process_data_from_tcp),
                TAG, "error initializing tcp client");
            break;
        default:
            ESP_LOGE(TAG, "unknown bridge mode");
            return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(serial_init(&serial_config, process_data_from_serial),
        TAG, "error initializing serial");

    bridge_ready = true;
    ESP_LOGI(TAG, "initialized");

    return ESP_OK;
}
