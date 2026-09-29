#pragma once

/*
 * Grow R503 fingerprint sensor: the packet protocol on a plain 3.3 V UART, and only the
 * commands the access control uses. Every call is blocking and must come from the one task
 * that owns the sensor (main/fp_acs/fp_acs.c); nothing here is thread-safe.
 *
 * Each command answers on two separate levels, and they are kept apart on purpose:
 *   - the return value is the TRANSPORT: did a well-formed acknowledge arrive at all;
 *   - *code is the sensor's confirmation code inside that acknowledge (R503_CODE_*), valid
 *     only when the return value is ESP_OK.
 *
 * Transport results:
 *   ESP_OK                   an acknowledge passed every check; *code holds its confirmation code
 *   ESP_ERR_TIMEOUT          no complete packet within the command's timeout
 *   ESP_ERR_INVALID_RESPONSE a packet with a wrong address, identifier, length or checksum
 *   ESP_ERR_INVALID_SIZE     a well-formed acknowledge too short for what the command returns,
 *                            or image data that does not fit the caller's buffer
 *   ESP_ERR_INVALID_STATE    r503_init() has not succeeded
 *   ESP_FAIL                 the UART refused the write
 */

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Confirmation codes this firmware acts on; r503_code_str() names the others it may see.
#define R503_CODE_OK                0x00
#define R503_CODE_NO_FINGER         0x02
#define R503_CODE_NOT_FOUND         0x09

// The factory handshake password.
#define R503_DEFAULT_PASSWORD       0x00000000u

// AuraLedConfig control codes and colours.
#define R503_LED_BREATHING          0x01
#define R503_LED_FLASHING           0x02
#define R503_LED_ON                 0x03
#define R503_LED_OFF                0x04
#define R503_LED_RED                0x01
#define R503_LED_BLUE               0x02
#define R503_LED_PURPLE             0x03
#define R503_LED_GREEN              0x04

// UpImage: 192 x 192 pixels at 4 bits each, two pixels per byte, high nibble first (left).
#define R503_IMAGE_WIDTH            192
#define R503_IMAGE_HEIGHT           192
#define R503_IMAGE_RAW_SIZE         ((R503_IMAGE_WIDTH * R503_IMAGE_HEIGHT) / 2)

// Installs the UART2 driver on the WBE2 slot pins (57600 8N1, no RTS/CTS). Call once.
esp_err_t r503_init(void);

esp_err_t r503_verify_password(uint32_t password, uint8_t *code);
// ReadSysPara, reduced to the one parameter used: the template library size (word 2).
esp_err_t r503_read_capacity(uint16_t *capacity, uint8_t *code);
esp_err_t r503_gen_img(uint8_t *code);
esp_err_t r503_img2tz(uint8_t buffer_id, uint8_t *code);
// On R503_CODE_OK *page and *score describe the match; R503_CODE_NOT_FOUND means no match.
esp_err_t r503_search(uint8_t buffer_id, uint16_t start, uint16_t count, uint16_t *page, uint16_t *score,
                      uint8_t *code);
esp_err_t r503_reg_model(uint8_t *code);
esp_err_t r503_store(uint8_t buffer_id, uint16_t page, uint8_t *code);
esp_err_t r503_delete_char(uint16_t start, uint16_t count, uint8_t *code);
// count 0 repeats a breathing or flashing effect until the next command.
esp_err_t r503_aura_led(uint8_t control, uint8_t speed, uint8_t colour, uint8_t count, uint8_t *code);
// The raw 4-bit image of ImageBuffer, as the sensor sends it, into raw[0..raw_size); *raw_len is
// how much arrived. Takes about 3.5 s at 57600 baud.
esp_err_t r503_up_image(uint8_t *raw, size_t raw_size, size_t *raw_len, uint8_t *code);

// A short English description of a confirmation code, for logs and the enrollment status.
const char *r503_code_str(uint8_t code);
