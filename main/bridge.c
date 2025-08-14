#include "bridge.h"

#include "esp_check.h"
#include "setting_items.h"
#include "driver/gpio.h"

#include "serial.h"
#include "tcp_client.h"
#include "tcp_server.h"

#include "sys_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
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

#define RS485_BUSY_MONITOR_STACK_SIZE 512
#define RS485_BUSY_MONITOR_PRIORITY   1

static const char *TAG = "bridge";

// Forward declarations
static bridge_mode_t string_to_bridge_mode(const char *str);

static bool bridge_ready = false;

static serial_desc_t *serial_desc[BRIDGES_COUNT] = {NULL, NULL};
static tcp_desc_t *tcp_desc[BRIDGES_COUNT] = {NULL, NULL};
static bridge_mode_t bridge_mode[BRIDGES_COUNT] = {BRIDGE_MODE_DISABLED, BRIDGE_MODE_DISABLED};

static int64_t last_activity_us[BRIDGES_COUNT] = {0, 0}; // microseconds

int tcp_server_active_connections(tcp_server_num_t server_num)
{
    if ((server_num < 0) || (server_num >= BRIDGES_COUNT)) {
        ESP_LOGE(TAG, "Unknown server number: %d", server_num);
        return 0;
    }
    if (!tcp_desc[server_num]) {
        return 0;
    }
    return tcp_desc[server_num]->active_connections;
}

static void rs485_busy_monitor_task(void *arg);

static bridge_mode_t string_to_bridge_mode(const char *str) {
    if (strncmp(str, BRIDGE_MODE_SERVER_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return BRIDGE_MODE_SERVER;
    } else if (strncmp(str, BRIDGE_MODE_CLIENT_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return BRIDGE_MODE_CLIENT;
    }
    return BRIDGE_MODE_DISABLED;
}

static void update_rs485_activity(int idx)
{
    last_activity_us[idx] = esp_timer_get_time();
    if (idx == 0) {
        sys_info.rs485_1_is_busy = true;
    } else if (idx == 1) {
        sys_info.rs485_2_is_busy = true;
    }
}

static void process_data_from_serial(serial_desc_t *desc, uint8_t *data, size_t len)
{
    if (!bridge_ready) {
        ESP_LOGW(TAG, "%s: bridge not ready", __func__);
        return;
    }

    ESP_LOGI(TAG, "received %d bytes from serial port %d", len, desc->port_num);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);

    int idx = -1;
    if (desc == serial_desc[0]) {
        idx = 0;
    } else if (desc == serial_desc[1]) {
        idx = 1;
    } else {
        ESP_LOGE(TAG, "%s: unknown serial descriptor", __func__);
        return;
    }

    update_rs485_activity(idx);

    esp_err_t err = ESP_OK;

    switch (bridge_mode[idx]) {
        case BRIDGE_MODE_SERVER:
            if (tcp_desc[idx]) {
                err = tcp_server_send(tcp_desc[idx], data, len);
            }
            break;
        case BRIDGE_MODE_CLIENT:
            if (tcp_desc[idx]) {
                err = tcp_client_send(tcp_desc[idx], data, len);
            }
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

    int idx = -1;
    if (desc == tcp_desc[0]) {
        idx = 0;
    } else if (desc == tcp_desc[1]) {
        idx = 1;
    } else {
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
        update_rs485_activity(idx);

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

static void rs485_busy_monitor_task(void *arg)
{
    while (1) {
        int64_t now = esp_timer_get_time();
        // Port 1
        if (sys_info.rs485_1_is_busy && (now - last_activity_us[0]) > RS485_BUSY_TIMEOUT_MS * 1000) {
            sys_info.rs485_1_is_busy = false;
        }
        // Port 2
        if (sys_info.rs485_2_is_busy && (now - last_activity_us[1]) > RS485_BUSY_TIMEOUT_MS * 1000) {
            sys_info.rs485_2_is_busy = false;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t bridge_init(void)
{
    serial_config_t serial_config[BRIDGES_COUNT] = {0};
    int bridge_port[BRIDGES_COUNT] = {0};
    uint32_t bridge_ip[BRIDGES_COUNT] = {0};

    uart_port_t port_nums[BRIDGES_COUNT] = {SERIAL_PORT_NUM_1, SERIAL_PORT_NUM_2};
    int tx_pins[BRIDGES_COUNT] = {SERIAL_OUTPUT_PIN_1, SERIAL_OUTPUT_PIN_2};
    int rx_pins[BRIDGES_COUNT] = {SERIAL_INPUT_PIN_1, SERIAL_INPUT_PIN_2};
    int dir_pins[BRIDGES_COUNT] = {SERIAL_IO_PIN_1, SERIAL_IO_PIN_2};

    for (int i = 0; i < BRIDGES_COUNT; ++i) {
        serial_config[i].port_num = port_nums[i];
        serial_config[i].tx_pin = tx_pins[i];
        serial_config[i].rx_pin = rx_pins[i];
        serial_config[i].dir_pin = dir_pins[i];

        char key_buf[32]; // TODO: use defined constant for buffer size

        snprintf(key_buf, sizeof(key_buf), "baudrate_%d", i + 1);
        serial_config[i].baudrate = setting_items_read_int(key_buf);

        snprintf(key_buf, sizeof(key_buf), "stopbits_%d", i + 1);
        char stopbits_str[SETTING_ITEM_MAX_STR_LEN] = {0};
        ESP_RETURN_ON_ERROR(setting_items_read(key_buf, stopbits_str), TAG, "error reading stopbits for port %d", i + 1);
        if (strncmp(stopbits_str, UART_STOP_BITS_1_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
            serial_config[i].stopbits = UART_STOP_BITS_1;
        } else if (strncmp(stopbits_str, UART_STOP_BITS_1_5_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
            serial_config[i].stopbits = UART_STOP_BITS_1_5;
        } else if (strncmp(stopbits_str, UART_STOP_BITS_2_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
            serial_config[i].stopbits = UART_STOP_BITS_2;
        } else {
            serial_config[i].stopbits = UART_STOP_BITS_2;
            ESP_LOGW(TAG, "Unknown stopbits setting for port %d, defaulting to 2", i + 1);
        }

        snprintf(key_buf, sizeof(key_buf), "parity_%d", i + 1);
        char parity_str[SETTING_ITEM_MAX_STR_LEN] = {0};
        ESP_RETURN_ON_ERROR(setting_items_read(key_buf, parity_str), TAG, "error reading parity for port %d", i + 1);
        if (strncmp(parity_str, UART_PARITY_DISABLE_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
            serial_config[i].parity = UART_PARITY_DISABLE;
        } else if (strncmp(parity_str, UART_PARITY_EVEN_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
            serial_config[i].parity = UART_PARITY_EVEN;
        } else if (strncmp(parity_str, UART_PARITY_ODD_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
            serial_config[i].parity = UART_PARITY_ODD;
        } else {
            serial_config[i].parity = UART_PARITY_DISABLE;
            ESP_LOGW(TAG, "Unknown parity setting for port %d, defaulting to disable", i + 1);
        }

        snprintf(key_buf, sizeof(key_buf), "databits_%d", i + 1);
        char databits_str[SETTING_ITEM_MAX_STR_LEN] = {0};
        ESP_RETURN_ON_ERROR(setting_items_read(key_buf, databits_str), TAG, "error reading databits for port %d", i + 1);
        if (strncmp(databits_str, UART_DATA_5_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
            serial_config[i].databits = UART_DATA_5_BITS;
        } else if (strncmp(databits_str, UART_DATA_6_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
            serial_config[i].databits = UART_DATA_6_BITS;
        } else if (strncmp(databits_str, UART_DATA_7_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
            serial_config[i].databits = UART_DATA_7_BITS;
        } else if (strncmp(databits_str, UART_DATA_8_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
            serial_config[i].databits = UART_DATA_8_BITS;
        } else {
            serial_config[i].databits = UART_DATA_8_BITS;
            ESP_LOGW(TAG, "Unknown databits setting for port %d, defaulting to 8", i + 1);
        }

        snprintf(key_buf, sizeof(key_buf), "bridge_mode_%d", i + 1);
        char mode_str[SETTING_ITEM_MAX_STR_LEN] = {0};
        ESP_RETURN_ON_ERROR(setting_items_read(key_buf, mode_str), TAG, "error reading bridge_mode for port %d", i + 1);
        bridge_mode[i] = string_to_bridge_mode(mode_str);

        snprintf(key_buf, sizeof(key_buf), "bridge_port_%d", i + 1);
        bridge_port[i] = setting_items_read_int(key_buf);

        snprintf(key_buf, sizeof(key_buf), "bridge_ip_%d", i + 1);
        char ip_str[SETTING_ITEM_MAX_STR_LEN] = {0};
        ESP_RETURN_ON_ERROR(setting_items_read(key_buf, ip_str), TAG, "error reading bridge_ip for port %d", i + 1);
        inet_pton(AF_INET, ip_str, &bridge_ip[i]);

        ESP_RETURN_ON_ERROR(bridge_init_port(&serial_config[i], bridge_mode[i], bridge_port[i], bridge_ip[i], &serial_desc[i], &tcp_desc[i]), TAG, "error initializing port %d", i + 1);
    }

    bridge_ready = true;
    ESP_LOGI(TAG, "initialized");

    // Start RS485 busy monitor task
    xTaskCreate(rs485_busy_monitor_task, "rs485_busy_monitor_task", RS485_BUSY_MONITOR_STACK_SIZE, NULL, RS485_BUSY_MONITOR_PRIORITY, NULL);

    return ESP_OK;
}
