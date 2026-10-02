#include "unity.h"

/*
 * r503: the Grow R503 packet protocol (main/fp_acs/r503.h).
 *
 * r503.c is #included with its UART reads, writes and flushes redirected to a scripted serial
 * line: what the test queues with r503_test_answer*() is what the sensor "sends" after the
 * command, and a read that finds nothing left waits out its whole timeout on the mock tick count,
 * as uart_read_bytes() would. The rest of the UART driver (install, config, pins) is the shared
 * mock.
 */

#include <stdbool.h>
#include <string.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

static int r503_test_uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length, TickType_t ticks_to_wait);
static int r503_test_uart_write_bytes(uart_port_t uart_num, const void *src, size_t size);
static esp_err_t r503_test_uart_flush_input(uart_port_t uart_num);

#define uart_read_bytes     r503_test_uart_read_bytes
#define uart_write_bytes    r503_test_uart_write_bytes
#define uart_flush_input    r503_test_uart_flush_input
#include "r503.c"
#undef uart_read_bytes
#undef uart_write_bytes
#undef uart_flush_input

#if !FP_ACS_ENABLED
    #error "r503_test needs FP_ACS_ENABLED: check DEFS in the Makefile"
#endif

#define R503_TEST_LINE_MAX          4096
#define R503_TEST_TX_MAX            64
#define R503_TEST_WAITS_MAX         64
#define R503_TEST_HEADER_LEN        9               // start code, address, identifier, length
#define R503_TEST_FOREIGN_ADDRESS   0x12345678u

// A valid packet with this identifier and content, queued as the sensor's answer.
#define R503_TEST_ANSWER(pid, ...)                                                  \
    do {                                                                            \
        const uint8_t r503_test_content_[] = {__VA_ARGS__};                         \
        r503_test_answer_packet((pid), r503_test_content_, sizeof(r503_test_content_)); \
    } while (0)

// Raw bytes queued as they are, valid or not.
#define R503_TEST_RAW(...)                                                          \
    do {                                                                            \
        const uint8_t r503_test_bytes_[] = {__VA_ARGS__};                           \
        r503_test_answer(r503_test_bytes_, sizeof(r503_test_bytes_));               \
    } while (0)

static uint8_t    r503_test_stale[R503_TEST_LINE_MAX];  // arrived before the command: a flush drops it
static size_t     r503_test_stale_len;
static size_t     r503_test_stale_pos;
static uint8_t    r503_test_rx[R503_TEST_LINE_MAX];     // the answer
static size_t     r503_test_rx_len;
static size_t     r503_test_rx_pos;
static uint8_t    r503_test_tx[R503_TEST_TX_MAX];
static size_t     r503_test_tx_len;
static int        r503_test_write_result;               // 0: the whole packet is taken
static bool       r503_test_read_fails;
static size_t     r503_test_chunk;                      // most bytes a read returns; 0: as many as asked
static TickType_t r503_test_latency;                    // ticks a read that returns bytes takes
static TickType_t r503_test_waits[R503_TEST_WAITS_MAX]; // ticks_to_wait of each read
static size_t     r503_test_wait_count;
static int        r503_test_flushes;
static int        r503_test_wrong_port;


// ── The serial line ──────────────────────────────────────────────────────────────────────

static size_t r503_test_take(const uint8_t *from, size_t len, size_t *pos, uint8_t *out, uint32_t want)
{
    size_t n = len - *pos;
    if (n > want) {
        n = want;
    }
    if ((r503_test_chunk > 0) && (n > r503_test_chunk)) {
        n = r503_test_chunk;
    }
    memcpy(out, from + *pos, n);
    *pos += n;
    return n;
}

static int r503_test_uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length, TickType_t ticks_to_wait)
{
    if (uart_num != UART_NUM_2) {
        r503_test_wrong_port++;
    }
    if (r503_test_wait_count < R503_TEST_WAITS_MAX) {
        r503_test_waits[r503_test_wait_count++] = ticks_to_wait;
    }
    if (r503_test_read_fails) {
        return -1;
    }
    size_t n = 0;
    if (r503_test_stale_pos < r503_test_stale_len) {
        n = r503_test_take(r503_test_stale, r503_test_stale_len, &r503_test_stale_pos, buf, length);
    } else if (r503_test_rx_pos < r503_test_rx_len) {
        n = r503_test_take(r503_test_rx, r503_test_rx_len, &r503_test_rx_pos, buf, length);
    }
    if (n == 0) {
        // Nothing arrives: the read waits out its timeout.
        mock_tick_count += ticks_to_wait;
        return 0;
    }
    mock_tick_count += r503_test_latency;
    return (int)n;
}

static int r503_test_uart_write_bytes(uart_port_t uart_num, const void *src, size_t size)
{
    if (uart_num != UART_NUM_2) {
        r503_test_wrong_port++;
    }
    if ((r503_test_tx_len + size) <= sizeof(r503_test_tx)) {
        memcpy(r503_test_tx + r503_test_tx_len, src, size);
        r503_test_tx_len += size;
    }
    return (r503_test_write_result != 0) ? r503_test_write_result : (int)size;
}

static esp_err_t r503_test_uart_flush_input(uart_port_t uart_num)
{
    if (uart_num != UART_NUM_2) {
        r503_test_wrong_port++;
    }
    r503_test_flushes++;
    r503_test_stale_len = 0;
    r503_test_stale_pos = 0;
    return ESP_OK;
}

static void r503_test_answer(const uint8_t *bytes, size_t len)
{
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(r503_test_rx), r503_test_rx_len + len);
    memcpy(r503_test_rx + r503_test_rx_len, bytes, len);
    r503_test_rx_len += len;
}

// A packet as the sensor frames it; sum_delta spoils its checksum.
static size_t r503_test_frame(uint8_t *out, uint32_t address, uint8_t pid, const uint8_t *content, size_t len,
                              uint16_t sum_delta)
{
    size_t pos = 0;
    uint16_t length = (uint16_t)(len + 2);
    out[pos++] = 0xEF;
    out[pos++] = 0x01;
    out[pos++] = (uint8_t)(address >> 24);
    out[pos++] = (uint8_t)(address >> 16);
    out[pos++] = (uint8_t)(address >> 8);
    out[pos++] = (uint8_t)address;
    out[pos++] = pid;
    out[pos++] = (uint8_t)(length >> 8);
    out[pos++] = (uint8_t)length;
    uint16_t sum = (uint16_t)(pid + (length >> 8) + (length & 0xFF));
    for (size_t i = 0; i < len; i++) {
        out[pos++] = content[i];
        sum = (uint16_t)(sum + content[i]);
    }
    sum = (uint16_t)(sum + sum_delta);
    out[pos++] = (uint8_t)(sum >> 8);
    out[pos++] = (uint8_t)sum;
    return pos;
}

static void r503_test_answer_packet(uint8_t pid, const uint8_t *content, size_t len)
{
    uint8_t frame[R503_TEST_LINE_MAX];
    r503_test_answer(frame, r503_test_frame(frame, 0xFFFFFFFFu, pid, content, len, 0));
}

void setUp(void)
{
    mock_uart_reset();
    mock_freertos_task_reset();
    mock_log_reset();

    r503_ready = false;
    r503_cmd = 0;
    r503_error[0] = '\0';

    r503_test_stale_len = 0;
    r503_test_stale_pos = 0;
    r503_test_rx_len = 0;
    r503_test_rx_pos = 0;
    r503_test_tx_len = 0;
    r503_test_write_result = 0;
    r503_test_read_fails = false;
    r503_test_chunk = 0;
    r503_test_latency = 0;
    r503_test_wait_count = 0;
    r503_test_flushes = 0;
    r503_test_wrong_port = 0;

    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_init());
    mock_log_reset();
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT(0, r503_test_wrong_port);
}


// ── r503_init() ──────────────────────────────────────────────────────────────────────────

static void test_init_installs_uart2_on_the_wbe2_pins_at_57600_8n1(void)
{
    TEST_ASSERT_EQUAL_INT(UART_NUM_2, mock_uart_driver_install_data.uart_num);
    TEST_ASSERT_EQUAL_INT(2048, mock_uart_driver_install_data.rx_buffer_size);
    TEST_ASSERT_EQUAL_INT(0, mock_uart_driver_install_data.tx_buffer_size);
    TEST_ASSERT_NULL(mock_uart_driver_install_data.uart_queue);
    TEST_ASSERT_EQUAL_INT(57600, mock_uart_param_config_data.config.baud_rate);
    TEST_ASSERT_EQUAL_INT(UART_DATA_8_BITS, mock_uart_param_config_data.config.data_bits);
    TEST_ASSERT_EQUAL_INT(UART_PARITY_DISABLE, mock_uart_param_config_data.config.parity);
    TEST_ASSERT_EQUAL_INT(UART_STOP_BITS_1, mock_uart_param_config_data.config.stop_bits);
    TEST_ASSERT_EQUAL_INT(UART_HW_FLOWCTRL_DISABLE, mock_uart_param_config_data.config.flow_ctrl);
    // TX on the slot's direction pin, where the sensor's input is wired; GPIO10 left alone.
    TEST_ASSERT_EQUAL_INT(UART_NUM_2, mock_uart_set_pin_data.uart_num);
    TEST_ASSERT_EQUAL_INT(13, mock_uart_set_pin_data.tx_pin);
    TEST_ASSERT_EQUAL_INT(4, mock_uart_set_pin_data.rx_pin);
    TEST_ASSERT_EQUAL_INT(UART_PIN_NO_CHANGE, mock_uart_set_pin_data.dir_pin);
    TEST_ASSERT_EQUAL_INT(UART_PIN_NO_CHANGE, mock_uart_set_pin_data.cts_pin);
}

static void test_commands_before_init_fail_without_touching_the_uart(void)
{
    r503_ready = false;
    mock_uart_reset();
    mock_uart_driver_install_data.result = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(ESP_FAIL, r503_init());

    uint8_t code = 0xAA;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("sensor UART not installed", r503_last_error());
    TEST_ASSERT_EQUAL_size_t(0, r503_test_tx_len);
    TEST_ASSERT_EQUAL_INT(0, r503_test_flushes);
}

static void test_failed_uart_setup_deletes_the_driver(void)
{
    r503_ready = false;
    mock_uart_reset();
    mock_uart_set_pin_data.result = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, r503_init());
    TEST_ASSERT_EQUAL_INT(1, mock_uart_driver_delete_data.called);
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, r503_gen_img(&code));
}


// ── Command packets ──────────────────────────────────────────────────────────────────────

static void test_gen_img_is_framed_for_the_factory_address(void)
{
    R503_TEST_ANSWER(0x07, 0x00);
    uint8_t code = 0xAA;
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_gen_img(&code));
    const uint8_t expected[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x01, 0x00, 0x05};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), r503_test_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r503_test_tx, sizeof(expected));
}

static void test_verify_password_sends_the_password_big_endian(void)
{
    R503_TEST_ANSWER(0x07, 0x00);
    uint8_t code = 0xAA;
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_verify_password(0x12345678u, &code));
    const uint8_t expected[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x07,
                                0x13, 0x12, 0x34, 0x56, 0x78, 0x01, 0x2F};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), r503_test_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r503_test_tx, sizeof(expected));
}

// The content of the last command, after the header.
static void r503_test_assert_content(const uint8_t *expected, size_t len)
{
    TEST_ASSERT_EQUAL_size_t(R503_TEST_HEADER_LEN + len + 2, r503_test_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, &r503_test_tx[R503_TEST_HEADER_LEN], len);
    r503_test_tx_len = 0;
}

static void test_every_command_carries_its_instruction_and_arguments(void)
{
    uint8_t code = 0;
    uint16_t value = 0;
    size_t raw_len = 0;
    uint8_t raw[4];

    R503_TEST_ANSWER(0x07, 0x01);
    r503_img2tz(2, &code);
    r503_test_assert_content((const uint8_t[]){0x02, 0x02}, 2);

    R503_TEST_ANSWER(0x07, 0x01);
    r503_search(1, 0x0000, 200, &value, &value, &code);
    r503_test_assert_content((const uint8_t[]){0x04, 0x01, 0x00, 0x00, 0x00, 0xC8}, 6);

    R503_TEST_ANSWER(0x07, 0x01);
    r503_reg_model(&code);
    r503_test_assert_content((const uint8_t[]){0x05}, 1);

    R503_TEST_ANSWER(0x07, 0x01);
    r503_store(1, 0x0123, &code);
    r503_test_assert_content((const uint8_t[]){0x06, 0x01, 0x01, 0x23}, 4);

    R503_TEST_ANSWER(0x07, 0x01);
    r503_delete_char(5, 1, &code);
    r503_test_assert_content((const uint8_t[]){0x0C, 0x00, 0x05, 0x00, 0x01}, 5);

    R503_TEST_ANSWER(0x07, 0x01);
    r503_aura_led(R503_LED_ON, 25, R503_LED_GREEN, 0, &code);
    r503_test_assert_content((const uint8_t[]){0x35, 0x03, 0x19, 0x04, 0x00}, 5);

    R503_TEST_ANSWER(0x07, 0x01);
    r503_read_capacity(&value, &code);
    r503_test_assert_content((const uint8_t[]){0x0F}, 1);

    R503_TEST_ANSWER(0x07, 0x01);
    r503_up_image(raw, sizeof(raw), &raw_len, &code);
    r503_test_assert_content((const uint8_t[]){0x0A}, 1);
}

static void test_each_command_waits_as_long_as_it_may_take(void)
{
    uint8_t code = 0;
    uint16_t value = 0;
    size_t raw_len = 0;
    uint8_t raw[4];
    struct {
        const char *name;
        unsigned    timeout_ms;
    } cases[] = {
        {"GenImg", 1000}, {"Img2Tz", 1000}, {"VfyPwd", 1000}, {"ReadSysPara", 1000}, {"AuraLed", 1000},
        {"Search", 3000}, {"RegModel", 3000}, {"Store", 3000}, {"DeletChar", 3000}, {"UpImage", 6000},
    };
    for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
        r503_test_wait_count = 0;
        R503_TEST_ANSWER(0x07, 0x01);
        switch (i) {
        case 0: r503_gen_img(&code); break;
        case 1: r503_img2tz(1, &code); break;
        case 2: r503_verify_password(0, &code); break;
        case 3: r503_read_capacity(&value, &code); break;
        case 4: r503_aura_led(1, 1, 1, 0, &code); break;
        case 5: r503_search(1, 0, 10, &value, &value, &code); break;
        case 6: r503_reg_model(&code); break;
        case 7: r503_store(1, 0, &code); break;
        case 8: r503_delete_char(0, 1, &code); break;
        default: r503_up_image(raw, sizeof(raw), &raw_len, &code); break;
        }
        TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(0, r503_test_wait_count, cases[i].name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(pdMS_TO_TICKS(cases[i].timeout_ms), r503_test_waits[0], cases[i].name);
    }
}

static void test_input_is_flushed_before_every_command(void)
{
    // A late "no finger" from an earlier exchange is waiting; the sensor then answers "ok".
    uint8_t stale[32];
    r503_test_stale_len = r503_test_frame(stale, 0xFFFFFFFFu, 0x07, (const uint8_t[]){0x02}, 1, 0);
    memcpy(r503_test_stale, stale, r503_test_stale_len);
    R503_TEST_ANSWER(0x07, 0x00);

    uint8_t code = 0xAA;
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_HEX8(R503_CODE_OK, code);
    TEST_ASSERT_EQUAL_INT(1, r503_test_flushes);
}


// ── Acknowledges ─────────────────────────────────────────────────────────────────────────

static void test_acknowledge_gives_the_confirmation_code(void)
{
    R503_TEST_ANSWER(0x07, 0x02);
    uint8_t code = 0xAA;
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_HEX8(R503_CODE_NO_FINGER, code);
    TEST_ASSERT_EQUAL_STRING("", r503_last_error());
    TEST_ASSERT_EQUAL_size_t(0, mock_log_count());
}

static void test_answer_read_a_byte_at_a_time_is_assembled(void)
{
    r503_test_chunk = 1;
    R503_TEST_ANSWER(0x07, 0x00, 0x00, 0x05, 0x00, 0x99);
    uint8_t code = 0xAA;
    uint16_t page = 0;
    uint16_t score = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_search(1, 0, 200, &page, &score, &code));
    TEST_ASSERT_EQUAL_UINT16(5, page);
    TEST_ASSERT_EQUAL_UINT16(0x99, score);
}

static void test_read_capacity_takes_word_2_of_the_basic_parameters(void)
{
    // Status register, system identifier, library size 0x00C8, ...
    R503_TEST_ANSWER(0x07, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0xC8, 0x00, 0x03,
                     0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x02, 0x00, 0x06);
    uint8_t code = 0xAA;
    uint16_t capacity = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_read_capacity(&capacity, &code));
    TEST_ASSERT_EQUAL_HEX8(R503_CODE_OK, code);
    TEST_ASSERT_EQUAL_UINT16(200, capacity);
}

static void test_refusal_leaves_the_outputs_alone_and_needs_no_payload(void)
{
    R503_TEST_ANSWER(0x07, R503_CODE_NOT_FOUND);
    uint8_t code = 0xAA;
    uint16_t page = 0x1111;
    uint16_t score = 0x2222;
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_search(1, 0, 200, &page, &score, &code));
    TEST_ASSERT_EQUAL_HEX8(R503_CODE_NOT_FOUND, code);
    TEST_ASSERT_EQUAL_HEX16(0x1111, page);
    TEST_ASSERT_EQUAL_HEX16(0x2222, score);
}

static void test_short_acknowledge_of_a_success_is_an_error(void)
{
    R503_TEST_ANSWER(0x07, 0x00, 0x00, 0x00, 0x00, 0x09);
    uint8_t code = 0xAA;
    uint16_t capacity = 0x1234;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_SIZE, r503_read_capacity(&capacity, &code));
    TEST_ASSERT_EQUAL_STRING("short acknowledge: 5 of 17 bytes", r503_last_error());
    TEST_ASSERT_EQUAL_HEX16(0x1234, capacity);
}

static void test_longest_acknowledge_fits(void)
{
    uint8_t content[256] = {0};
    r503_test_answer_packet(0x07, content, sizeof(content));
    uint8_t code = 0xAA;
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_HEX8(R503_CODE_OK, code);
}


// ── What is skipped and what is refused ──────────────────────────────────────────────────

static void test_bytes_before_the_start_code_are_skipped(void)
{
    R503_TEST_RAW(0x55, 0x00, 0xEF);                // power-up noise, then a lone high byte
    R503_TEST_ANSWER(0x07, 0x00);
    uint8_t code = 0xAA;
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_HEX8(R503_CODE_OK, code);
    TEST_ASSERT_EQUAL_STRING("", r503_last_error());
}

static void test_silence_is_a_timeout_named_no_answer_and_not_logged(void)
{
    uint8_t code = 0xAA;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("no answer", r503_last_error());
    TEST_ASSERT_EQUAL_size_t(0, mock_log_count());
}

static void test_bytes_without_a_start_code_are_counted(void)
{
    R503_TEST_RAW(0x55, 0x00);
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("no start code; stray bytes: 2", r503_last_error());
}

static void test_header_cut_short_is_a_timeout(void)
{
    R503_TEST_RAW(0xEF, 0x01, 0xFF, 0xFF);
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("packet header cut short", r503_last_error());
}

static void test_content_cut_short_is_a_timeout(void)
{
    R503_TEST_RAW(0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x03, 0x00);
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("packet of length 3 cut short", r503_last_error());
}

static void test_packet_from_another_address_is_refused(void)
{
    uint8_t frame[32];
    r503_test_answer(frame, r503_test_frame(frame, R503_TEST_FOREIGN_ADDRESS, 0x07, (const uint8_t[]){0x00}, 1, 0));
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("packet from address 0x12345678", r503_last_error());
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_level(ESP_LOG_WARN, "Command 0x01: packet from address 0x12345678"));
}

static void test_unexpected_packet_identifier_is_refused(void)
{
    R503_TEST_ANSWER(0x01, 0x00);
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("unexpected packet identifier 0x01", r503_last_error());
}

static void test_length_too_short_for_a_checksum_is_refused(void)
{
    R503_TEST_RAW(0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x01);
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("packet length 1 out of range", r503_last_error());
}

static void test_length_over_the_largest_packet_is_refused(void)
{
    R503_TEST_RAW(0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x01, 0x03);
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("packet length 259 out of range", r503_last_error());
}

static void test_checksum_mismatch_is_refused_with_both_sums(void)
{
    uint8_t frame[32];
    r503_test_answer(frame, r503_test_frame(frame, 0xFFFFFFFFu, 0x07, (const uint8_t[]){0x00}, 1, 1));
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("checksum mismatch: received 0x000B, computed 0x000A", r503_last_error());
}

static void test_data_packet_in_place_of_an_acknowledge_is_refused(void)
{
    R503_TEST_ANSWER(0x02, 0x00);
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("not an acknowledge: identifier 0x02, 1 bytes", r503_last_error());
}

static void test_acknowledge_without_a_code_is_refused(void)
{
    r503_test_answer_packet(0x07, NULL, 0);
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("not an acknowledge: identifier 0x07, 0 bytes", r503_last_error());
}

static void test_uart_that_refuses_the_write_fails_the_command(void)
{
    r503_test_write_result = -1;
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_FAIL, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("UART write failed", r503_last_error());
}

static void test_uart_that_fails_a_read_fails_the_command(void)
{
    r503_test_read_fails = true;
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_FAIL, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("UART read failed", r503_last_error());
}

static void test_reason_is_cleared_by_the_next_command(void)
{
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("no answer", r503_last_error());
    R503_TEST_ANSWER(0x07, 0x00);
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_gen_img(&code));
    TEST_ASSERT_EQUAL_STRING("", r503_last_error());
}


// ── UpImage ──────────────────────────────────────────────────────────────────────────────

static void test_image_is_assembled_from_its_data_packets(void)
{
    R503_TEST_ANSWER(0x07, 0x00);
    R503_TEST_ANSWER(0x02, 0x11, 0x22, 0x33, 0x44);
    R503_TEST_ANSWER(0x02, 0x55, 0x66, 0x77, 0x88);
    R503_TEST_ANSWER(0x08, 0x99, 0xAA);
    uint8_t raw[16] = {0};
    size_t raw_len = 99;
    uint8_t code = 0xAA;
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_up_image(raw, sizeof(raw), &raw_len, &code));
    TEST_ASSERT_EQUAL_HEX8(R503_CODE_OK, code);
    TEST_ASSERT_EQUAL_size_t(10, raw_len);
    const uint8_t expected[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, raw, sizeof(expected));
}

static void test_image_refused_by_the_sensor_reads_no_data(void)
{
    R503_TEST_ANSWER(0x07, 0x0F);
    R503_TEST_ANSWER(0x08, 0x99);
    uint8_t raw[16];
    size_t raw_len = 99;
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_up_image(raw, sizeof(raw), &raw_len, &code));
    TEST_ASSERT_EQUAL_HEX8(0x0F, code);
    TEST_ASSERT_EQUAL_size_t(0, raw_len);
    TEST_ASSERT_LESS_THAN_size_t(r503_test_rx_len, r503_test_rx_pos);
}

static void test_image_larger_than_the_buffer_is_refused(void)
{
    R503_TEST_ANSWER(0x07, 0x00);
    R503_TEST_ANSWER(0x02, 0x11, 0x22, 0x33, 0x44);
    R503_TEST_ANSWER(0x08, 0x55, 0x66, 0x77, 0x88);
    uint8_t raw[6];
    size_t raw_len = 0;
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_SIZE, r503_up_image(raw, sizeof(raw), &raw_len, &code));
    TEST_ASSERT_EQUAL_STRING("more than 6 bytes of image data", r503_last_error());
}

static void test_acknowledge_among_the_image_data_is_refused(void)
{
    R503_TEST_ANSWER(0x07, 0x00);
    R503_TEST_ANSWER(0x02, 0x11);
    R503_TEST_ANSWER(0x07, 0x00);
    uint8_t raw[16];
    size_t raw_len = 0;
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, r503_up_image(raw, sizeof(raw), &raw_len, &code));
    TEST_ASSERT_EQUAL_STRING("packet identifier 0x07 among the image data", r503_last_error());
}

static void test_image_that_stops_arriving_is_a_timeout(void)
{
    R503_TEST_ANSWER(0x07, 0x00);
    R503_TEST_ANSWER(0x02, 0x11);
    uint8_t raw[16];
    size_t raw_len = 0;
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, r503_up_image(raw, sizeof(raw), &raw_len, &code));
    TEST_ASSERT_EQUAL_STRING("no answer", r503_last_error());
    TEST_ASSERT_EQUAL_size_t(1, raw_len);
}

static void test_one_deadline_covers_the_whole_image(void)
{
    // Every read that returns bytes takes 100 ticks; each packet takes four reads (the two bytes of
    // the start code, the header, the rest).
    r503_test_latency = 100;
    R503_TEST_ANSWER(0x07, 0x00);
    R503_TEST_ANSWER(0x08, 0x11);
    uint8_t raw[16];
    size_t raw_len = 0;
    uint8_t code = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, r503_up_image(raw, sizeof(raw), &raw_len, &code));
    TEST_ASSERT_EQUAL_size_t(8, r503_test_wait_count);
    TEST_ASSERT_EQUAL_UINT32(pdMS_TO_TICKS(6000), r503_test_waits[0]);
    TEST_ASSERT_EQUAL_UINT32(pdMS_TO_TICKS(6000) - 400, r503_test_waits[4]);

    // A transfer slower than the deadline as a whole fails, however fast each packet is.
    setUp();
    r503_test_latency = 400;
    R503_TEST_ANSWER(0x07, 0x00);
    R503_TEST_ANSWER(0x02, 0x11);
    R503_TEST_ANSWER(0x08, 0x22);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, r503_up_image(raw, sizeof(raw), &raw_len, &code));
}


// ── r503_code_str() ──────────────────────────────────────────────────────────────────────

static void test_confirmation_codes_have_names(void)
{
    TEST_ASSERT_EQUAL_STRING("ok", r503_code_str(0x00));
    TEST_ASSERT_EQUAL_STRING("packet receive error", r503_code_str(0x01));
    TEST_ASSERT_EQUAL_STRING("no finger", r503_code_str(0x02));
    TEST_ASSERT_EQUAL_STRING("image capture failed", r503_code_str(0x03));
    TEST_ASSERT_EQUAL_STRING("image too messy", r503_code_str(0x06));
    TEST_ASSERT_EQUAL_STRING("too few features", r503_code_str(0x07));
    TEST_ASSERT_EQUAL_STRING("no match", r503_code_str(0x08));
    TEST_ASSERT_EQUAL_STRING("not found", r503_code_str(0x09));
    TEST_ASSERT_EQUAL_STRING("the takes do not match", r503_code_str(0x0A));
    TEST_ASSERT_EQUAL_STRING("slot out of range", r503_code_str(0x0B));
    TEST_ASSERT_EQUAL_STRING("image upload failed", r503_code_str(0x0F));
    TEST_ASSERT_EQUAL_STRING("template delete failed", r503_code_str(0x10));
    TEST_ASSERT_EQUAL_STRING("wrong password", r503_code_str(0x13));
    TEST_ASSERT_EQUAL_STRING("no valid image", r503_code_str(0x15));
    TEST_ASSERT_EQUAL_STRING("flash write error", r503_code_str(0x18));
    TEST_ASSERT_EQUAL_STRING("password not verified", r503_code_str(0x21));
    TEST_ASSERT_EQUAL_STRING("sensor error", r503_code_str(0x04));
    TEST_ASSERT_EQUAL_STRING("sensor error", r503_code_str(0xFF));
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_installs_uart2_on_the_wbe2_pins_at_57600_8n1);
    RUN_TEST(test_commands_before_init_fail_without_touching_the_uart);
    RUN_TEST(test_failed_uart_setup_deletes_the_driver);

    RUN_TEST(test_gen_img_is_framed_for_the_factory_address);
    RUN_TEST(test_verify_password_sends_the_password_big_endian);
    RUN_TEST(test_every_command_carries_its_instruction_and_arguments);
    RUN_TEST(test_each_command_waits_as_long_as_it_may_take);
    RUN_TEST(test_input_is_flushed_before_every_command);

    RUN_TEST(test_acknowledge_gives_the_confirmation_code);
    RUN_TEST(test_answer_read_a_byte_at_a_time_is_assembled);
    RUN_TEST(test_read_capacity_takes_word_2_of_the_basic_parameters);
    RUN_TEST(test_refusal_leaves_the_outputs_alone_and_needs_no_payload);
    RUN_TEST(test_short_acknowledge_of_a_success_is_an_error);
    RUN_TEST(test_longest_acknowledge_fits);

    RUN_TEST(test_bytes_before_the_start_code_are_skipped);
    RUN_TEST(test_silence_is_a_timeout_named_no_answer_and_not_logged);
    RUN_TEST(test_bytes_without_a_start_code_are_counted);
    RUN_TEST(test_header_cut_short_is_a_timeout);
    RUN_TEST(test_content_cut_short_is_a_timeout);
    RUN_TEST(test_packet_from_another_address_is_refused);
    RUN_TEST(test_unexpected_packet_identifier_is_refused);
    RUN_TEST(test_length_too_short_for_a_checksum_is_refused);
    RUN_TEST(test_length_over_the_largest_packet_is_refused);
    RUN_TEST(test_checksum_mismatch_is_refused_with_both_sums);
    RUN_TEST(test_data_packet_in_place_of_an_acknowledge_is_refused);
    RUN_TEST(test_acknowledge_without_a_code_is_refused);
    RUN_TEST(test_uart_that_refuses_the_write_fails_the_command);
    RUN_TEST(test_uart_that_fails_a_read_fails_the_command);
    RUN_TEST(test_reason_is_cleared_by_the_next_command);

    RUN_TEST(test_image_is_assembled_from_its_data_packets);
    RUN_TEST(test_image_refused_by_the_sensor_reads_no_data);
    RUN_TEST(test_image_larger_than_the_buffer_is_refused);
    RUN_TEST(test_acknowledge_among_the_image_data_is_refused);
    RUN_TEST(test_image_that_stops_arriving_is_a_timeout);
    RUN_TEST(test_one_deadline_covers_the_whole_image);

    RUN_TEST(test_confirmation_codes_have_names);

    return UNITY_END();
}
