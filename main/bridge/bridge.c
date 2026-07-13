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
#include "array_size.h"
#include "board_pins.h"
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>


#define SERIAL_PORT_NUM_1             1
#define SERIAL_PORT_NUM_2             2

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
} bridge_ctx_t;

static bridge_config_t bridge_current_cfg[BRIDGES_COUNT] = {0};
static bridge_ctx_t bridge_ctx[BRIDGES_COUNT] = {0};

int tcp_server_active_connections(tcp_server_num_t server_num)
{
    if ((server_num < 0) || (server_num >= BRIDGES_COUNT)) {
        ESP_LOGE(TAG, "Unknown server number: %d", server_num);
        return 0;
    }

    if (bridge_current_cfg[server_num].bridge_mode == BRIDGE_MODE_DISABLED) {
        return 0;
    }

    if (!bridge_ctx[server_num].tcp_desc) {
        return 0;
    }

    return (int)bridge_ctx[server_num].tcp_desc->active_connections;
}

static bridge_mode_t string_to_bridge_mode(const char *str) {
    if (strncmp(str, BRIDGE_MODE_SERVER_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return BRIDGE_MODE_SERVER;
    } else if (strncmp(str, BRIDGE_MODE_CLIENT_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return BRIDGE_MODE_CLIENT;
    }
    return BRIDGE_MODE_DISABLED;
}

/* Map a string value to its corresponding integer constant using a lookup table.
 * Returns default_val when the string does not match any entry. */
static int lookup_str_to_int(const char *str, const char * const keys[], const int vals[], int count, int default_val)
{
    for (int i = 0; i < count; i++) {
        if (strncmp(str, keys[i], SETTING_ITEM_MAX_STR_LEN) == 0) {
            return vals[i];
        }
    }
    return default_val;
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

    static const char * const parity_keys[] = {
        UART_PARITY_DISABLE_STR, UART_PARITY_EVEN_STR, UART_PARITY_ODD_STR
    };
    static const int parity_vals[] = {
        UART_PARITY_DISABLE, UART_PARITY_EVEN, UART_PARITY_ODD
    };
    snprintf(key_buf, sizeof(key_buf), "parity_%d", index + 1);
    ESP_RETURN_ON_ERROR(setting_items_read(key_buf, value_str), TAG, "Failed to read parity for port %d", index + 1);
    serial_config->parity = (uart_parity_t)lookup_str_to_int(
        value_str, parity_keys, parity_vals, ARRAY_SIZE(parity_keys), UART_PARITY_DISABLE
    );

    static const char * const stopbits_keys[] = {
        UART_STOP_BITS_1_STR, UART_STOP_BITS_1_5_STR, UART_STOP_BITS_2_STR
    };
    static const int stopbits_vals[] = {
        UART_STOP_BITS_1, UART_STOP_BITS_1_5, UART_STOP_BITS_2
    };
    snprintf(key_buf, sizeof(key_buf), "stopbits_%d", index + 1);
    ESP_RETURN_ON_ERROR(setting_items_read(key_buf, value_str), TAG, "Failed to read stopbits for port %d", index + 1);
    serial_config->stopbits = (uart_stop_bits_t)lookup_str_to_int(
        value_str, stopbits_keys, stopbits_vals, ARRAY_SIZE(stopbits_keys), UART_STOP_BITS_2
    );

    static const char * const databits_keys[] = {
        UART_DATA_5_BITS_STR, UART_DATA_6_BITS_STR, UART_DATA_7_BITS_STR, UART_DATA_8_BITS_STR
    };
    static const int databits_vals[] = {
        UART_DATA_5_BITS, UART_DATA_6_BITS, UART_DATA_7_BITS, UART_DATA_8_BITS
    };
    snprintf(key_buf, sizeof(key_buf), "databits_%d", index + 1);
    ESP_RETURN_ON_ERROR(setting_items_read(key_buf, value_str), TAG, "Failed to read databits for port %d", index + 1);
    serial_config->databits = (uart_word_length_t)lookup_str_to_int(
        value_str, databits_keys, databits_vals, ARRAY_SIZE(databits_keys), UART_DATA_8_BITS
    );

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
    // Note: rs485_busy_monitor_init() and rs485_stats_init() have been moved to
    // port_manager_init() so they are called even when bridge_init() is not used directly.
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

    ESP_LOGD(TAG, "Port[%u]: Initializing...", index + 1);

    ESP_RETURN_ON_ERROR(read_serial_port_config(index, &bridge_current_cfg[index].serial_config),
                        TAG, "Port[%u]: Failed to read serial config for port", index + 1);
    ESP_RETURN_ON_ERROR(read_tcp_bridge_config(index, &bridge_current_cfg[index].bridge_mode, &bridge_current_cfg[index].bridge_ip,
                                                &bridge_current_cfg[index].bridge_port, &bridge_current_cfg[index].bridge_mb),
                        TAG, "Port[%u]: Failed to read bridge config", index + 1);

    if (bridge_current_cfg[index].bridge_mode == BRIDGE_MODE_DISABLED) {
        // port_mode is the authoritative on/off axis now: if a port is set to
        // tcp_bridge, its bridge_mode must be a valid server/client TCP role.
        // BRIDGE_MODE_DISABLED here means a corrupt/legacy bridge_mode value, so we
        // refuse to half-initialize. Returning an error (instead of the old
        // ESP_OK with initialized=false) lets port_manager_set_mode() roll the port
        // back rather than leave a zombie tcp_bridge that reports active but isn't.
        ESP_LOGE(TAG, "Port[%d]: tcp_bridge requires a valid server/client bridge_mode, "
                      "but an invalid/legacy value was found; refusing to initialize",
                 bridge_current_cfg[index].serial_config.port_num);
        return ESP_ERR_INVALID_STATE;
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

    // Note: sniffer_attach() is now called by port_manager after bridge_port_init(),
    // so it is not called here to avoid double-attach.
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

    ESP_LOGD(TAG, "Port[%u]: Deinitializing...", index + 1);
    // Note: sniffer_detach() is now called by port_manager before bridge_port_deinit(),
    // so it is not called here to avoid double-detach.
    if (cfg->bridge_mb) {
        modbus_tcp_deinit_port(index);
    } else {
        transparent_tcp_deinit_port(index);
    }

    bridge_ctx[index].initialized = false;
    bridge_ctx[index].serial_desc = NULL;

    ESP_LOGD(TAG, "Port[%u]: Deinitialized", index + 1);
    return ESP_OK;
}


static inline bool bridge_config_equal(const bridge_config_t *a, const bridge_config_t *b)
{
    return (a->serial_config.port_num == b->serial_config.port_num) &&
           (a->serial_config.tx_pin == b->serial_config.tx_pin) &&
           (a->serial_config.rx_pin == b->serial_config.rx_pin) &&
           (a->serial_config.dir_pin == b->serial_config.dir_pin) &&
           (a->serial_config.baudrate == b->serial_config.baudrate) &&
           (a->serial_config.parity == b->serial_config.parity) &&
           (a->serial_config.stopbits == b->serial_config.stopbits) &&
           (a->serial_config.databits == b->serial_config.databits) &&
           (a->bridge_mode == b->bridge_mode) &&
           (a->bridge_ip == b->bridge_ip) &&
           (a->bridge_port == b->bridge_port) &&
           (a->bridge_mb == b->bridge_mb);
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

    if (bridge_config_equal(&bridge_current_cfg[index], &new_cfg)) {
        return false;
    } else {
        return true;
    }
}

esp_err_t bridge_port_init_serial_only(unsigned index, serial_desc_t **serial_desc_out)
{
    if (index >= BRIDGES_COUNT || !serial_desc_out) {
        return ESP_ERR_INVALID_ARG;
    }

    serial_config_t serial_config = {0};
    ESP_RETURN_ON_ERROR(read_serial_port_config(index, &serial_config),
                        TAG, "Port[%u]: Failed to read serial config", index + 1);

    // Pass NULL as receive_handler; sniffer uses only sniff_handler set by sniffer_attach().
    serial_desc_t *desc = serial_init(&serial_config, NULL);
    if (!desc) {
        ESP_LOGE(TAG, "Port[%u]: Failed to initialize serial port", index + 1);
        return ESP_FAIL;
    }

    *serial_desc_out = desc;
    ESP_LOGI(TAG, "Port[%u]: Serial-only initialized", index + 1);
    return ESP_OK;
}

serial_desc_t *bridge_get_serial_desc(unsigned index)
{
    if (index >= BRIDGES_COUNT) {
        return NULL;
    }
    return bridge_ctx[index].serial_desc;
}

esp_err_t bridge_read_serial_config(unsigned index, serial_config_t *config)
{
    if (index >= BRIDGES_COUNT || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    return read_serial_port_config(index, config);
}

#ifdef __unittest_env__
    void bridge_reset(void)
    {
        memset(bridge_current_cfg, 0, sizeof(bridge_current_cfg));
        memset(bridge_ctx, 0, sizeof(bridge_ctx));
    }
#endif
