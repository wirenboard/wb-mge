#pragma once

/*
 * esp_netif_sntp.h, reduced to what fp_log_clock_start() uses: the config, its default
 * initialiser for one server and esp_netif_sntp_init(), which records the server it was given.
 */

#include "esp_err.h"

typedef struct {
    int         num_of_servers;
    const char *servers[1];
} esp_sntp_config_t;

#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server)   { .num_of_servers = 1, .servers = { server } }

esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config);

extern int         mock_sntp_init_calls;
extern const char *mock_sntp_server;
extern esp_err_t   mock_sntp_init_result;

void mock_sntp_reset(void);
