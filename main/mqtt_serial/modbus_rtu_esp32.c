#include "modbus_rtu.h"
#include "modbus_frame.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const char *TAG = "modbus_rtu_esp32";

#define MB_UART_BUF_SIZE   512
#define MB_UART_QUEUE_SIZE 10

struct mb_rtu_port {
    uart_port_t uart_num;
    int         response_timeout_ms;
};

/*
 * Open (configure) an ESP32 UART for Modbus RTU.
 * device: "0", "1", or "2" — UART port number as string.
 * Pins must already be set externally (bridge_config sets them before calling us).
 * Here we only configure baud, parity, stop bits.
 */
mb_rtu_port_t *mb_rtu_open(const char *device, int baud, char parity,
                            int stop_bits, int response_timeout_ms)
{
    int port_num = (int)strtol(device, NULL, 10);
    if (port_num < 0 || port_num > 2) {
        ESP_LOGE(TAG, "Invalid UART port: %s", device);
        return NULL;
    }

    uart_parity_t uart_parity;
    switch (parity) {
        case 'E': uart_parity = UART_PARITY_EVEN;    break;
        case 'O': uart_parity = UART_PARITY_ODD;     break;
        default:  uart_parity = UART_PARITY_DISABLE; break;
    }

    uart_stop_bits_t uart_stop;
    uart_stop = (stop_bits == 2) ? UART_STOP_BITS_2 : UART_STOP_BITS_1;

    uart_config_t cfg = {
        .baud_rate  = baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = uart_parity,
        .stop_bits  = uart_stop,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config((uart_port_t)port_num, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return NULL;
    }

    /* Install driver without event queue — we use direct uart_read_bytes */
    err = uart_driver_install((uart_port_t)port_num,
                              MB_UART_BUF_SIZE, MB_UART_BUF_SIZE,
                              0, NULL, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        /* Driver already installed (shared port) — that is OK */
        ESP_LOGW(TAG, "UART%d driver already installed, reconfiguring only", port_num);
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return NULL;
    }

    /* RS-485 half-duplex mode */
    uart_set_mode((uart_port_t)port_num, UART_MODE_RS485_HALF_DUPLEX);

    mb_rtu_port_t *p = malloc(sizeof(*p));
    if (!p) return NULL;
    p->uart_num            = (uart_port_t)port_num;
    p->response_timeout_ms = (response_timeout_ms > 0) ? response_timeout_ms : 300;
    return p;
}

void mb_rtu_close(mb_rtu_port_t *port)
{
    if (!port) return;
    /* Do not call uart_driver_delete — the port may be shared with the TCP bridge */
    free(port);
}

/* ------------------------------------------------------------------
 * Low-level send / recv
 * ------------------------------------------------------------------ */

static int port_send(mb_rtu_port_t *p, const uint8_t *buf, int len)
{
    uart_flush(p->uart_num);
    int written = uart_write_bytes(p->uart_num, (const char *)buf, (size_t)len);
    if (written != len) {
        ESP_LOGE(TAG, "uart_write_bytes: wrote %d of %d", written, len);
        return -1;
    }
    /* Wait until all bytes are physically transmitted before switching to RX */
    uart_wait_tx_done(p->uart_num, pdMS_TO_TICKS(100));
    return 0;
}

static int port_recv(mb_rtu_port_t *p, uint8_t *buf, int max_len, int timeout_ms)
{
    /* Flush any stale RX data */
    uart_flush_input(p->uart_num);

    int total    = 0;
    int deadline = timeout_ms;

    while (total < max_len && deadline > 0) {
        int chunk = uart_read_bytes(p->uart_num, buf + total,
                                   (size_t)(max_len - total),
                                   pdMS_TO_TICKS(deadline));
        if (chunk <= 0) break;
        total   += chunk;
        deadline = 20; /* short inter-character gap: keep reading until silence */
    }
    return total;
}

/* ------------------------------------------------------------------
 * Request / response helpers (identical to POSIX version)
 * ------------------------------------------------------------------ */

#define crc16 modbus_crc16

static int send_request(mb_rtu_port_t *p, uint8_t slave,
                        const uint8_t *pdu, int pdu_len)
{
    uint8_t frame[256];
    if (pdu_len + 3 > (int)sizeof(frame)) return -1;
    frame[0] = slave;
    memcpy(frame + 1, pdu, (size_t)pdu_len);
    uint16_t crc = crc16(frame, pdu_len + 1);
    frame[pdu_len + 1] = (uint8_t)(crc & 0xFF);
    frame[pdu_len + 2] = (uint8_t)(crc >> 8);
    return port_send(p, frame, pdu_len + 3);
}

static int recv_response(mb_rtu_port_t *p, uint8_t slave, uint8_t fc,
                         uint8_t *rsp_pdu, int max_pdu)
{
    uint8_t frame[256];
    int n = port_recv(p, frame, sizeof(frame), p->response_timeout_ms);
    if (n < 4) {
        ESP_LOGW(TAG, "short response (%d bytes) slave=%d fc=%d", n, slave, fc);
        return -1;
    }

    uint16_t crc_rx   = frame[n-2] | ((uint16_t)frame[n-1] << 8);
    uint16_t crc_calc = crc16(frame, n - 2);
    if (crc_rx != crc_calc) {
        ESP_LOGW(TAG, "CRC error slave=%d fc=%d", slave, fc);
        return -1;
    }
    if (frame[0] != slave) {
        ESP_LOGW(TAG, "wrong slave %d vs %d", frame[0], slave);
        return -1;
    }
    if (frame[1] == (fc | 0x80)) {
        ESP_LOGW(TAG, "exception 0x%02X slave=%d fc=%d", frame[2], slave, fc);
        return -1;
    }
    if (frame[1] != fc) {
        ESP_LOGW(TAG, "wrong FC 0x%02X exp 0x%02X", frame[1], fc);
        return -1;
    }

    int pdu_len = n - 3;
    if (pdu_len > max_pdu) pdu_len = max_pdu;
    memcpy(rsp_pdu, frame + 1, (size_t)pdu_len);
    return pdu_len;
}

/* ------------------------------------------------------------------
 * FC01/FC02
 * ------------------------------------------------------------------ */
static int read_bits(mb_rtu_port_t *p, uint8_t slave, uint8_t fc,
                     uint16_t addr, uint16_t n_bits, uint8_t *bits)
{
    uint8_t pdu[5] = {fc,
        (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
        (uint8_t)(n_bits >> 8), (uint8_t)(n_bits & 0xFF)};
    if (send_request(p, slave, pdu, 5) < 0) return -1;

    uint8_t rsp[256];
    int rlen = recv_response(p, slave, fc, rsp, sizeof(rsp));
    if (rlen < 0) return -1;

    int byte_count = rsp[1];
    int max_data   = rlen - 2;
    if (max_data < 0) max_data = 0;
    if (byte_count > max_data) byte_count = max_data;
    for (int i = 0; i < n_bits; i++) {
        int bi = i / 8, bj = i % 8;
        bits[i] = (bi >= byte_count) ? 0 : ((rsp[2 + bi] >> bj) & 1);
    }
    return 0;
}

/* ------------------------------------------------------------------
 * FC03/FC04
 * ------------------------------------------------------------------ */
static int read_regs(mb_rtu_port_t *p, uint8_t slave, uint8_t fc,
                     uint16_t addr, uint16_t n_regs, uint16_t *regs)
{
    uint8_t pdu[5] = {fc,
        (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
        (uint8_t)(n_regs >> 8), (uint8_t)(n_regs & 0xFF)};
    if (send_request(p, slave, pdu, 5) < 0) return -1;

    uint8_t rsp[256];
    int rlen = recv_response(p, slave, fc, rsp, sizeof(rsp));
    if (rlen < 0) return -1;

    for (int i = 0; i < n_regs; i++) {
        int off = 2 + i * 2;
        regs[i] = (off + 1 >= rlen) ? 0 :
                  ((uint16_t)rsp[off] << 8) | rsp[off + 1];
    }
    return 0;
}

/* ------------------------------------------------------------------
 * FC05/FC06/FC16
 * ------------------------------------------------------------------ */
int mb_write_coil(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, int value)
{
    uint8_t pdu[5] = {0x05,
        (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
        value ? 0xFF : 0x00, 0x00};
    if (send_request(p, slave, pdu, 5) < 0) return -1;
    uint8_t rsp[8];
    return recv_response(p, slave, 0x05, rsp, sizeof(rsp)) >= 0 ? 0 : -1;
}

int mb_write_holding(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t value)
{
    uint8_t pdu[5] = {0x06,
        (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
        (uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
    if (send_request(p, slave, pdu, 5) < 0) return -1;
    uint8_t rsp[8];
    return recv_response(p, slave, 0x06, rsp, sizeof(rsp)) >= 0 ? 0 : -1;
}

int mb_write_holding_multi(mb_rtu_port_t *p, uint8_t slave, uint16_t addr,
                           uint16_t n_regs, const uint16_t *regs)
{
    int data_bytes = n_regs * 2;
    int pdu_len    = 6 + data_bytes;
    if (pdu_len > 255) return -1;
    uint8_t pdu[256];
    pdu[0] = 0x10;
    pdu[1] = (uint8_t)(addr >> 8);
    pdu[2] = (uint8_t)(addr & 0xFF);
    pdu[3] = (uint8_t)(n_regs >> 8);
    pdu[4] = (uint8_t)(n_regs & 0xFF);
    pdu[5] = (uint8_t)data_bytes;
    for (int i = 0; i < n_regs; i++) {
        pdu[6 + i*2]     = (uint8_t)(regs[i] >> 8);
        pdu[6 + i*2 + 1] = (uint8_t)(regs[i] & 0xFF);
    }
    if (send_request(p, slave, pdu, pdu_len) < 0) return -1;
    uint8_t rsp[8];
    return recv_response(p, slave, 0x10, rsp, sizeof(rsp)) >= 0 ? 0 : -1;
}

/* Public wrappers */
int mb_read_holding(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n, uint16_t *r)
    { return read_regs(p, slave, 0x03, addr, n, r); }
int mb_read_input(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n, uint16_t *r)
    { return read_regs(p, slave, 0x04, addr, n, r); }
int mb_read_coils(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n, uint8_t *b)
    { return read_bits(p, slave, 0x01, addr, n, b); }
int mb_read_discrete(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n, uint8_t *b)
    { return read_bits(p, slave, 0x02, addr, n, b); }
