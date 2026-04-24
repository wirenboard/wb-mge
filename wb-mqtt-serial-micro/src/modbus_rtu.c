#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE  /* for cfmakeraw on Linux */

#include "modbus_rtu.h"
#include "modbus_frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

/* ---------------------------------------------------------------
 * Platform layer: on Linux we use POSIX termios.
 * On an MCU this whole block (struct + open/close/send/recv)
 * gets replaced by the HAL (e.g. ESP-IDF uart_driver_*) while
 * the protocol logic above stays unchanged.
 * --------------------------------------------------------------- */
/* Use POSIX layer on Linux and macOS for the PoC */
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#define POSIX_PLATFORM 1
#endif

#ifdef POSIX_PLATFORM

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

struct mb_rtu_port {
    int fd;
    int baud;                /* for inter-frame delay calculation */
    int response_timeout_ms; /* max wait for device response      */
};

static speed_t baud_to_speed(int baud)
{
    switch (baud) {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

mb_rtu_port_t *mb_rtu_open(const char *device, int baud, char parity,
                            int stop_bits, int response_timeout_ms)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("mb_rtu_open: open");
        return NULL;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("mb_rtu_open: tcgetattr");
        close(fd);
        return NULL;
    }

    speed_t spd = baud_to_speed(baud);
    cfsetospeed(&tty, spd);
    cfsetispeed(&tty, spd);

    cfmakeraw(&tty);
    tty.c_cflag |= CLOCAL | CREAD;

    /* Parity */
    tty.c_cflag &= ~(PARENB | PARODD);
    if (parity == 'E') {
        tty.c_cflag |= PARENB;
    } else if (parity == 'O') {
        tty.c_cflag |= PARENB | PARODD;
    }

    /* Stop bits */
    if (stop_bits == 2)
        tty.c_cflag |= CSTOPB;
    else
        tty.c_cflag &= ~CSTOPB;

    /* 8 data bits */
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;

    /* Non-blocking reads with 100 ms timeout */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1; /* 100 ms */

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("mb_rtu_open: tcsetattr");
        close(fd);
        return NULL;
    }

    mb_rtu_port_t *p = malloc(sizeof(*p));
    if (!p) { close(fd); return NULL; }
    p->fd                  = fd;
    p->baud                = baud;
    p->response_timeout_ms = response_timeout_ms > 0 ? response_timeout_ms : 300;
    return p;
}

void mb_rtu_close(mb_rtu_port_t *port)
{
    if (!port) return;
    close(port->fd);
    free(port);
}

/* Send raw bytes */
static int port_send(mb_rtu_port_t *p, const uint8_t *buf, int len)
{
    int n = (int)write(p->fd, buf, (size_t)len);
    if (n != len) {
        perror("mb_rtu: write");
        return -1;
    }
    return 0;
}

/* Receive up to max_len bytes, timeout_ms total.
 * Returns number of bytes received or -1 on error. */
static int port_recv(mb_rtu_port_t *p, uint8_t *buf, int max_len, int timeout_ms)
{
    int total = 0;
    struct timeval deadline;
    gettimeofday(&deadline, NULL);
    deadline.tv_sec  += timeout_ms / 1000;
    deadline.tv_usec += (timeout_ms % 1000) * 1000;
    if (deadline.tv_usec >= 1000000) {
        deadline.tv_sec++;
        deadline.tv_usec -= 1000000;
    }

    while (total < max_len) {
        struct timeval now, rem;
        gettimeofday(&now, NULL);
        rem.tv_sec  = deadline.tv_sec  - now.tv_sec;
        rem.tv_usec = deadline.tv_usec - now.tv_usec;
        if (rem.tv_usec < 0) { rem.tv_sec--; rem.tv_usec += 1000000; }
        if (rem.tv_sec < 0) break;

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(p->fd, &rfds);
        int r = select(p->fd + 1, &rfds, NULL, NULL, &rem);
        if (r < 0)  { perror("mb_rtu: select"); return -1; }
        if (r == 0) break;   /* timeout */

        int n = (int)read(p->fd, buf + total, (size_t)(max_len - total));
        if (n <= 0) break;
        total += n;

        /* Inter-character gap: wait a bit; if no more data arrives, frame is done */
        struct timeval gap = {0, 5000}; /* 5 ms */
        fd_set rfds2;
        FD_ZERO(&rfds2);
        FD_SET(p->fd, &rfds2);
        r = select(p->fd + 1, &rfds2, NULL, NULL, &gap);
        if (r <= 0) break;
    }
    return total;
}

#else /* MCU ------------------------------------------------------- */
/*
 * Replace the block below with your UART HAL.
 * Typical ESP32-IDF implementation:
 *
 *   struct mb_rtu_port { uart_port_t uart_num; };
 *
 *   mb_rtu_port_t *mb_rtu_open(const char *device, int baud, char parity, int stop_bits, int response_timeout_ms) {
 *       uart_port_t num = atoi(device);  // or parse from string
 *       uart_config_t cfg = { .baud_rate = baud, .data_bits = UART_DATA_8_BITS,
 *           .parity = (parity=='E') ? UART_PARITY_EVEN :
 *                     (parity=='O') ? UART_PARITY_ODD : UART_PARITY_DISABLE,
 *           .stop_bits = (stop_bits==2) ? UART_STOP_BITS_2 : UART_STOP_BITS_1,
 *           .flow_ctrl = UART_HW_FLOWCTRL_DISABLE };
 *       uart_driver_install(num, 256, 256, 0, NULL, 0);
 *       uart_param_config(num, &cfg);
 *       mb_rtu_port_t *p = malloc(sizeof(*p));
 *       p->uart_num = num;
 *       return p;
 *   }
 *
 *   static int port_send(mb_rtu_port_t *p, const uint8_t *buf, int len) {
 *       uart_write_bytes(p->uart_num, buf, len);
 *       return 0;
 *   }
 *
 *   static int port_recv(mb_rtu_port_t *p, uint8_t *buf, int max_len, int timeout_ms) {
 *       return uart_read_bytes(p->uart_num, buf, max_len,
 *                              pdMS_TO_TICKS(timeout_ms));
 *   }
 */
#error "Implement port layer for your MCU (see POSIX block above as reference)"

#endif /* POSIX_PLATFORM */

/* Alias for internal use - implemented in modbus_frame.c */
#define crc16 modbus_crc16

/* ------------------------------------------------------------------
 * Low-level request/response
 * ------------------------------------------------------------------ */

/* Build and send a Modbus RTU request frame.
 * pdu: bytes after slave id (starts with FC byte), pdu_len bytes.
 * Returns 0 on success. */
static int send_request(mb_rtu_port_t *p, uint8_t slave,
                        const uint8_t *pdu, int pdu_len)
{
    /* Frame: [slave][pdu...][crc_lo][crc_hi] */
    uint8_t frame[256];
    if (pdu_len + 3 > (int)sizeof(frame)) return -1;

    frame[0] = slave;
    memcpy(frame + 1, pdu, (size_t)pdu_len);
    uint16_t crc = crc16(frame, pdu_len + 1);
    frame[pdu_len + 1] = (uint8_t)(crc & 0xFF);
    frame[pdu_len + 2] = (uint8_t)(crc >> 8);

    /* Flush input before sending (clear any stale data) */
#ifdef POSIX_PLATFORM
    tcflush(p->fd, TCIFLUSH);
#endif

    return port_send(p, frame, pdu_len + 3);
}

/* Receive a response frame.  Validates CRC, slave id, and FC.
 * rsp_pdu is filled with bytes after slave id (starting with FC).
 * Returns number of PDU bytes, or -1 on error. */
static int recv_response(mb_rtu_port_t *p, uint8_t slave, uint8_t fc,
                         uint8_t *rsp_pdu, int max_pdu)
{
    uint8_t frame[256];
    int n = port_recv(p, frame, sizeof(frame), p->response_timeout_ms);
    if (n < 4) {
        fprintf(stderr, "mb: short response (%d bytes) for slave=%d fc=%d\n",
                n, slave, fc);
        return -1;
    }

    /* Check CRC */
    uint16_t crc_rx   = frame[n-2] | ((uint16_t)frame[n-1] << 8);
    uint16_t crc_calc = crc16(frame, n - 2);
    if (crc_rx != crc_calc) {
        fprintf(stderr, "mb: CRC error (slave=%d fc=%d)\n", slave, fc);
        return -1;
    }

    if (frame[0] != slave) {
        fprintf(stderr, "mb: wrong slave id (%d vs %d)\n", frame[0], slave);
        return -1;
    }

    /* Check for exception response (FC | 0x80) */
    if (frame[1] == (fc | 0x80)) {
        fprintf(stderr, "mb: exception code 0x%02X (slave=%d fc=%d)\n",
                frame[2], slave, fc);
        return -1;
    }

    if (frame[1] != fc) {
        fprintf(stderr, "mb: wrong FC (got 0x%02X, exp 0x%02X)\n",
                frame[1], fc);
        return -1;
    }

    int pdu_len = n - 3; /* subtract slave, 2 CRC */
    if (pdu_len > max_pdu) pdu_len = max_pdu;
    memcpy(rsp_pdu, frame + 1, (size_t)pdu_len);
    return pdu_len;
}

/* ------------------------------------------------------------------
 * FC01/FC02: Read Coils / Read Discrete Inputs
 * ------------------------------------------------------------------ */
static int read_bits(mb_rtu_port_t *p, uint8_t slave, uint8_t fc,
                     uint16_t addr, uint16_t n_bits, uint8_t *bits)
{
    uint8_t pdu[5];
    pdu[0] = fc;
    pdu[1] = (uint8_t)(addr >> 8);
    pdu[2] = (uint8_t)(addr & 0xFF);
    pdu[3] = (uint8_t)(n_bits >> 8);
    pdu[4] = (uint8_t)(n_bits & 0xFF);

    if (send_request(p, slave, pdu, 5) < 0) return -1;

    uint8_t rsp[256];
    int rlen = recv_response(p, slave, fc, rsp, sizeof(rsp));
    if (rlen < 0) return -1;

    /* rsp[0]=FC, rsp[1]=byte count, rsp[2..] = data */
    int byte_count = rsp[1];
    /* Clamp against actual received data length to prevent OOB read
     * in case a misbehaving device sends a wrong byte_count. */
    int max_data_bytes = rlen - 2;  /* rlen includes FC and byte_count bytes */
    if (max_data_bytes < 0) max_data_bytes = 0;
    if (byte_count > max_data_bytes) byte_count = max_data_bytes;
    for (int i = 0; i < n_bits; i++) {
        int byte_idx = i / 8;
        int bit_idx  = i % 8;
        if (byte_idx >= byte_count) { bits[i] = 0; continue; }
        bits[i] = (rsp[2 + byte_idx] >> bit_idx) & 1;
    }
    return 0;
}

/* ------------------------------------------------------------------
 * FC03/FC04: Read Holding / Input Registers
 * ------------------------------------------------------------------ */
static int read_regs(mb_rtu_port_t *p, uint8_t slave, uint8_t fc,
                     uint16_t addr, uint16_t n_regs, uint16_t *regs)
{
    uint8_t pdu[5];
    pdu[0] = fc;
    pdu[1] = (uint8_t)(addr >> 8);
    pdu[2] = (uint8_t)(addr & 0xFF);
    pdu[3] = (uint8_t)(n_regs >> 8);
    pdu[4] = (uint8_t)(n_regs & 0xFF);

    if (send_request(p, slave, pdu, 5) < 0) return -1;

    uint8_t rsp[256];
    int rlen = recv_response(p, slave, fc, rsp, sizeof(rsp));
    if (rlen < 0) return -1;

    /* rsp[0]=FC, rsp[1]=byte count, rsp[2..] = data (big-endian words) */
    (void)rsp[1]; /* byte_count - not needed, bounds checked via rlen */
    for (int i = 0; i < n_regs; i++) {
        int off = 2 + i * 2;
        if (off + 1 >= rlen) { regs[i] = 0; continue; }
        regs[i] = ((uint16_t)rsp[off] << 8) | rsp[off + 1];
    }
    return 0;
}

/* ------------------------------------------------------------------
 * FC05: Write Single Coil
 * ------------------------------------------------------------------ */
int mb_write_coil(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, int value)
{
    uint8_t pdu[5];
    pdu[0] = 0x05;
    pdu[1] = (uint8_t)(addr >> 8);
    pdu[2] = (uint8_t)(addr & 0xFF);
    pdu[3] = value ? 0xFF : 0x00;
    pdu[4] = 0x00;

    if (send_request(p, slave, pdu, 5) < 0) return -1;

    uint8_t rsp[8];
    return recv_response(p, slave, 0x05, rsp, sizeof(rsp)) >= 0 ? 0 : -1;
}

/* ------------------------------------------------------------------
 * FC06: Write Single Holding Register
 * ------------------------------------------------------------------ */
int mb_write_holding(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t value)
{
    uint8_t pdu[5];
    pdu[0] = 0x06;
    pdu[1] = (uint8_t)(addr >> 8);
    pdu[2] = (uint8_t)(addr & 0xFF);
    pdu[3] = (uint8_t)(value >> 8);
    pdu[4] = (uint8_t)(value & 0xFF);

    if (send_request(p, slave, pdu, 5) < 0) return -1;

    uint8_t rsp[8];
    return recv_response(p, slave, 0x06, rsp, sizeof(rsp)) >= 0 ? 0 : -1;
}

/* ------------------------------------------------------------------
 * FC16: Write Multiple Holding Registers
 * ------------------------------------------------------------------ */
int mb_write_holding_multi(mb_rtu_port_t *p, uint8_t slave, uint16_t addr,
                           uint16_t n_regs, const uint16_t *regs)
{
    /* PDU: FC(1) + addr(2) + qty(2) + byte_count(1) + data(n_regs*2) */
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

/* ------------------------------------------------------------------
 * Public wrappers
 * ------------------------------------------------------------------ */
int mb_read_holding(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n_regs, uint16_t *regs)
    { return read_regs(p, slave, 0x03, addr, n_regs, regs); }

int mb_read_input(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n_regs, uint16_t *regs)
    { return read_regs(p, slave, 0x04, addr, n_regs, regs); }

int mb_read_coils(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n_bits, uint8_t *bits)
    { return read_bits(p, slave, 0x01, addr, n_bits, bits); }

int mb_read_discrete(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n_bits, uint8_t *bits)
    { return read_bits(p, slave, 0x02, addr, n_bits, bits); }
