#include "bridge.h"

#include "esp_check.h"
#include "setting_items.h"
#include "driver/gpio.h"

#include "serial.h"
#include "tcp_client.h"
#include "tcp_server.h"
#include "transparent_tcp.h"
#include "modbus_tcp.h"
#include "rs485_stats.h"

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
} bridge_config_t;

typedef struct {
    serial_desc_t* serial_desc;
    tcp_desc_t* tcp_desc;
    bool initialized;
    bool disabled;
    bool init_request;
} bridge_ctx_t;

static bridge_config_t bridge_current_cfg[BRIDGES_COUNT] = {0};
static bridge_ctx_t bridge_ctx[BRIDGES_COUNT] = {0};

int tcp_server_active_connections(tcp_server_num_t server_num)
{
    if ((server_num < 0) || (server_num >= TCP_SERVER_COUNT)) {
        ESP_LOGE(TAG, "Unknown server number: %d", server_num);
        return 0;
    }

    if (bridge_current_cfg[server_num].bridge_mode == BRIDGE_MODE_DISABLED) {
        return 0;
    }

    if (!bridge_ctx[server_num].tcp_desc) {
        return 0;
    }

    return bridge_ctx[server_num].tcp_desc->active_connections;
}

esp_err_t bridge_disable_port(unsigned index)
{
    if (index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    bridge_ctx[index].disabled = true;

    if (bridge_ctx[index].initialized) {
        bridge_ctx[index].init_request = true;
        bridge_port_deinit(index);
    }

    return ESP_OK;
}

esp_err_t bridge_enable_port(unsigned index)
{
    if (index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    bridge_ctx[index].disabled = false;

    if (bridge_ctx[index].init_request) {
        if (!bridge_ctx[index].initialized) {
            bridge_ctx[index].init_request = false;
            bridge_port_init(index);
        }
    }

    return ESP_OK;
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
    // Start RS485 busy monitor task and init error percentage statistics
    rs485_busy_monitor_init();
    rs485_stats_init();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        bridge_port_init(index);
    }

    ESP_LOGI(TAG, "Bridge initialized");
    return ESP_OK;
}


esp_err_t bridge_port_init(unsigned index)
{
    if (index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (bridge_ctx[index].initialized) {
        ESP_LOGW(TAG, "Port %u already initialized", index + 1);
        return ESP_ERR_NOT_ALLOWED;
    }
    if (bridge_ctx[index].disabled) {
        ESP_LOGW(TAG, "Port %u is disabled", index + 1);
        bridge_ctx[index].init_request = true;
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Port[%u]: Initializing...", index + 1);

    ESP_RETURN_ON_ERROR(read_serial_port_config(index, &bridge_current_cfg[index].serial_config),
                        TAG, "Port[%u]: Failed to read serial config for port", index + 1);
    ESP_RETURN_ON_ERROR(read_tcp_bridge_config(index, &bridge_current_cfg[index].bridge_mode, &bridge_current_cfg[index].bridge_ip,
                                                &bridge_current_cfg[index].bridge_port, &bridge_current_cfg[index].bridge_mb),
                        TAG, "Port[%u]: Failed to read bridge config", index + 1);

    if (bridge_current_cfg[index].bridge_mode == BRIDGE_MODE_DISABLED) {
        ESP_LOGW(TAG, "Port[%d] is disabled", bridge_current_cfg[index].serial_config.port_num);
        return ESP_OK;
    }

    if (bridge_current_cfg[index].bridge_mb) {
        ESP_RETURN_ON_ERROR(modbus_tcp_init_port(index, &bridge_current_cfg[index].serial_config, bridge_current_cfg[index].bridge_mode,
                                                    bridge_current_cfg[index].bridge_port, bridge_current_cfg[index].bridge_ip,
                                                    &bridge_ctx[index].serial_desc, &bridge_ctx[index].tcp_desc),
                            TAG, "Failed to initialize port %u in Modbus TCP mode", index + 1);
        ESP_LOGI(TAG, "Port[%d] initialized in Modbus TCP mode", bridge_current_cfg[index].serial_config.port_num);
    } else {
        ESP_RETURN_ON_ERROR(transparent_tcp_init_port(index, &bridge_current_cfg[index].serial_config, bridge_current_cfg[index].bridge_mode,
                                                        bridge_current_cfg[index].bridge_port, bridge_current_cfg[index].bridge_ip,
                                                        &bridge_ctx[index].serial_desc, &bridge_ctx[index].tcp_desc),
                            TAG, "Failed to initialize port %u in transparent bridge mode", index + 1);
        ESP_LOGI(TAG, "Port[%d] initialized in transparent bridge mode", bridge_current_cfg[index].serial_config.port_num);
    }

    bridge_ctx[index].initialized = true;
    ESP_LOGD(TAG, "Port[%u]: Initialized", index + 1);

    return ESP_OK;
}


esp_err_t bridge_port_deinit(unsigned index)
{
    if (index >= BRIDGES_COUNT) {
        ESP_LOGE(TAG, "Port[%u]: Port number out of range", index + 1);
        return ESP_ERR_INVALID_ARG;
    }
    if (!bridge_ctx[index].initialized) {
        ESP_LOGI(TAG, "Port[%u] is not initialized", index + 1);
        return ESP_OK;
    }

    bridge_config_t* cfg = &bridge_current_cfg[index];
    if (cfg->bridge_mode == BRIDGE_MODE_DISABLED) {
        ESP_LOGD(TAG, "Port[%u]: Nothing to deinitialize", index + 1);
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Port[%u]: Deinitializing...", index + 1);
    if (cfg->bridge_mb) {
        modbus_tcp_deinit_port(index);
    } else {
        transparent_tcp_deinit_port(index);
    }

    bridge_ctx[index].initialized = false;

    rs485_busy_monitor_reset(index);
    rs485_stats_reset(index);

    ESP_LOGD(TAG, "Port[%u]: Deinitialized", index + 1);
    return ESP_OK;
}


bool bridge_port_check_settings_changed(unsigned index)
{
    if (index >= BRIDGES_COUNT) {
        return false;
    }

    bridge_config_t new_cfg = {0};
    esp_err_t ret = read_serial_port_config(index, &new_cfg.serial_config);
    if (ret != ESP_OK) {
        return false;
    }
    ret = read_tcp_bridge_config(index, &new_cfg.bridge_mode, &new_cfg.bridge_ip, &new_cfg.bridge_port, &new_cfg.bridge_mb);
    if (ret != ESP_OK) {
        return false;
    }

    if (!bridge_ctx[index].initialized) {
        if (new_cfg.bridge_mode == BRIDGE_MODE_DISABLED) {
            return false;
        } else {
            return true;
        }
    }

    if (memcmp(&bridge_current_cfg[index], &new_cfg, sizeof(new_cfg)) == 0) {
        return false;
    } else {
        return true;
    }
}

#ifdef __unittest_env__
    void bridge_reset(void)
    {
        memset(bridge_current_cfg, 0, sizeof(bridge_current_cfg));
        memset(bridge_ctx, 0, sizeof(bridge_ctx));
    }
#endif
