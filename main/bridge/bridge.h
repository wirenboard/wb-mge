#pragma once

#include "esp_err.h"
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
