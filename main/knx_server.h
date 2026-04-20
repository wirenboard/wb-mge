/*
 * KNX IP Secure TCP server — C API for wb-mge integration.
 * Wraps the knxd Router with NCN5120 backend + tcptunsrv.
 */
#pragma once

#include "esp_err.h"
#include "driver/uart.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KNX_MAX_PASSWORD_LEN 64

typedef struct {
    uint16_t tcp_port;
    char device_auth[KNX_MAX_PASSWORD_LEN];
    char user_password[KNX_MAX_PASSWORD_LEN];
    uint8_t serial_number[6];
    uart_port_t uart_num;
    int uart_rx_pin;
    int uart_tx_pin;
    int uart_baud;
} knx_server_config_t;

esp_err_t knx_server_start(const knx_server_config_t *config);
esp_err_t knx_server_stop(void);
bool knx_server_is_running(void);

typedef struct {
    bool running;            /* knx server task is alive */
    bool bus_alive;          /* NCN5120 link is up (last heartbeat OK) */
    uint16_t tcp_port;       /* TCP port the secure tunnel listens on */
    uint32_t tx_count;       /* telegrams forwarded bus -> tunnel client */
    uint32_t rx_count;       /* telegrams forwarded tunnel client -> bus */
    uint8_t  clients_count;  /* total active TCP tunnel connections */
    uint8_t  secure_count;   /* of which are inside an IP Secure session */
} knx_server_stats_t;

void knx_server_get_stats(knx_server_stats_t *stats);

#ifdef __cplusplus
}
#endif
