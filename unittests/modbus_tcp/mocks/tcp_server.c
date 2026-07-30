#include "tcp_server.h"
#include <string.h>
#include <stdbool.h>

/* Capture state for tcp_server_send (R2). Tests can read these after a send call. */
uint8_t   mock_tcp_send_buf[1024];
size_t    mock_tcp_send_len  = 0;
int       mock_tcp_send_sock = -1;
esp_err_t mock_tcp_send_result = 0;  /* override to simulate send failure */
bool      mock_tcp_send_overflow = false;  /* set when len > sizeof(mock_tcp_send_buf) */
/* What the module handed to tcp_server_send_to_captured_client(): how many times it was
 * called, and the connection generation of the last call. */
int       mock_tcp_send_captured_called = 0;
uint32_t  mock_tcp_send_generation = 0;

/* Descriptor tcp_server_init() publishes through its out-parameter. NULL by default, which
 * the mock reports as an init FAILURE — see tcp_server_init() below. A test that drives
 * modbus_tcp_init_port() must point this at a real tcp_desc_t, because the module writes
 * desc->close_handler right after the call.
 *
 * A settable POINTER rather than a per-call allocation, for the same reason as
 * mock_serial_init_return: the aliasing tests hand two consecutive inits the SAME address
 * to reproduce a just-freed descriptor being recycled by the next caller. */
tcp_desc_t *mock_tcp_server_init_desc   = NULL;
int         mock_tcp_server_deinit_count = 0;

void mock_tcp_server_reset(void)
{
    mock_tcp_send_len      = 0;
    mock_tcp_send_sock     = -1;
    mock_tcp_send_result   = 0;
    mock_tcp_send_overflow = false;
    mock_tcp_send_captured_called = 0;
    mock_tcp_send_generation = 0;
    memset(mock_tcp_send_buf, 0, sizeof(mock_tcp_send_buf));
    mock_tcp_server_init_desc    = NULL;
    mock_tcp_server_deinit_count = 0;
}

esp_err_t tcp_server_init(int port, tcp_receive_handler_t handler, tcp_desc_t **desc_out)
{
    (void)port;
    if (mock_tcp_server_init_desc == NULL) {
        /* Not armed. Report failure and write nothing, exactly as the real
         * tcp_server_init() does — it only touches the out-parameter on success. Returning
         * ESP_OK with *desc_out = NULL would be a contract the real one never offers, and
         * modbus_tcp_init_port() dereferences the descriptor immediately afterwards
         * ((*tcp_desc)->close_handler = ...), so a test that forgot to arm the mocks would
         * segfault inside production code instead of failing on its own terms. */
        return ESP_FAIL;
    }
    mock_tcp_server_init_desc->receive_handler = handler;
    if (desc_out != NULL) {
        *desc_out = mock_tcp_server_init_desc;
    }
    return ESP_OK;
}

esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
{
    (void)desc;
    mock_tcp_send_sock = client_sock;
    if (len <= sizeof(mock_tcp_send_buf)) {
        memcpy(mock_tcp_send_buf, data, len);
        mock_tcp_send_len = len;
    } else {
        mock_tcp_send_overflow = true;
        mock_tcp_send_len = len;  /* record size even though data is not copied */
    }
    return mock_tcp_send_result;
}

/* The real implementation validates (client_sock, generation) against the descriptor
 * under its connection lock and only then sends — that logic belongs to tcp_server and is
 * covered by its own suite. Here we only record what modbus_tcp addressed the response
 * to, which is what these tests are about. */
esp_err_t tcp_server_send_to_captured_client(tcp_desc_t *desc, int client_sock,
                                             uint32_t generation, uint8_t *data, size_t len)
{
    mock_tcp_send_captured_called++;
    mock_tcp_send_generation = generation;
    return tcp_server_send(desc, client_sock, data, len);
}

esp_err_t tcp_server_connected(tcp_desc_t *desc)
{
    (void)desc;
    return 0;
}

esp_err_t tcp_server_deinit(tcp_desc_t *desc)
{
    (void)desc;
    mock_tcp_server_deinit_count++;
    return 0;
}
