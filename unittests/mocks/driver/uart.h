#pragma once

/**
 * @brief UART stop bits number
 */
typedef enum {
    UART_STOP_BITS_1   = 0x1,  /*!< stop bit: 1bit*/
    UART_STOP_BITS_1_5 = 0x2,  /*!< stop bit: 1.5bits*/
    UART_STOP_BITS_2   = 0x3,  /*!< stop bit: 2bits*/
    UART_STOP_BITS_MAX = 0x4,
} uart_stop_bits_t;

/**
 * @brief UART parity constants
 */
typedef enum {
    UART_PARITY_DISABLE  = 0x0,  /*!< Disable UART parity*/
    UART_PARITY_EVEN     = 0x2,  /*!< Enable UART even parity*/
    UART_PARITY_ODD      = 0x3   /*!< Enable UART odd parity*/
} uart_parity_t;

/**
 * @brief UART word length constants
 */
typedef enum {
    UART_DATA_5_BITS = 0x0, /*!< word length: 5bits*/
    UART_DATA_6_BITS = 0x1, /*!< word length: 6bits*/
    UART_DATA_7_BITS = 0x2, /*!< word length: 7bits*/
    UART_DATA_8_BITS = 0x3, /*!< word length: 8bits*/
    UART_DATA_BITS_MAX = 0x4,
} uart_word_length_t;
