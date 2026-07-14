#include "packet_queue.h"
#include <string.h>
#include <stdlib.h>

#define MOCK_PQ_MAX_PACKETS 64
#define MOCK_PQ_MAX_LEN     300

typedef struct {
    uint8_t data[MOCK_PQ_MAX_LEN];
    size_t  len;
    int     sock;
} mock_pq_entry_t;

mock_pq_entry_t mock_pq_packets[MOCK_PQ_MAX_PACKETS];
int             mock_pq_push_count = 0;
esp_err_t       mock_pq_push_result = 0;  /* 0 = ESP_OK */

packet_queue_handle packet_queue_create(const size_t max_len)
{
    (void)max_len;
    return (packet_queue_handle)1;  /* non-NULL sentinel */
}

void packet_queue_delete(const packet_queue_handle handle) { (void)handle; }

size_t packet_queue_count(const packet_queue_handle handle) { (void)handle; return 0; }

void packet_queue_clear(const packet_queue_handle handle) { (void)handle; }

esp_err_t packet_queue_push_with_client(const packet_queue_handle handle, const uint8_t *data, const size_t len, int client_sock)
{
    (void)handle;
    /* Always count the call regardless of the configured return value. */
    mock_pq_push_count++;
    if (mock_pq_push_result != 0) { return mock_pq_push_result; }
    if ((mock_pq_push_count - 1) < MOCK_PQ_MAX_PACKETS) {
        mock_pq_entry_t *e = &mock_pq_packets[mock_pq_push_count - 1];
        size_t copy_len = len < MOCK_PQ_MAX_LEN ? len : MOCK_PQ_MAX_LEN;
        memcpy(e->data, data, copy_len);
        e->len  = len;
        e->sock = client_sock;
    }
    return 0; /* ESP_OK */
}

size_t packet_queue_pop_with_client(const packet_queue_handle handle, uint8_t **buf_ptr, const TickType_t timeout_ticks, int *client_sock)
{
    (void)handle; (void)timeout_ticks;
    *buf_ptr    = NULL;
    *client_sock = -1;
    return 0;
}

void mock_packet_queue_reset(void)
{
    memset(mock_pq_packets, 0, sizeof(mock_pq_packets));
    mock_pq_push_count  = 0;
    mock_pq_push_result = 0;
}
