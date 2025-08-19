#include "bridge.h"

#include "esp_check.h"
#include "setting_items.h"
#include "driver/gpio.h"

#include "serial.h"
#include "tcp_client.h"
#include "tcp_server.h"
#include "transparent_tcp.h"
#include "modbus_tcp.h"
#include "rs485_busy_monitor.h"

#include "freertos/FreeRTOS.h"
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERIAL_PORT_NUM_1             1
#define SERIAL_INPUT_PIN_1            GPIO_NUM_9
#define SERIAL_OUTPUT_PIN_1           GPIO_NUM_10
#define SERIAL_IO_PIN_1               GPIO_NUM_4

#define SERIAL_PORT_NUM_2             2
#define SERIAL_INPUT_PIN_2            GPIO_NUM_12
#define SERIAL_OUTPUT_PIN_2           GPIO_NUM_14
#define SERIAL_IO_PIN_2               GPIO_NUM_15

#define RS485_BUSY_TIMEOUT_MS         5000

#define BRIDGES_COUNT                 2

#define RS485_BUSY_MONITOR_STACK_SIZE 1024
#define RS485_BUSY_MONITOR_PRIORITY   1

static const char *TAG = "bridge";

// Forward declarations
static bridge_mode_t string_to_bridge_mode(const char *str);

typedef struct {
    serial_config_t serial_config;
    bridge_mode_t bridge_mode;
    uint32_t bridge_ip;
    int bridge_port;
    bool bridge_mb;
    serial_desc_t* serial_desc;
    tcp_desc_t* tcp_desc;
} bridge_ctx_t;

static bridge_ctx_t bridge_ctx[BRIDGES_COUNT] = {0};

int tcp_server_active_connections(tcp_server_num_t server_num)
{
    if ((server_num < 0) || (server_num >= BRIDGES_COUNT)) {
        ESP_LOGE(TAG, "Unknown server number: %d", server_num);
        return 0;
    }
    if (!bridge_ctx[server_num].tcp_desc) {
        return 0;
    }
    return bridge_ctx[server_num].tcp_desc->active_connections;
}

static bridge_mode_t string_to_bridge_mode(const char *str) {
    if (strncmp(str, BRIDGE_MODE_SERVER_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return BRIDGE_MODE_SERVER;
    } else if (strncmp(str, BRIDGE_MODE_CLIENT_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return BRIDGE_MODE_CLIENT;
    }
    return BRIDGE_MODE_DISABLED;
}

static esp_err_t read_serial_port_config(const int index, serial_config_t* serial_config)
{
    static const uart_port_t port_nums[BRIDGES_COUNT] = {SERIAL_PORT_NUM_1, SERIAL_PORT_NUM_2};
    static const int tx_pins[BRIDGES_COUNT] = {SERIAL_OUTPUT_PIN_1, SERIAL_OUTPUT_PIN_2};
    static const int rx_pins[BRIDGES_COUNT] = {SERIAL_INPUT_PIN_1, SERIAL_INPUT_PIN_2};
    static const int dir_pins[BRIDGES_COUNT] = {SERIAL_IO_PIN_1, SERIAL_IO_PIN_2};

    serial_config->port_num = port_nums[index];
    serial_config->tx_pin = tx_pins[index];
    serial_config->rx_pin = rx_pins[index];
    serial_config->dir_pin = dir_pins[index];

    char key_buf[SETTING_ITEM_MAX_STR_LEN];
    char value_str[SETTING_ITEM_MAX_STR_LEN];

    snprintf(key_buf, sizeof(key_buf), "baudrate_%d", index + 1);
    serial_config->baudrate = setting_items_read_int(key_buf);
    if (!serial_config->baudrate) {
        ESP_LOGE(TAG, "Failed to read baudrate for port %d", index + 1);
        return ESP_FAIL;
    }

    snprintf(key_buf, sizeof(key_buf), "parity_%d", index + 1);
    ESP_RETURN_ON_ERROR(setting_items_read(key_buf, value_str), TAG, "Failed to read parity for port %d", index + 1);
    if (strncmp(value_str, UART_PARITY_DISABLE_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        serial_config->parity = UART_PARITY_DISABLE;
    } else if (strncmp(value_str, UART_PARITY_EVEN_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        serial_config->parity = UART_PARITY_EVEN;
    } else if (strncmp(value_str, UART_PARITY_ODD_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        serial_config->parity = UART_PARITY_ODD;
    } else {
        serial_config->parity = UART_PARITY_DISABLE;
    }

    snprintf(key_buf, sizeof(key_buf), "stopbits_%d", index + 1);
    ESP_RETURN_ON_ERROR(setting_items_read(key_buf, value_str), TAG, "Failed to read stopbits for port %d", index + 1);
    if (strncmp(value_str, UART_STOP_BITS_1_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        serial_config->stopbits = UART_STOP_BITS_1;
    } else if (strncmp(value_str, UART_STOP_BITS_1_5_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        serial_config->stopbits = UART_STOP_BITS_1_5;
    } else if (strncmp(value_str, UART_STOP_BITS_2_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        serial_config->stopbits = UART_STOP_BITS_2;
    } else {
        serial_config->stopbits = UART_STOP_BITS_2;
    }

    snprintf(key_buf, sizeof(key_buf), "databits_%d", index + 1);
    ESP_RETURN_ON_ERROR(setting_items_read(key_buf, value_str), TAG, "Failed to read databits for port %d", index + 1);
    if (strncmp(value_str, UART_DATA_5_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        serial_config->databits = UART_DATA_5_BITS;
    } else if (strncmp(value_str, UART_DATA_6_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        serial_config->databits = UART_DATA_6_BITS;
    } else if (strncmp(value_str, UART_DATA_7_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        serial_config->databits = UART_DATA_7_BITS;
    } else if (strncmp(value_str, UART_DATA_8_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        serial_config->databits = UART_DATA_8_BITS;
    } else {
        serial_config->databits = UART_DATA_8_BITS;
    }

    return ESP_OK;
}

static esp_err_t read_tcp_bridge_config(const int index, bridge_mode_t* mode, uint32_t* ip, int* port, bool* modbus)
{
    char key_buf[SETTING_ITEM_MAX_STR_LEN];
    char value_str[SETTING_ITEM_MAX_STR_LEN];

    snprintf(key_buf, sizeof(key_buf), "bridge_mode_%d", index + 1);
    ESP_RETURN_ON_ERROR(setting_items_read(key_buf, value_str), TAG, "Failed to read bridge_mode for port %d", index + 1);
    *mode = string_to_bridge_mode(value_str);

    snprintf(key_buf, sizeof(key_buf), "bridge_ip_%d", index + 1);
    ESP_RETURN_ON_ERROR(setting_items_read(key_buf, value_str), TAG, "Failed to read bridge_ip for port %d", index + 1);
    inet_pton(AF_INET, value_str, ip);

    snprintf(key_buf, sizeof(key_buf), "bridge_port_%d", index + 1);
    *port = setting_items_read_int(key_buf);
    if (!*port) {
        ESP_LOGE(TAG, "Failed to read bridge_port for port %d", index + 1);
        return ESP_FAIL;
    }

    snprintf(key_buf, sizeof(key_buf), "bridge_modbus_%d", index + 1);
    *modbus = setting_items_read_bool(key_buf);

    return ESP_OK;
}

esp_err_t bridge_init(void)
{
    for (int i = 0; i < BRIDGES_COUNT; ++i) {

        ESP_RETURN_ON_ERROR(read_serial_port_config(i, &bridge_ctx[i].serial_config), TAG, "Failed to read serial config for port %d", i + 1);
        ESP_RETURN_ON_ERROR(read_tcp_bridge_config(i, &bridge_ctx[i].bridge_mode, &bridge_ctx[i].bridge_ip, &bridge_ctx[i].bridge_port, &bridge_ctx[i].bridge_mb),
                                                    TAG, "Failed to read bridge config for port %d", i + 1);

        if (bridge_ctx[i].bridge_mode == BRIDGE_MODE_DISABLED) {
            ESP_LOGW(TAG, "Port[%d] is disabled", bridge_ctx[i].serial_config.port_num);
            continue;
        }

        if (bridge_ctx[i].bridge_mb) {
            ESP_RETURN_ON_ERROR(modbus_tcp_init_port(i, &bridge_ctx[i].serial_config, bridge_ctx[i].bridge_mode,
                                bridge_ctx[i].bridge_port, bridge_ctx[i].bridge_ip, &bridge_ctx[i].serial_desc, &bridge_ctx[i].tcp_desc),
                                TAG, "error initializing port %d in Modbus TCP mode", i + 1);
            ESP_LOGI(TAG, "Port[%d] initialized in Modbus TCP mode", bridge_ctx[i].serial_config.port_num);
        } else {
            ESP_RETURN_ON_ERROR(transparent_tcp_init_port(i, &bridge_ctx[i].serial_config, bridge_ctx[i].bridge_mode,
                                bridge_ctx[i].bridge_port, bridge_ctx[i].bridge_ip, &bridge_ctx[i].serial_desc, &bridge_ctx[i].tcp_desc),
                                TAG, "error initializing port %d in transparent bridge mode", i + 1);
            ESP_LOGI(TAG, "Port[%d] initialized in transparent bridge mode", bridge_ctx[i].serial_config.port_num);
        }
    }

    rs485_busy_monitor_init();

    ESP_LOGI(TAG, "Bridge initialized");

    return ESP_OK;
}
