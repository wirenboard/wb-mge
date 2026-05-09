/* Stub implementation of cache_multimaster for sniffer unit tests.
 * All functions are no-ops or return safe defaults. */

#include <stdint.h>
#include <stdbool.h>

bool cache_multimaster_is_enabled(void)
{
    return false;
}

void cache_multimaster_on_request(uint8_t port, uint8_t slave_id, uint8_t function,
                                   uint16_t start_reg, uint16_t count)
{
    (void)port;
    (void)slave_id;
    (void)function;
    (void)start_reg;
    (void)count;
}

void cache_multimaster_on_response(uint8_t port, uint8_t slave_id, uint8_t function,
                                    const uint8_t *data, uint16_t data_len,
                                    uint64_t timestamp_us)
{
    (void)port;
    (void)slave_id;
    (void)function;
    (void)data;
    (void)data_len;
    (void)timestamp_us;
}
