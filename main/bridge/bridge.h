#pragma once

#include "esp_err.h"
#include "serial.h"
#include <stdbool.h>


#define BRIDGES_COUNT                 2


typedef enum {
    TCP_SERVER_1 = 0,
    TCP_SERVER_2 = 1,
    TCP_SERVER_COUNT
} tcp_server_num_t;

typedef enum {
    BRIDGE_MODE_DISABLED = 0,
    BRIDGE_MODE_SERVER,
    BRIDGE_MODE_CLIENT,
} bridge_mode_t;


esp_err_t bridge_init(void);

esp_err_t bridge_port_init(unsigned index);
esp_err_t bridge_port_deinit(unsigned index);
bool bridge_port_check_settings_changed(unsigned index);

int tcp_server_active_connections(tcp_server_num_t server_num);

// Initialize only the serial port for the given bridge index, without any TCP layer.
// Reads serial config from NVS (baudrate/parity/stopbits/databits).
// Returns serial_desc via out parameter.
// Used by port_manager for SNIFFER and CACHE_BUS modes.
esp_err_t bridge_port_init_serial_only(unsigned index, serial_desc_t **serial_desc_out);

// Return the serial_desc for the given port index, or NULL if not initialized.
serial_desc_t *bridge_get_serial_desc(unsigned index);

// Read serial port configuration from NVS for the given port index.
// Returns ESP_OK and fills *config on success.
// Used by port_manager to check whether serial-only parameters have changed.
esp_err_t bridge_read_serial_config(unsigned index, serial_config_t *config);
