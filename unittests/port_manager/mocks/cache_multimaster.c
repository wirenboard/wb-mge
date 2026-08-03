#include "cache_multimaster.h"
#include <string.h>

/* Call tracking variables exposed for test assertions */
int mock_cache_multimaster_init_called = 0;
int mock_cache_multimaster_enable_called = 0;
int mock_cache_multimaster_disable_called = 0;
int mock_cache_multimaster_clear_called = 0;
/* Reflects enable/disable so production guards (is_enabled) behave realistically */
bool mock_cache_multimaster_enabled = false;
/* Set by a test to make the mutex allocation fail, the only way this init can fail
 * on the device (out of memory). Still counts the call. */
bool mock_cache_multimaster_init_should_fail = false;
/* Set by a test to make the 32 KB pool allocation fail (out of contiguous DRAM).
 * Mirrors the real function: the call is counted, ESP_ERR_NO_MEM is returned and the
 * cache stays OFF, so is_enabled() keeps reporting false. */
bool mock_cache_multimaster_enable_should_fail = false;

esp_err_t cache_multimaster_init(void)
{
    mock_cache_multimaster_init_called++;
    if (mock_cache_multimaster_init_should_fail) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t cache_multimaster_enable(void)
{
    mock_cache_multimaster_enable_called++;
    if (mock_cache_multimaster_enable_should_fail) {
        return ESP_ERR_NO_MEM;
    }
    mock_cache_multimaster_enabled = true;
    return ESP_OK;
}

void cache_multimaster_disable(void)
{
    mock_cache_multimaster_disable_called++;
    /* Order and nesting copied from the real function: it clears s_cache_enabled first
     * so nothing new enters the pool, then calls cache_multimaster_clear() to wipe it,
     * and only then frees. The nested call matters to the mock's users, not to
     * port_manager: mock_cache_multimaster_clear_called is what a test asserts a move's
     * wipe on, and a mock whose disable() skipped clear() would answer 0 for a sequence
     * that does clear the pool on the device. */
    mock_cache_multimaster_enabled = false;
    cache_multimaster_clear();
}

bool cache_multimaster_is_enabled(void)
{
    return mock_cache_multimaster_enabled;
}

/* Counted, and deliberately does NOT touch mock_cache_multimaster_enabled: the real
 * clear() wipes the pool contents in place without freeing the allocation or turning
 * the cache off. That is the property port_manager_set_cache() relies on to drop a
 * move's stale values without cycling the pool. */
void cache_multimaster_clear(void)
{
    mock_cache_multimaster_clear_called++;
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
    mock_cache_multimaster_clear_called = 0;
    mock_cache_multimaster_enabled = false;
    mock_cache_multimaster_init_should_fail = false;
    mock_cache_multimaster_enable_should_fail = false;
}
