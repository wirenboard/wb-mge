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

/* Forward-declare httpd_req_t so the test-only prototypes below compile without
 * pulling in the full esp_http_server mock header (which is not on every test
 * suite's include path). The complete definition is provided by esp_http_server.h
 * which sniffer.c includes directly in __unittest_env__ builds. */
struct httpd_req;

/* Test-only accessors and forward declarations — not available in production builds */
void sniffer_ws_dispatch(sniff_packet_t *pkt);
esp_err_t sniffer_ws_handler(struct httpd_req *req);
int sniffer_test_get_ws_client_fd(void);
#endif

/**
 * @brief Reasons that keep the sniffer pipeline running for a port.
 *
 * The sniffer is an additive overlay: it runs whenever at least one reason bit
 * is set. Each reason is enabled/disabled independently by its owner.
 */
typedef enum {
    SNIFF_REASON_DISPLAY = 1u << 0,  // a WS client wants live display
    SNIFF_REASON_CACHE   = 1u << 1,  // the cache overlay wants data
} sniff_reason_t;

esp_err_t sniffer_init(void);
void sniffer_attach(unsigned port_index, serial_desc_t *serial_desc);
void sniffer_detach(unsigned port_index);
void sniffer_enable(unsigned port_index, sniff_reason_t reason);
void sniffer_disable(unsigned port_index, sniff_reason_t reason);

esp_err_t sniffer_register_handlers(httpd_handle_t server);
