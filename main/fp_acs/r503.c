#include "r503.h"

#include "fp_acs.h"

#if FP_ACS_ENABLED

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "r503";

// The WBE2 slot of WB-MGU, plain UART, no RTS/CTS. The sensor's input is wired to the slot's
// direction pin (GPIO13) rather than to its TX pin (GPIO10), so TX is routed there and GPIO10 is
// left alone.
#define R503_UART_NUM                   UART_NUM_2
#define R503_UART_TX_PIN                SERIAL_IO_PIN_2
#define R503_UART_RX_PIN                SERIAL_INPUT_PIN_2
#define R503_UART_BAUD_RATE             57600
#define R503_UART_RX_BUFFER_SIZE        2048    // an UpImage burst is read as it arrives; this is slack
#define R503_UART_TX_BUFFER_SIZE        0       // writes block until the FIFO takes them

// Packet: start code (2) | address (4) | identifier (1) | length (2) | content | checksum (2).
// Multi-byte fields are big-endian; length counts the content and the checksum; the checksum is
// the 16-bit sum of the identifier, both length bytes and the content.
#define R503_START_HIGH                 0xEF
#define R503_START_LOW                  0x01
#define R503_ADDRESS                    0xFFFFFFFFu     // factory address
#define R503_ADDRESS_LEN                4
#define R503_PID_COMMAND                0x01
#define R503_PID_DATA                   0x02            // image data, more follows
#define R503_PID_ACK                    0x07
#define R503_PID_END_DATA               0x08            // the last image data packet, data included
#define R503_HEAD_LEN                   (R503_ADDRESS_LEN + 3)  // what follows the start code up to the content
#define R503_HEAD_PID                   4
#define R503_HEAD_LEN_HIGH              5
#define R503_HEAD_LEN_LOW               6
#define R503_START_LEN                  2
#define R503_CHECKSUM_LEN               2
#define R503_CONTENT_MAX                256     // the largest data packet size ReadSysPara can report
#define R503_COMMAND_MAX                8       // room for the longest command content sent here (Search: 6)
#define R503_BYTE_BITS                  8
#define R503_BYTE_MASK                  0xFF
#define R503_WORD_BITS                  16
#define R503_WORD_MASK                  0xFFFF

// Instruction codes.
#define R503_CMD_GEN_IMG                0x01
#define R503_CMD_IMG2TZ                 0x02
#define R503_CMD_SEARCH                 0x04
#define R503_CMD_REG_MODEL              0x05
#define R503_CMD_STORE                  0x06
#define R503_CMD_UP_IMAGE               0x0A
#define R503_CMD_DELET_CHAR             0x0C
#define R503_CMD_READ_SYS_PARA          0x0F
#define R503_CMD_VFY_PWD                0x13
#define R503_CMD_AURA_LED               0x35

// Confirmation codes r503_code_str() names, besides the three in r503.h.
#define R503_CODE_PACKET_ERROR          0x01
#define R503_CODE_CAPTURE_FAILED        0x03
#define R503_CODE_IMAGE_MESSY           0x06
#define R503_CODE_FEW_FEATURES          0x07
#define R503_CODE_NO_MATCH              0x08
#define R503_CODE_COMBINE_FAILED        0x0A
#define R503_CODE_BAD_PAGE              0x0B
#define R503_CODE_UPLOAD_FAILED         0x0F
#define R503_CODE_DELETE_FAILED         0x10
#define R503_CODE_WRONG_PASSWORD        0x13
#define R503_CODE_NO_VALID_IMAGE        0x15
#define R503_CODE_FLASH_ERROR           0x18
#define R503_CODE_PASSWORD_REQUIRED     0x21

// Acknowledge layouts: content offsets after the confirmation code at 0.
#define R503_ACK_CODE_LEN               1
#define R503_SYS_PARA_LEN               (R503_ACK_CODE_LEN + 16)
#define R503_SYS_PARA_CAPACITY          (R503_ACK_CODE_LEN + 4)     // word 2 of the basic parameters
#define R503_SEARCH_ACK_LEN             (R503_ACK_CODE_LEN + 4)
#define R503_SEARCH_PAGE                1
#define R503_SEARCH_SCORE               3

// Most commands answer within milliseconds; the three that write the template flash take longer,
// and an image upload is ~20 KB on the wire (~3.5 s at 57600 baud).
#define R503_TIMEOUT_MS                 1000
#define R503_TIMEOUT_FLASH_MS           3000
#define R503_TIMEOUT_IMAGE_MS           6000

// Room for the longest reason below; the enrollment status holds it after "RegModel: ".
#define R503_ERROR_MAX                  56

static bool r503_ready;

// Only the sensor task calls in here, so none of these is guarded.
static uint8_t r503_rx[R503_CONTENT_MAX + R503_CHECKSUM_LEN];   // content and checksum of the packet being received
static uint8_t r503_cmd;                                        // instruction code of the command in progress
static char    r503_error[R503_ERROR_MAX];                      // why it failed; empty while it has not

static void r503_error_set(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void r503_error_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));


esp_err_t r503_init(void)
{
    const uart_config_t config = {
        .baud_rate = R503_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(R503_UART_NUM, R503_UART_RX_BUFFER_SIZE, R503_UART_TX_BUFFER_SIZE,
                                        0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(R503_UART_NUM, &config);
    if (err == ESP_OK) {
        err = uart_set_pin(R503_UART_NUM, R503_UART_TX_PIN, R503_UART_RX_PIN, UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART%d setup failed: %s", R503_UART_NUM, esp_err_to_name(err));
        uart_driver_delete(R503_UART_NUM);
        return err;
    }

    r503_ready = true;
    ESP_LOGI(TAG, "UART%d: TX GPIO%d, RX GPIO%d, %d 8N1", R503_UART_NUM, R503_UART_TX_PIN, R503_UART_RX_PIN,
             R503_UART_BAUD_RATE);
    return ESP_OK;
}


static uint8_t r503_high(uint16_t value)
{
    return (uint8_t)(value >> R503_BYTE_BITS);
}

static uint8_t r503_low(uint16_t value)
{
    return (uint8_t)(value & R503_BYTE_MASK);
}

static uint16_t r503_get_u16(const uint8_t *in)
{
    return (uint16_t)((in[0] << R503_BYTE_BITS) | in[1]);
}

// Ticks left until the deadline, 0 once it has passed.
static TickType_t r503_ticks_left(TickType_t deadline)
{
    int32_t left = (int32_t)(deadline - xTaskGetTickCount());
    return (left > 0) ? (TickType_t)left : 0;
}

// Records why the command in progress failed, for r503_last_error(). Not logged: a timeout repeats
// every few seconds while the sensor is offline, and the caller logs the failure anyway.
static void r503_error_set(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(r503_error, sizeof(r503_error), fmt, args);
    va_end(args);
}

// r503_error_set(), and a warning in the log: a malformed packet, which is not expected to repeat.
static void r503_error_warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(r503_error, sizeof(r503_error), fmt, args);
    va_end(args);
    ESP_LOGW(TAG, "Command 0x%02X: %s", r503_cmd, r503_error);
}

static esp_err_t r503_read_exact(uint8_t *buf, size_t len, TickType_t deadline)
{
    size_t got = 0;
    while (got < len) {
        TickType_t left = r503_ticks_left(deadline);
        if (left == 0) {
            return ESP_ERR_TIMEOUT;
        }
        int n = uart_read_bytes(R503_UART_NUM, buf + got, len - got, left);
        if (n < 0) {
            r503_error_set("UART read failed");
            return ESP_FAIL;
        }
        got += (size_t)n;
    }
    return ESP_OK;
}

static esp_err_t r503_send(const uint8_t *content, size_t content_len)
{
    uint8_t packet[R503_START_LEN + R503_HEAD_LEN + R503_COMMAND_MAX + R503_CHECKSUM_LEN];
    uint16_t length = (uint16_t)(content_len + R503_CHECKSUM_LEN);
    size_t pos = 0;

    if (content_len > R503_COMMAND_MAX) {
        r503_error_set("command of %u bytes too long", (unsigned)content_len);
        return ESP_ERR_INVALID_SIZE;
    }

    packet[pos++] = R503_START_HIGH;
    packet[pos++] = R503_START_LOW;
    for (int shift = (R503_ADDRESS_LEN - 1) * R503_BYTE_BITS; shift >= 0; shift -= R503_BYTE_BITS) {
        packet[pos++] = (uint8_t)((R503_ADDRESS >> shift) & R503_BYTE_MASK);
    }
    packet[pos++] = R503_PID_COMMAND;
    packet[pos++] = r503_high(length);
    packet[pos++] = r503_low(length);

    uint16_t sum = (uint16_t)(R503_PID_COMMAND + r503_high(length) + r503_low(length));
    for (size_t i = 0; i < content_len; i++) {
        packet[pos++] = content[i];
        sum = (uint16_t)(sum + content[i]);
    }
    packet[pos++] = r503_high(sum);
    packet[pos++] = r503_low(sum);

    int written = uart_write_bytes(R503_UART_NUM, packet, pos);
    if (written != (int)pos) {
        r503_error_set("UART write failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Receives one packet into r503_rx. Everything before the start code is skipped: the module
// sends a lone 0x55 (and may send 0x00) after power-up, and a flush does not catch bytes that
// were still on the wire. What follows the start code is validated field by field.
static esp_err_t r503_receive(uint8_t *pid, size_t *content_len, TickType_t deadline)
{
    uint8_t byte = 0;
    bool high_seen = false;
    unsigned skipped = 0;                                       // bytes read without a start code among them
    for (;;) {
        esp_err_t err = r503_read_exact(&byte, 1, deadline);
        if (err == ESP_ERR_TIMEOUT) {
            if (skipped == 0) {
                r503_error_set("no answer");
            } else {
                r503_error_set("no start code; stray bytes: %u", skipped);
            }
        }
        if (err != ESP_OK) {
            return err;
        }
        if (high_seen && (byte == R503_START_LOW)) {
            break;
        }
        high_seen = (byte == R503_START_HIGH);
        skipped++;
    }

    uint8_t head[R503_HEAD_LEN];
    esp_err_t err = r503_read_exact(head, sizeof(head), deadline);
    if (err == ESP_ERR_TIMEOUT) {
        r503_error_set("packet header cut short");
    }
    if (err != ESP_OK) {
        return err;
    }

    uint32_t address = 0;
    for (int i = 0; i < R503_ADDRESS_LEN; i++) {
        address = (address << R503_BYTE_BITS) | head[i];
    }
    if (address != R503_ADDRESS) {
        r503_error_warn("packet from address 0x%08" PRIX32, address);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *pid = head[R503_HEAD_PID];
    if ((*pid != R503_PID_ACK) && (*pid != R503_PID_DATA) && (*pid != R503_PID_END_DATA)) {
        r503_error_warn("unexpected packet identifier 0x%02X", *pid);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint16_t length = r503_get_u16(&head[R503_HEAD_LEN_HIGH]);
    if ((length < R503_CHECKSUM_LEN) || (length > sizeof(r503_rx))) {
        r503_error_warn("packet length %u out of range", length);
        return ESP_ERR_INVALID_RESPONSE;
    }

    err = r503_read_exact(r503_rx, length, deadline);
    if (err == ESP_ERR_TIMEOUT) {
        r503_error_set("packet of length %u cut short", length);
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t content = length - R503_CHECKSUM_LEN;
    uint16_t sum = (uint16_t)(*pid + head[R503_HEAD_LEN_HIGH] + head[R503_HEAD_LEN_LOW]);
    for (size_t i = 0; i < content; i++) {
        sum = (uint16_t)(sum + r503_rx[i]);
    }
    uint16_t received = r503_get_u16(&r503_rx[content]);
    if (sum != received) {
        r503_error_warn("checksum mismatch: received 0x%04X, computed 0x%04X", received, sum);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *content_len = content;
    return ESP_OK;
}

// One command and its acknowledge. On ESP_OK *code is the confirmation code and, when that is
// R503_CODE_OK, at least ack_len bytes of the acknowledge are in r503_rx.
static esp_err_t r503_command(const uint8_t *content, size_t content_len, size_t ack_len, TickType_t deadline,
                              uint8_t *code)
{
    r503_cmd = content[0];
    r503_error[0] = '\0';
    if (!r503_ready) {
        r503_error_set("sensor UART not installed");
        return ESP_ERR_INVALID_STATE;
    }

    // Whatever arrived since the last exchange - a late answer, power-up noise - is not an
    // answer to this command.
    uart_flush_input(R503_UART_NUM);

    esp_err_t err = r503_send(content, content_len);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t pid = 0;
    size_t len = 0;
    err = r503_receive(&pid, &len, deadline);
    if (err != ESP_OK) {
        return err;
    }
    if ((pid != R503_PID_ACK) || (len < R503_ACK_CODE_LEN)) {
        r503_error_warn("not an acknowledge: identifier 0x%02X, %u bytes", pid, (unsigned)len);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *code = r503_rx[0];
    if ((*code == R503_CODE_OK) && (len < ack_len)) {
        r503_error_warn("short acknowledge: %u of %u bytes", (unsigned)len, (unsigned)ack_len);
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static TickType_t r503_deadline(uint32_t timeout_ms)
{
    return xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
}


esp_err_t r503_verify_password(uint32_t password, uint8_t *code)
{
    uint16_t high_word = (uint16_t)(password >> R503_WORD_BITS);
    uint16_t low_word = (uint16_t)(password & R503_WORD_MASK);
    uint8_t cmd[] = {R503_CMD_VFY_PWD, r503_high(high_word), r503_low(high_word), r503_high(low_word),
                     r503_low(low_word)};
    return r503_command(cmd, sizeof(cmd), R503_ACK_CODE_LEN, r503_deadline(R503_TIMEOUT_MS), code);
}

esp_err_t r503_read_capacity(uint16_t *capacity, uint8_t *code)
{
    uint8_t cmd[] = {R503_CMD_READ_SYS_PARA};
    esp_err_t err = r503_command(cmd, sizeof(cmd), R503_SYS_PARA_LEN, r503_deadline(R503_TIMEOUT_MS), code);
    if ((err == ESP_OK) && (*code == R503_CODE_OK)) {
        *capacity = r503_get_u16(&r503_rx[R503_SYS_PARA_CAPACITY]);
    }
    return err;
}

esp_err_t r503_gen_img(uint8_t *code)
{
    uint8_t cmd[] = {R503_CMD_GEN_IMG};
    return r503_command(cmd, sizeof(cmd), R503_ACK_CODE_LEN, r503_deadline(R503_TIMEOUT_MS), code);
}

esp_err_t r503_img2tz(uint8_t buffer_id, uint8_t *code)
{
    uint8_t cmd[] = {R503_CMD_IMG2TZ, buffer_id};
    return r503_command(cmd, sizeof(cmd), R503_ACK_CODE_LEN, r503_deadline(R503_TIMEOUT_MS), code);
}

esp_err_t r503_search(uint8_t buffer_id, uint16_t start, uint16_t count, uint16_t *page, uint16_t *score,
                      uint8_t *code)
{
    uint8_t cmd[] = {R503_CMD_SEARCH, buffer_id, r503_high(start), r503_low(start), r503_high(count), r503_low(count)};
    esp_err_t err = r503_command(cmd, sizeof(cmd), R503_SEARCH_ACK_LEN, r503_deadline(R503_TIMEOUT_FLASH_MS), code);
    if ((err == ESP_OK) && (*code == R503_CODE_OK)) {
        *page = r503_get_u16(&r503_rx[R503_SEARCH_PAGE]);
        *score = r503_get_u16(&r503_rx[R503_SEARCH_SCORE]);
    }
    return err;
}

esp_err_t r503_reg_model(uint8_t *code)
{
    uint8_t cmd[] = {R503_CMD_REG_MODEL};
    return r503_command(cmd, sizeof(cmd), R503_ACK_CODE_LEN, r503_deadline(R503_TIMEOUT_FLASH_MS), code);
}

esp_err_t r503_store(uint8_t buffer_id, uint16_t page, uint8_t *code)
{
    uint8_t cmd[] = {R503_CMD_STORE, buffer_id, r503_high(page), r503_low(page)};
    return r503_command(cmd, sizeof(cmd), R503_ACK_CODE_LEN, r503_deadline(R503_TIMEOUT_FLASH_MS), code);
}

esp_err_t r503_delete_char(uint16_t start, uint16_t count, uint8_t *code)
{
    uint8_t cmd[] = {R503_CMD_DELET_CHAR, r503_high(start), r503_low(start), r503_high(count), r503_low(count)};
    return r503_command(cmd, sizeof(cmd), R503_ACK_CODE_LEN, r503_deadline(R503_TIMEOUT_FLASH_MS), code);
}

esp_err_t r503_aura_led(uint8_t control, uint8_t speed, uint8_t colour, uint8_t count, uint8_t *code)
{
    uint8_t cmd[] = {R503_CMD_AURA_LED, control, speed, colour, count};
    return r503_command(cmd, sizeof(cmd), R503_ACK_CODE_LEN, r503_deadline(R503_TIMEOUT_MS), code);
}

// The acknowledge first, then data packets of whatever size the module is configured for (128
// bytes from the factory), the last one marked R503_PID_END_DATA and carrying data as well. One
// deadline covers the whole transfer.
esp_err_t r503_up_image(uint8_t *raw, size_t raw_size, size_t *raw_len, uint8_t *code)
{
    uint8_t cmd[] = {R503_CMD_UP_IMAGE};
    TickType_t deadline = r503_deadline(R503_TIMEOUT_IMAGE_MS);

    *raw_len = 0;
    esp_err_t err = r503_command(cmd, sizeof(cmd), R503_ACK_CODE_LEN, deadline, code);
    if ((err != ESP_OK) || (*code != R503_CODE_OK)) {
        return err;
    }

    for (;;) {
        uint8_t pid = 0;
        size_t len = 0;
        err = r503_receive(&pid, &len, deadline);
        if (err != ESP_OK) {
            return err;
        }
        if ((pid != R503_PID_DATA) && (pid != R503_PID_END_DATA)) {
            r503_error_warn("packet identifier 0x%02X among the image data", pid);
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (len > (raw_size - *raw_len)) {
            r503_error_warn("more than %u bytes of image data", (unsigned)raw_size);
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(raw + *raw_len, r503_rx, len);
        *raw_len += len;
        if (pid == R503_PID_END_DATA) {
            return ESP_OK;
        }
    }
}

const char *r503_code_str(uint8_t code)
{
    switch (code) {
    case R503_CODE_OK:                  return "ok";
    case R503_CODE_PACKET_ERROR:        return "packet receive error";
    case R503_CODE_NO_FINGER:           return "no finger";
    case R503_CODE_CAPTURE_FAILED:      return "image capture failed";
    case R503_CODE_IMAGE_MESSY:         return "image too messy";
    case R503_CODE_FEW_FEATURES:        return "too few features";
    case R503_CODE_NO_MATCH:            return "no match";
    case R503_CODE_NOT_FOUND:           return "not found";
    case R503_CODE_COMBINE_FAILED:      return "the takes do not match";
    case R503_CODE_BAD_PAGE:            return "slot out of range";
    case R503_CODE_UPLOAD_FAILED:       return "image upload failed";
    case R503_CODE_DELETE_FAILED:       return "template delete failed";
    case R503_CODE_WRONG_PASSWORD:      return "wrong password";
    case R503_CODE_NO_VALID_IMAGE:      return "no valid image";
    case R503_CODE_FLASH_ERROR:         return "flash write error";
    case R503_CODE_PASSWORD_REQUIRED:   return "password not verified";
    default:                            return "sensor error";
    }
}

const char *r503_last_error(void)
{
    return r503_error;
}

#endif // FP_ACS_ENABLED
