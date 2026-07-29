#ifdef __unittest_env__
    #define malloc test_malloc
    #define free test_free
#endif

#include "fast_modbus.h"
#include "modbus_helpers.h"
#include "tcp_server.h"
#include "esp_log.h"

#include <string.h>

#define MODBUS_MGE_DETECT_LENGTH                    17              // Fast Modbus request length
#define MODBUS_MGE_DETECT_UID                       0x00            // Unit ID for MGE detection (0)
#define MODBUS_MGE_DETECT_FCODE                     0x47            // Modbus function code for MGE detection (71)

static const char *FAST_MODBUS_REQUEST_STR = "WB-FAST-MODBUS?";
static const char *FAST_MODBUS_RESPONSE_STR = "WB-FAST-MODBUS-OK";

static const char *TAG = "fast_modbus";

enum fast_modbus_probe_result fast_modbus_send_probe_response(uint8_t port, tcp_desc_t *tcp_desc, int client_sock,
                                                              uint32_t conn_generation, uint8_t *tcp_req_buf)
{
    mb_tcp_header_t *tcp_req_header = (mb_tcp_header_t *)tcp_req_buf;
    if ((tcp_req_header->function == MODBUS_MGE_DETECT_FCODE) &&
        (tcp_req_header->unit_id == MODBUS_MGE_DETECT_UID) &&
        (modbus_swap16(tcp_req_header->length) == MODBUS_MGE_DETECT_LENGTH) &&
        (memcmp(&tcp_req_buf[sizeof(mb_tcp_header_t)], FAST_MODBUS_REQUEST_STR, strlen(FAST_MODBUS_REQUEST_STR)) == 0))
    {
        ESP_LOGI(TAG, "Port[%d]: Fast Modbus support probe received", port);
    } else {
        return FAST_MODBUS_NOT_PROBE;
    }

    const size_t tcp_resp_data_len = strlen(FAST_MODBUS_RESPONSE_STR);
    const size_t tcp_resp_len = sizeof(mb_tcp_header_t) + tcp_resp_data_len;

    uint8_t *tcp_resp_buf = malloc(tcp_resp_len);
    if (!tcp_resp_buf) {
        ESP_LOGE(TAG, "Port[%d]: Failed to create Fast Modbus support TCP response buffer", port);
        return FAST_MODBUS_PROBE_MALLOC_FAIL;
    }

    mb_tcp_header_t *tcp_resp_header = (mb_tcp_header_t *)tcp_resp_buf;

    tcp_resp_header->transaction_id = tcp_req_header->transaction_id;
    tcp_resp_header->protocol_id = tcp_req_header->protocol_id;
    tcp_resp_header->unit_id = tcp_req_header->unit_id;
    tcp_resp_header->function = tcp_req_header->function;
    tcp_resp_header->length = modbus_swap16(
        sizeof(tcp_resp_header->unit_id) + sizeof(tcp_resp_header->function) + tcp_resp_data_len
    );

    memcpy(&tcp_resp_buf[sizeof(mb_tcp_header_t)], FAST_MODBUS_RESPONSE_STR, tcp_resp_data_len);

    ESP_LOGD(TAG, "Port[%d]: Sending Fast Modbus support TCP response", port);
    /* Addressed by the (socket, generation) pair the request carried, not by the bare fd:
     * this runs in modbus_tcp_server_task(), so the connection's receiver task may be
     * closing that socket concurrently and lwIP may already have handed the fd number to a
     * different peer. tcp_server_send_to_captured_client() validates the pair and sends
     * under the descriptor's connection lock, so a stale probe response is dropped instead
     * of being written into a stranger's socket. */
    esp_err_t send_result = tcp_server_send_to_captured_client(tcp_desc, client_sock, conn_generation,
                                                               tcp_resp_buf, tcp_resp_len);

    free(tcp_resp_buf);

    if (send_result != ESP_OK) {
        ESP_LOGE(TAG, "Port[%d]: Failed to send Fast Modbus support TCP response", port);
        return FAST_MODBUS_PROBE_SEND_FAIL;
    }

    return FAST_MODBUS_PROBE_SUCCESS;
}

size_t fast_modbus_truncate_ff(uint8_t **data, size_t len)
{
    while (len && (**data == 0xFF)) {
        (*data)++;
        len--;
    }
    return len;
}
