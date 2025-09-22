#include "fast_modbus.h"
#include "tcp_server.h"

#include <esp_log.h>
#include <string.h>

#define MODBUS_MGE_DETECT_FCODE             0x47            // Код функции Modbus для определения MGE (71)

static const char *TAG = "fast_modbus";
static const char *FAST_MODBUS_RESPONSE_STR = "WB-FAST-MODBUS-OK";

// Проверка запроса на поддержку Быстрого Modbus и отправка ответа, что устройство его поддерживает
enum fast_modbus_probe_result fast_modbus_send_probe_response(mb_tcp_task_ctx_t *ctx, const mb_tcp_header_t *tcp_req_header)
{
    if ((tcp_req_header->function == MODBUS_MGE_DETECT_FCODE) && (tcp_req_header->unit_id == 0)) {
        ESP_LOGD(TAG, "Port[%d]: Fast Modbus support probe received", ctx->index + 1);
    } else {
        return FAST_MODBUS_NOT_PROBE;
    }

    uint8_t *tcp_resp_buf = malloc(MODBUS_TCP_SEND_BUFFER_SIZE);
    if (!tcp_resp_buf) {
        ESP_LOGE(TAG, "Port[%d]: Failed to create Fast Modbus support TCP response buffer", ctx->index + 1);
        return FAST_MODBUS_PROBE_MALLOC_FAIL;
    }

    mb_tcp_header_t *tcp_resp_header = (mb_tcp_header_t *)tcp_resp_buf;
    tcp_resp_header->transaction_id = tcp_req_header->transaction_id;
    tcp_resp_header->protocol_id = tcp_req_header->protocol_id;
    tcp_resp_header->length = 1 /* unit_id */ + 1 /* function */ + strlen(FAST_MODBUS_RESPONSE_STR);
    tcp_resp_header->unit_id = tcp_req_header->unit_id;
    tcp_resp_header->function = tcp_req_header->function;

    memcpy(&tcp_resp_buf[sizeof(mb_tcp_header_t)], FAST_MODBUS_RESPONSE_STR, strlen(FAST_MODBUS_RESPONSE_STR));
    size_t tcp_resp_len = sizeof(mb_tcp_header_t) + strlen(FAST_MODBUS_RESPONSE_STR);

    ESP_LOGD(TAG, "Port[%d]: Sending Fast Modbus support TCP response", ctx->index + 1);
    esp_err_t send_result = tcp_server_send(ctx->tcp_desc, tcp_resp_buf, tcp_resp_len);
    if (send_result != ESP_OK) {
        ESP_LOGE(TAG, "Port[%d]: Failed to send Fast Modbus support TCP response", ctx->index + 1);
        free(tcp_resp_buf);
        return FAST_MODBUS_PROBE_SEND_FAIL;
    }

    free(tcp_resp_buf);
    return FAST_MODBUS_PROBE_SUCCESS;
}

// Удаляем байты 0xFF, предшествующие началу пакета в быстром Modbus
size_t fast_modbus_truncate_ff(uint8_t **data, size_t len)
{
    while (len && (**data == 0xFF)) {
        (*data)++;
        len--;
    }
    return len;
}
