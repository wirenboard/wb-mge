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
#endif

/* One sniffed frame as it travels from the state machine to the WS task.
 * Declared here (not in sniffer.c) so production code and unit tests share a
 * single definition: the previous per-build duplicate could drift silently.
 * Depends only on stdint/stdbool and SNIFFER_MAX_PACKET_LEN above. */
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

/* Direction classification result for a standard Modbus RTU PDU.
 * Shared with the tests for the same reason as sniff_packet_t above: one
 * definition, no value drift between the production and test builds. */
typedef enum {
    DIRECTION_REQUEST  = 0, /* Packet is unambiguously a master request */
    DIRECTION_RESPONSE = 1, /* Packet is unambiguously a slave response  */
    DIRECTION_UNKNOWN  = 2, /* Cannot determine direction from length/FC alone */
} pdu_direction_t;

/* Per-port framing state of the request/response state machine. */
typedef enum {
    SNIFF_IDLE = 0,
    SNIFF_RES_WAIT,
} sniff_state_t;

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
