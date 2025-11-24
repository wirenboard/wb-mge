#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#define MOCK_DATA_FROM_UART_READ        "HELLO_WORLD_FROM_MGE"

#define UART_PIN_NO_CHANGE              (-1)

typedef enum {
    UART_DATA_5_BITS   = 0x0,    /*!< word length: 5bits*/
    UART_DATA_6_BITS   = 0x1,    /*!< word length: 6bits*/
    UART_DATA_7_BITS   = 0x2,    /*!< word length: 7bits*/
    UART_DATA_8_BITS   = 0x3,    /*!< word length: 8bits*/
    UART_DATA_BITS_MAX = 0x4,
} uart_word_length_t;

typedef enum {
    UART_STOP_BITS_1   = 0x1,  /*!< stop bit: 1bit*/
    UART_STOP_BITS_1_5 = 0x2,  /*!< stop bit: 1.5bits*/
    UART_STOP_BITS_2   = 0x3,  /*!< stop bit: 2bits*/
    UART_STOP_BITS_MAX = 0x4,
} uart_stop_bits_t;

typedef enum {
    UART_PARITY_DISABLE  = 0x0,  /*!< Disable UART parity*/
    UART_PARITY_EVEN     = 0x2,  /*!< Enable UART even parity*/
    UART_PARITY_ODD      = 0x3   /*!< Enable UART odd parity*/
} uart_parity_t;

typedef enum {
    UART_NUM_0,                         /*!< UART port 0 */
    UART_NUM_1,                         /*!< UART port 1 */
    UART_NUM_2,                         /*!< UART port 2 */
    UART_NUM_MAX,                       /*!< UART port max */
} uart_port_t;

typedef enum {
    UART_DATA,              /*!< UART data event*/
    UART_BREAK,             /*!< UART break event*/
    UART_BUFFER_FULL,       /*!< UART RX buffer full event*/
    UART_FIFO_OVF,          /*!< UART FIFO overflow event*/
    UART_FRAME_ERR,         /*!< UART RX frame error event*/
    UART_PARITY_ERR,        /*!< UART RX parity event*/
    UART_DATA_BREAK,        /*!< UART TX data and break event*/
    UART_PATTERN_DET,       /*!< UART pattern detected */
    UART_EVENT_MAX,         /*!< UART event max index*/
} uart_event_type_t;

typedef enum {
    UART_MODE_RS485_HALF_DUPLEX = 0x01,         /*!< mode: half duplex RS485 UART mode control by RTS pin */
} uart_mode_t;

typedef enum {
    UART_HW_FLOWCTRL_DISABLE = 0x0,   /*!< disable hardware flow control*/
} uart_hw_flowcontrol_t;

typedef enum {
    SOC_MOD_CLK_PLL_F40M = 5,
} soc_module_clk_t;

typedef enum {
    UART_SCLK_DEFAULT = SOC_MOD_CLK_PLL_F40M,  /*!< UART source clock default choice is PLL_F40M */
} soc_periph_uart_clk_src_legacy_t;

typedef soc_periph_uart_clk_src_legacy_t uart_sclk_t;

typedef struct {
    uart_event_type_t type; /*!< UART event type */
    size_t size;            /*!< UART data size for UART_DATA event*/
    bool timeout_flag;      /*!< UART data read timeout flag for UART_DATA event (no new data received during configured RX TOUT)*/
} uart_event_t;

typedef struct {
    int baud_rate;                      /*!< UART baud rate*/
    uart_word_length_t data_bits;       /*!< UART byte size*/
    uart_parity_t parity;               /*!< UART parity mode*/
    uart_stop_bits_t stop_bits;         /*!< UART stop bits*/
    uart_hw_flowcontrol_t flow_ctrl;    /*!< UART HW flow control mode (cts/rts)*/
    uint8_t rx_flow_ctrl_thresh;        /*!< UART HW RTS threshold*/
    union {
        uart_sclk_t source_clk;             /*!< UART source clock selection */
    };
    struct {
        uint32_t allow_pd: 1;               /*!< If set, driver allows the power domain to be powered off when system enters sleep mode.
                                                 This can save power, but at the expense of more RAM being consumed to save register context. */
        uint32_t backup_before_sleep: 1;    /*!< @deprecated, same meaning as allow_pd */
    } flags;                                /*!< Configuration flags */
} uart_config_t;

esp_err_t uart_flush_input(uart_port_t uart_num);
esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size,
                              int tx_buffer_size, int event_queue_size,
                              QueueHandle_t *uart_queue, int intr_alloc_flags);
esp_err_t uart_driver_delete(uart_port_t uart_num);
esp_err_t uart_param_config(uart_port_t uart_num, const uart_config_t *uart_config);
esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num, int rts_io_num, int cts_io_num);
esp_err_t uart_set_mode(uart_port_t uart_num, uart_mode_t mode);
esp_err_t uart_set_rx_timeout(uart_port_t uart_num, const uint8_t tout_thresh);
esp_err_t uart_wait_tx_done(uart_port_t uart_num, TickType_t ticks_to_wait);
int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length, TickType_t ticks_to_wait);
int uart_write_bytes(uart_port_t uart_num, const void *src, size_t size);

typedef struct {
    int called;
    uart_port_t uart_num;
} mock_uart_flush_input_t;

typedef struct {
    int called;
    uart_port_t uart_num;
    int rx_buffer_size;
    int tx_buffer_size;
    int event_queue_size;
    QueueHandle_t *uart_queue;
    int intr_alloc_flags;
    esp_err_t result;
} mock_uart_driver_install_t;

typedef struct {
    int called;
    uart_port_t uart_num;
} mock_uart_driver_delete_t;

typedef struct {
    int called;
    uart_port_t uart_num;
    uart_config_t config;
    esp_err_t result;
} mock_uart_param_config_t;

typedef struct {
    int called;
    uart_port_t uart_num;
    int tx_pin;
    int rx_pin;
    int dir_pin;
    int cts_pin;
    esp_err_t result;
} mock_uart_set_pin_t;

typedef struct {
    int called;
    uart_port_t uart_num;
    uart_mode_t mode;
    esp_err_t result;
} mock_uart_set_mode_t;

typedef struct {
    int called;
    uart_port_t uart_num;
    uint8_t rx_timeout;
    esp_err_t result;
} mock_uart_set_rx_timeout_t;

typedef struct {
    int called;
    uart_port_t uart_num;
    TickType_t ticks_to_wait;
    esp_err_t result;
} mock_uart_wait_tx_done_t;

typedef struct {
    int called;
    uart_port_t uart_num;
    uint32_t length;
    TickType_t ticks_to_wait;
} mock_uart_read_bytes_t;

typedef struct {
    int called;
    uart_port_t uart_num;
    void *src;
    size_t size;
    int return_value;
} mock_uart_write_bytes_t;

extern mock_uart_flush_input_t mock_uart_flush_input_data;
extern mock_uart_driver_install_t mock_uart_driver_install_data;
extern mock_uart_driver_delete_t mock_uart_driver_delete_data;
extern mock_uart_param_config_t mock_uart_param_config_data;
extern mock_uart_set_pin_t mock_uart_set_pin_data;
extern mock_uart_set_mode_t mock_uart_set_mode_data;
extern mock_uart_set_rx_timeout_t mock_uart_set_rx_timeout_data;
extern mock_uart_wait_tx_done_t mock_uart_wait_tx_done_data;
extern mock_uart_read_bytes_t mock_uart_read_bytes_data;
extern mock_uart_write_bytes_t mock_uart_write_bytes_data;

void mock_uart_reset(void);
