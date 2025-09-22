#include "fast_modbus.h"
#include "tcp_server.h"

#include <esp_log.h>
#include <string.h>

static const char *TAG = "fast_modbus";

// Отправка ответа, что устройство поддерживает Быстрый Modbus
esp_err_t fast_modbus_send_probe_response(mb_tcp_task_ctx_t* ctx, const mb_tcp_header_t* tcp_req_header)
{
    ESP_LOGD(TAG, "Port[%d]: Fast Modbus support probe received", ctx->index + 1);

    uint8_t* tcp_resp_buf = malloc(MODBUS_TCP_SEND_BUFFER_SIZE);
    if (!tcp_resp_buf) {
        ESP_LOGE(TAG, "Port[%d]: Failed to create Fast Modbus support TCP response buffer", ctx->index + 1);
        return ESP_FAIL;
    }

    mb_tcp_header_t* tcp_resp_header = (mb_tcp_header_t*)tcp_resp_buf;
    tcp_resp_header->transaction_id = tcp_req_header->transaction_id;
    tcp_resp_header->protocol_id = tcp_req_header->protocol_id;
    tcp_resp_header->length = 1 /* unit_id */ + 1 /* function */ + strlen("WB-FAST-MODBUS-OK");
    tcp_resp_header->unit_id = tcp_req_header->unit_id;
    tcp_resp_header->function = tcp_req_header->function;

    // Add data payload: 'WB-FAST-MODBUS-OK'
    const char* fast_modbus_data = "WB-FAST-MODBUS-OK";
    memcpy(&tcp_resp_buf[sizeof(mb_tcp_header_t)], fast_modbus_data, strlen(fast_modbus_data));
    size_t tcp_resp_len = sizeof(mb_tcp_header_t) + strlen(fast_modbus_data);

    ESP_LOGD(TAG, "Port[%d]: Sending Fast Modbus support TCP response", ctx->index + 1);
    tcp_server_send(ctx->tcp_desc, tcp_resp_buf, tcp_resp_len);

    free(tcp_resp_buf);
    return ESP_OK;
}

// Удаляем байты 0xFF, предшествующие началу пакета в быстром Modbus
size_t fast_modbus_truncate_ff(uint8_t** data, size_t len)
{
    while (len && (**data == 0xFF)) {
        (*data)++;
        len--;
    }
    return len;
}
