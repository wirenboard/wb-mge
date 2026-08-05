/* Unit-test mock for the gateway self-request handler (mb_device).
 * The real implementation pulls in sys_info / voltage_monitor / esp_heap_caps /
 * esp_reset_reason, which are not available in the host test environment.
 * cache_modbus_server.c calls mb_device_handle_self_request() for unit 0xFF;
 * this mock makes the returned ADU controllable from the test and counts calls. */
#include "mb_device.h"
#include <string.h>

bool mb_device_is_self(uint8_t unit_id)
{
    return unit_id == MB_DEVICE_UNIT_ID;
}

/* ---- Controllable self-request handler ----------------------------------- */

static uint8_t s_mock_resp[260];
static size_t  s_mock_resp_len;
int mock_mb_device_handle_called;

void mock_mb_device_set_response(const uint8_t *buf, size_t len)
{
    if (len > sizeof(s_mock_resp)) {
        len = sizeof(s_mock_resp);
    }
    memcpy(s_mock_resp, buf, len);
    s_mock_resp_len = len;
}

void mock_mb_device_reset(void)
{
    s_mock_resp_len = 0;
    mock_mb_device_handle_called = 0;
}

size_t mb_device_handle_self_request(const uint8_t *req, size_t req_len,
                                     uint8_t *resp_buf)
{
    (void)req; (void)req_len;
    mock_mb_device_handle_called++;
    memcpy(resp_buf, s_mock_resp, s_mock_resp_len);
    return s_mock_resp_len;
}
