#include "cache_multimaster.h"
#include <string.h>

/* Call tracking variables exposed for test assertions */
int mock_cache_multimaster_init_called = 0;
int mock_cache_multimaster_enable_called = 0;
int mock_cache_multimaster_disable_called = 0;
/* Reflects enable/disable so production guards (is_enabled) behave realistically */
bool mock_cache_multimaster_enabled = false;

esp_err_t cache_multimaster_init(void)
{
    mock_cache_multimaster_init_called++;
    return ESP_OK;
}

void cache_multimaster_enable(void)
{
    mock_cache_multimaster_enable_called++;
    mock_cache_multimaster_enabled = true;
}

void cache_multimaster_disable(void)
{
    mock_cache_multimaster_disable_called++;
    mock_cache_multimaster_enabled = false;
}

bool cache_multimaster_is_enabled(void)
{
    return mock_cache_multimaster_enabled;
}

void cache_multimaster_clear(void)
{
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

esp_err_t cache_multimaster_register_handlers(httpd_handle_t server)
{
    (void)server;
    return ESP_OK;
}

cache_lookup_result_t cache_multimaster_lookup(uint8_t slave_id, uint8_t function_code,
                                                uint16_t address, uint16_t *value_out,
                                                uint16_t value_timeout_s)
{
    (void)slave_id;
    (void)function_code;
    (void)address;
    (void)value_out;
    (void)value_timeout_s;
    return CACHE_LOOKUP_NOT_FOUND;
}

void mock_cache_multimaster_reset(void)
{
    mock_cache_multimaster_init_called = 0;
    mock_cache_multimaster_enable_called = 0;
    mock_cache_multimaster_disable_called = 0;
    mock_cache_multimaster_enabled = false;
}
