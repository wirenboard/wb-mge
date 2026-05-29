/* Recording/controllable mock of cache_multimaster for sniffer unit tests.
 *
 * Enablement is controlled by mock_cache_multimaster_enabled (default false,
 * preserving the original no-op stub behavior for existing tests). Calls to
 * cache_multimaster_on_request / cache_multimaster_on_response are recorded so
 * tests can assert routing and argument decoding. */

#include <stdint.h>
#include <stdbool.h>

/* ---- Controllable state ---- */
bool mock_cache_multimaster_enabled = false;

/* ---- Recording state: on_request ---- */
int     mock_cache_multimaster_on_request_called    = 0;
uint8_t mock_cache_multimaster_on_request_last_port = 0;
uint8_t mock_cache_multimaster_on_request_last_slave_id = 0;
uint8_t mock_cache_multimaster_on_request_last_function = 0;
uint16_t mock_cache_multimaster_on_request_last_start_reg = 0;
uint16_t mock_cache_multimaster_on_request_last_count = 0;

/* ---- Recording state: on_response ---- */
int     mock_cache_multimaster_on_response_called    = 0;
uint8_t mock_cache_multimaster_on_response_last_port = 0;
uint8_t mock_cache_multimaster_on_response_last_slave_id = 0;
uint8_t mock_cache_multimaster_on_response_last_function = 0;
uint16_t mock_cache_multimaster_on_response_last_data_len = 0;
uint64_t mock_cache_multimaster_on_response_last_timestamp_us = 0;

void mock_cache_multimaster_reset(void)
{
    mock_cache_multimaster_enabled = false;

    mock_cache_multimaster_on_request_called    = 0;
    mock_cache_multimaster_on_request_last_port = 0;
    mock_cache_multimaster_on_request_last_slave_id = 0;
    mock_cache_multimaster_on_request_last_function = 0;
    mock_cache_multimaster_on_request_last_start_reg = 0;
    mock_cache_multimaster_on_request_last_count = 0;

    mock_cache_multimaster_on_response_called    = 0;
    mock_cache_multimaster_on_response_last_port = 0;
    mock_cache_multimaster_on_response_last_slave_id = 0;
    mock_cache_multimaster_on_response_last_function = 0;
    mock_cache_multimaster_on_response_last_data_len = 0;
    mock_cache_multimaster_on_response_last_timestamp_us = 0;
}

bool cache_multimaster_is_enabled(void)
{
    return mock_cache_multimaster_enabled;
}

void cache_multimaster_on_request(uint8_t port, uint8_t slave_id, uint8_t function,
                                   uint16_t start_reg, uint16_t count)
{
    mock_cache_multimaster_on_request_called++;
    mock_cache_multimaster_on_request_last_port = port;
    mock_cache_multimaster_on_request_last_slave_id = slave_id;
    mock_cache_multimaster_on_request_last_function = function;
    mock_cache_multimaster_on_request_last_start_reg = start_reg;
    mock_cache_multimaster_on_request_last_count = count;
}

void cache_multimaster_on_response(uint8_t port, uint8_t slave_id, uint8_t function,
                                    const uint8_t *data, uint16_t data_len,
                                    uint64_t timestamp_us)
{
    (void)data;
    mock_cache_multimaster_on_response_called++;
    mock_cache_multimaster_on_response_last_port = port;
    mock_cache_multimaster_on_response_last_slave_id = slave_id;
    mock_cache_multimaster_on_response_last_function = function;
    mock_cache_multimaster_on_response_last_data_len = data_len;
    mock_cache_multimaster_on_response_last_timestamp_us = timestamp_us;
}
