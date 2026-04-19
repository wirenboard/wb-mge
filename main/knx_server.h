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

#ifdef __cplusplus
}
#endif
