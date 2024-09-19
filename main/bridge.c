#include "bridge.h"

#include "esp_check.h"
#include "serial.h"
#include "setting_items.h"
#include "tcp_client.h"
#include "tcp_server.h"

static const char *TAG = "bridge";

bridge_mode_t bridge_mode = 0;
bool bridge_ready = false;
bool bridge_modbus = false;

void process_data_from_serial(uint8_t *data, uint8_t len)
{
    if (!bridge_ready) {
        return;
    }

    ESP_LOGI(TAG, "received %d bytes from serial", len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);

    if (bridge_mode == BRIDGE_MODE_SERVER) {
        tcp_server_send(data, len);
    } else if (bridge_mode == BRIDGE_MODE_CLIENT) {
        tcp_client_send(data, len);
    }
}

void process_data_from_tcp(uint8_t *data, uint8_t len)
{
    if (!bridge_ready) {
        return;
    }

    ESP_LOGI(TAG, "received %d bytes from tcp", len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);

    serial_send(data, len);
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
    ESP_RETURN_ON_ERROR(setting_items_read_raw("bridge_ip", &bridge_ip, SETTING_ITEM_TYPE_NUM), TAG,
                        "error reading bridge_ip");

    ESP_RETURN_ON_ERROR(
        setting_items_read_raw("baudrate", &serial_config.baudrate, SETTING_ITEM_TYPE_NUM), TAG,
        "error reading baudrate");
    ESP_RETURN_ON_ERROR(
        setting_items_read_raw("baudrate", &serial_config.baudrate, SETTING_ITEM_TYPE_NUM), TAG,
        "error reading baudrate");
    ESP_RETURN_ON_ERROR(
        setting_items_read_raw("stopbits", &serial_config.stopbits, SETTING_ITEM_TYPE_NUM), TAG,
        "error reading stopbits");
    ESP_RETURN_ON_ERROR(
        setting_items_read_raw("parity", &serial_config.parity, SETTING_ITEM_TYPE_NUM), TAG,
        "error reading parity");
    ESP_RETURN_ON_ERROR(
        setting_items_read_raw("databits", &serial_config.databits, SETTING_ITEM_TYPE_NUM), TAG,
        "error reading databits");

    if (bridge_mode == BRIDGE_MODE_SERVER) {
        ESP_RETURN_ON_ERROR(tcp_server_init(bridge_port, process_data_from_tcp), TAG,
                            "error initializing tcp server");
    } else if (bridge_mode == BRIDGE_MODE_CLIENT) {
        ESP_RETURN_ON_ERROR(tcp_client_init(bridge_ip, bridge_port, process_data_from_tcp), TAG,
                            "error initializing tcp client");
    }

    ESP_RETURN_ON_ERROR(serial_init(&serial_config, process_data_from_serial), TAG,
                        "error initializing serial");

    bridge_ready = true;
    ESP_LOGI(TAG, "initialized");

    return ESP_OK;
}
