#include "packet_queue.h"
#include <string.h>
#include <stdlib.h>

#define MOCK_PQ_MAX_PACKETS 64
#define MOCK_PQ_MAX_LEN     300

typedef struct {
    uint8_t  data[MOCK_PQ_MAX_LEN];
    size_t   len;
    int      sock;
    uint32_t generation;
} mock_pq_entry_t;

mock_pq_entry_t mock_pq_packets[MOCK_PQ_MAX_PACKETS];
int             mock_pq_push_count = 0;
esp_err_t       mock_pq_push_result = 0;  /* 0 = ESP_OK */
/* Read cursor for pop: the mock is a real FIFO over mock_pq_packets, so a test can push a
 * request (as the receive handler would) and then pop it (as the server task does) and see
 * the (socket, generation) pair travel through the queue rather than be re-derived. */
int             mock_pq_pop_count = 0;

packet_queue_handle packet_queue_create(const size_t max_len)
{
    (void)max_len;
    return (packet_queue_handle)1;  /* non-NULL sentinel */
}

void packet_queue_delete(const packet_queue_handle handle) { (void)handle; }

size_t packet_queue_count(const packet_queue_handle handle)
{
    (void)handle;
    int pending = mock_pq_push_count - mock_pq_pop_count;
    return (pending > 0) ? (size_t)pending : 0;
}

void packet_queue_clear(const packet_queue_handle handle) { (void)handle; }

esp_err_t packet_queue_push_with_client(const packet_queue_handle handle, const uint8_t *data, const size_t len,
                                        int client_sock, uint32_t conn_generation)
{
    (void)handle;
    /* Always count the call regardless of the configured return value. */
    mock_pq_push_count++;
    if (mock_pq_push_result != 0) { return mock_pq_push_result; }
    if ((mock_pq_push_count - 1) < MOCK_PQ_MAX_PACKETS) {
        mock_pq_entry_t *e = &mock_pq_packets[mock_pq_push_count - 1];
        size_t copy_len = len < MOCK_PQ_MAX_LEN ? len : MOCK_PQ_MAX_LEN;
        memcpy(e->data, data, copy_len);
        e->len        = len;
        e->sock       = client_sock;
        e->generation = conn_generation;
    }
    return 0; /* ESP_OK */
}

size_t packet_queue_pop_with_client(const packet_queue_handle handle, uint8_t **buf_ptr, const TickType_t timeout_ticks,
                                    int *client_sock, uint32_t *conn_generation)
{
    (void)handle; (void)timeout_ticks;

    if ((mock_pq_pop_count >= mock_pq_push_count) || (mock_pq_pop_count >= MOCK_PQ_MAX_PACKETS)) {
        if (buf_ptr)         { *buf_ptr = NULL; }
        if (client_sock)     { *client_sock = -1; }
        if (conn_generation) { *conn_generation = 0; }
        return 0;
    }

    const mock_pq_entry_t *e = &mock_pq_packets[mock_pq_pop_count++];

    /* The production consumer free()s this buffer, so hand out a heap copy. */
    if (buf_ptr) {
        size_t copy_len = e->len < MOCK_PQ_MAX_LEN ? e->len : MOCK_PQ_MAX_LEN;
        uint8_t *copy = malloc(copy_len ? copy_len : 1);
        if (!copy) { return 0; }
        memcpy(copy, e->data, copy_len);
        *buf_ptr = copy;
    }
    if (client_sock)     { *client_sock = e->sock; }
    if (conn_generation) { *conn_generation = e->generation; }

    return e->len;
}

void mock_packet_queue_reset(void)
{
    memset(mock_pq_packets, 0, sizeof(mock_pq_packets));
    mock_pq_push_count  = 0;
    mock_pq_pop_count   = 0;
    mock_pq_push_result = 0;
}
