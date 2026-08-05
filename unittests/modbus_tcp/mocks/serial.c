#include "serial.h"
#include <stddef.h>

/* Call counter so tests can assert that a self-addressed (unit 0xFF) request is
 * NOT forwarded to RS485 (serial_send must not be called for the self path). */
int mock_serial_send_count = 0;

/* Descriptor serial_init() hands back. NULL by default, i.e. "the port could not be
 * opened" — the tests that never call modbus_tcp_init_port() are unaffected by it.
 *
 * It is a settable POINTER, not a per-call allocation, because that is what the
 * descriptor-aliasing tests need: they point two consecutive inits at the SAME address to
 * reproduce the allocator handing a just-freed block straight back to the next caller. */
serial_desc_t *mock_serial_init_return = NULL;
int mock_serial_deinit_count = 0;

void mock_serial_reset(void)
{
    mock_serial_send_count   = 0;
    mock_serial_init_return  = NULL;
    mock_serial_deinit_count = 0;
}

serial_desc_t *serial_init(serial_config_t *config, serial_receive_handler_t handler)
{
    (void)config;
    if (mock_serial_init_return != NULL) {
        mock_serial_init_return->receive_handler = handler;
    }
    return mock_serial_init_return;
}

esp_err_t serial_send(serial_desc_t *desc, uint8_t *data, size_t len)
{
    (void)desc; (void)data; (void)len;
    mock_serial_send_count++;
    return 0;
}

esp_err_t serial_wait_tx_done(serial_desc_t *desc, TickType_t timeout_ticks)
{
    (void)desc; (void)timeout_ticks;
    return 0;
}

esp_err_t serial_deinit(serial_desc_t *desc)
{
    (void)desc;
    mock_serial_deinit_count++;
    return 0;
}

esp_err_t serial_set_rx_timeout(serial_desc_t *desc, uint8_t tout_symbols)
{
    (void)desc; (void)tout_symbols;
    return 0;
}
