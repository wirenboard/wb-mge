#include "bridge.h"

#include "esp_check.h"
#include "setting_items.h"
#include "driver/gpio.h"

#include "serial.h"
#include "tcp_client.h"
#include "tcp_server.h"

#define SERIAL_PORT_NUM_1             1
#define SERIAL_INPUT_PIN_1            GPIO_NUM_9
#define SERIAL_OUTPUT_PIN_1           GPIO_NUM_10
#define SERIAL_IO_PIN_1               GPIO_NUM_4

#define SERIAL_PORT_NUM_2             2
#define SERIAL_INPUT_PIN_2            GPIO_NUM_12
#define SERIAL_OUTPUT_PIN_2           GPIO_NUM_14
#define SERIAL_IO_PIN_2               GPIO_NUM_15

static const char *TAG = "bridge";

static bool bridge_ready = false;

static serial_desc_t *serial_desc[2] = {NULL, NULL};
static tcp_desc_t *tcp_desc[2] = {NULL, NULL};
static bridge_mode_t bridge_mode[2] = {BRIDGE_MODE_DISABLED, BRIDGE_MODE_DISABLED};

static void process_data_from_serial(serial_desc_t *desc, uint8_t *data, size_t len)
{
    if (!bridge_ready) {
        ESP_LOGW(TAG, "%s: bridge not ready", __func__);
        return;
    }

    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "received %d bytes from serial port %d", len, desc->port_num);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);

    int idx = (desc == serial_desc[0]) ? 0 : (desc == serial_desc[1]) ? 1 : -1;
    if (idx < 0) {
        ESP_LOGE(TAG, "%s: unknown serial descriptor", __func__);
        return;
    }

    switch (bridge_mode[idx]) {
        case BRIDGE_MODE_SERVER:
            if (tcp_desc[idx])
                err = tcp_server_send(tcp_desc[idx], data, len);
            break;
        case BRIDGE_MODE_CLIENT:
            if (tcp_desc[idx])
                err = tcp_client_send(tcp_desc[idx], data, len);
            break;
        default:
            ESP_LOGE(TAG, "%s: unknown bridge mode %d", __func__, idx + 1);
            return;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: error sending data to tcp", __func__);
    }
}

static void process_data_from_tcp(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    if (!bridge_ready) {
        ESP_LOGW(TAG, "%s: bridge not ready", __func__);
        return;
    }

    int idx = (desc == tcp_desc[0]) ? 0 : (desc == tcp_desc[1]) ? 1 : -1;

    if (idx < 0) {
        ESP_LOGE(TAG, "%s: unknown tcp descriptor", __func__);
        return;
    }

    if (desc->client_sock < 0) {
        ESP_LOGE(TAG, "%s: no client connected", __func__);
        return;
    }

    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "received %d bytes from tcp", len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);

    if (serial_desc[idx]) {
        err = serial_send(serial_desc[idx], data, len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: error sending data to serial port", __func__);
        }
    }
}

static esp_err_t bridge_init_port(serial_config_t *config, bridge_mode_t mode, int port, uint32_t ip, serial_desc_t **serial_desc,
                                  tcp_desc_t **tcp_desc)
{
    esp_err_t err = ESP_OK;
    switch (mode) {
        case BRIDGE_MODE_SERVER:
            err = tcp_server_init(port, process_data_from_tcp, tcp_desc);
            break;
        case BRIDGE_MODE_CLIENT:
            err = tcp_client_init(ip, port, process_data_from_tcp, tcp_desc);
            break;
        default:
            ESP_LOGE(TAG, "unknown bridge mode");
            return ESP_FAIL;
    }

    if (err != ESP_OK) {
        return err;
    }

    *serial_desc = serial_init(config, process_data_from_serial);
    if (!*serial_desc) {
        ESP_LOGE(TAG, "error initializing serial port");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t bridge_init(void)
{
    serial_config_t serial_config[2] = {0};
    int bridge_port[2] = {0};
    uint32_t bridge_ip[2] = {0};
    
    const char *baudrate_keys[2] = {"baudrate", "baudrate_2"};
    const char *stopbits_keys[2] = {"stopbits", "stopbits_2"};
    const char *parity_keys[2] = {"parity", "parity_2"};
    const char *databits_keys[2] = {"databits", "databits_2"};
    const char *mode_keys[2] = {"bridge_mode", "bridge_mode_2"};
    const char *port_keys[2] = {"bridge_port", "bridge_port_2"};
    const char *ip_keys[2] = {"bridge_ip", "bridge_ip_2"};
    
    uart_port_t port_nums[2] = {SERIAL_PORT_NUM_1, SERIAL_PORT_NUM_2};
    int tx_pins[2] = {SERIAL_OUTPUT_PIN_1, SERIAL_OUTPUT_PIN_2};
    int rx_pins[2] = {SERIAL_INPUT_PIN_1, SERIAL_INPUT_PIN_2};
    int dir_pins[2] = {SERIAL_IO_PIN_1, SERIAL_IO_PIN_2};

    for (int i = 0; i < 2; ++i) {
        serial_config[i].port_num = port_nums[i];
        serial_config[i].tx_pin = tx_pins[i];
        serial_config[i].rx_pin = rx_pins[i];
        serial_config[i].dir_pin = dir_pins[i];

        ESP_RETURN_ON_ERROR(setting_items_read_raw(baudrate_keys[i], &serial_config[i].baudrate, SETTING_ITEM_TYPE_NUM), TAG, "error reading baudrate for port %d", i + 1);
        ESP_RETURN_ON_ERROR(setting_items_read_raw(stopbits_keys[i], &serial_config[i].stopbits, SETTING_ITEM_TYPE_NUM), TAG, "error reading stopbits for port %d", i + 1);
        ESP_RETURN_ON_ERROR(setting_items_read_raw(parity_keys[i], &serial_config[i].parity, SETTING_ITEM_TYPE_NUM), TAG, "error reading parity for port %d", i + 1);
        ESP_RETURN_ON_ERROR(setting_items_read_raw(databits_keys[i], &serial_config[i].databits, SETTING_ITEM_TYPE_NUM), TAG, "error reading databits for port %d", i + 1);
        ESP_RETURN_ON_ERROR(setting_items_read_raw(mode_keys[i], &bridge_mode[i], SETTING_ITEM_TYPE_NUM), TAG, "error reading bridge_mode for port %d", i + 1);
        ESP_RETURN_ON_ERROR(setting_items_read_raw(port_keys[i], &bridge_port[i], SETTING_ITEM_TYPE_NUM), TAG, "error reading bridge_port for port %d", i + 1);
        ESP_RETURN_ON_ERROR(setting_items_read_raw(ip_keys[i], &bridge_ip[i], SETTING_ITEM_TYPE_NUM), TAG, "error reading bridge_ip for port %d", i + 1);
        ESP_RETURN_ON_ERROR(bridge_init_port(&serial_config[i], bridge_mode[i], bridge_port[i], bridge_ip[i], &serial_desc[i], &tcp_desc[i]), TAG, "error initializing port %d", i + 1);
    }

    bridge_ready = true;
    ESP_LOGI(TAG, "initialized");
    return ESP_OK;
}
