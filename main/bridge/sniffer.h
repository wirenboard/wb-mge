#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "serial.h"

/* Maximum length of a single sniffed packet (bytes). */
#define SNIFFER_MAX_PACKET_LEN 256
/* Size of the JSON formatting buffer used by the sniffer WS task. */
#define SNIFFER_JSON_BUF_SIZE  1200

#ifndef __unittest_env__
#include "esp_http_server.h"
#else
typedef void* httpd_handle_t;

/* Internal packet structure; exposed here only for unit test builds so that
 * format_timeout_json / format_packet_json can be tested without including
 * the full FreeRTOS / ESP-IDF headers. */
typedef struct {
    uint8_t  port;
    uint64_t timestamp_us;
    bool     is_master;
    bool     crc_valid;
    bool     is_timeout;
    uint8_t  slave_id;
    uint8_t  function;
    uint8_t  data[SNIFFER_MAX_PACKET_LEN];
    uint16_t data_len;
} sniff_packet_t;

/* Direction classification result; exported for unit tests so that tests can
 * use the same enum values as the production classify_direction() function
 * without duplicating the definition and risking value drift. */
typedef enum {
    DIRECTION_REQUEST  = 0,
    DIRECTION_RESPONSE = 1,
    DIRECTION_UNKNOWN  = 2,
} pdu_direction_t;
#endif

esp_err_t sniffer_init(void);
void sniffer_attach(unsigned port_index, serial_desc_t *serial_desc);
void sniffer_detach(unsigned port_index);
void sniffer_enable(unsigned port_index);
void sniffer_disable(unsigned port_index);
void sniffer_set_cache_active(bool active);
esp_err_t sniffer_register_handlers(httpd_handle_t server);
