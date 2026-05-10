#include "cache_multimaster.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---- Mock state for cache_multimaster_lookup() --------------------------- */

/* Array-mode return values: if mock_lookup_arr_count > 0, returns values from
 * mock_lookup_results[] / mock_lookup_values_arr[] in order, then falls back
 * to single-value mode for any further calls. */
#define MOCK_LOOKUP_MAX_VALUES 16

cache_lookup_result_t mock_lookup_result        = CACHE_LOOKUP_FOUND;
uint16_t              mock_lookup_value         = 0;
int                   mock_lookup_call_count    = 0;
uint8_t               mock_lookup_last_slave_id = 0;
uint8_t               mock_lookup_last_fc       = 0;
uint16_t              mock_lookup_last_address  = 0;
uint16_t              mock_lookup_last_timeout  = 0;

cache_lookup_result_t mock_lookup_results[MOCK_LOOKUP_MAX_VALUES];
uint16_t              mock_lookup_values_arr[MOCK_LOOKUP_MAX_VALUES];
int                   mock_lookup_arr_count = 0; /* number of values in array mode */
int                   mock_lookup_arr_index = 0; /* current read index in array mode */

bool mock_cache_enabled = true;

/* ---- Mock implementations ------------------------------------------------ */

cache_lookup_result_t cache_multimaster_lookup(uint8_t slave_id, uint8_t function_code,
                                               uint16_t address, uint16_t *value_out,
                                               uint16_t value_timeout_s)
{
    mock_lookup_call_count++;
    mock_lookup_last_slave_id = slave_id;
    mock_lookup_last_fc       = function_code;
    mock_lookup_last_address  = address;
    mock_lookup_last_timeout  = value_timeout_s;

    cache_lookup_result_t result;
    uint16_t              value;

    if ((mock_lookup_arr_count > 0) && (mock_lookup_arr_index < mock_lookup_arr_count)) {
        /* Array mode: consume next entry */
        result = mock_lookup_results[mock_lookup_arr_index];
        value  = mock_lookup_values_arr[mock_lookup_arr_index];
        mock_lookup_arr_index++;
    } else {
        /* Single-value mode */
        result = mock_lookup_result;
        value  = mock_lookup_value;
    }

    if (value_out != NULL) {
        *value_out = value;
    }
    return result;
}

bool cache_multimaster_is_enabled(void)
{
    return mock_cache_enabled;
}

/* ---- Stubs for functions not exercised by builder tests ------------------ */

esp_err_t cache_multimaster_init(void)
{
    return 0; /* ESP_OK */
}

void cache_multimaster_enable(void) {}
void cache_multimaster_disable(void) {}
void cache_multimaster_clear(void) {}

void cache_multimaster_on_request(uint8_t port, uint8_t slave_id, uint8_t function,
                                   uint16_t start_reg, uint16_t count)
{
    (void)port; (void)slave_id; (void)function; (void)start_reg; (void)count;
}

void cache_multimaster_on_response(uint8_t port, uint8_t slave_id, uint8_t function,
                                    const uint8_t *data, uint16_t data_len,
                                    uint64_t timestamp_us)
{
    (void)port; (void)slave_id; (void)function; (void)data; (void)data_len; (void)timestamp_us;
}

esp_err_t cache_multimaster_register_handlers(httpd_handle_t server)
{
    (void)server;
    return 0; /* ESP_OK */
}

/* ---- Reset helper -------------------------------------------------------- */

void mock_cache_multimaster_reset(void)
{
    mock_lookup_result        = CACHE_LOOKUP_FOUND;
    mock_lookup_value         = 0;
    mock_lookup_call_count    = 0;
    mock_lookup_last_slave_id = 0;
    mock_lookup_last_fc       = 0;
    mock_lookup_last_address  = 0;
    mock_lookup_last_timeout  = 0;
    mock_cache_enabled        = true;

    memset(mock_lookup_results,    0, sizeof(mock_lookup_results));
    memset(mock_lookup_values_arr, 0, sizeof(mock_lookup_values_arr));
    mock_lookup_arr_count = 0;
    mock_lookup_arr_index = 0;
}
