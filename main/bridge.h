#pragma once

#include "esp_err.h"

esp_err_t bridge_init(void);

typedef enum {
    TCP_SERVER_1 = 0,
    TCP_SERVER_2 = 1,
} tcp_server_num_t;

int tcp_server_active_connections(tcp_server_num_t server_num);
