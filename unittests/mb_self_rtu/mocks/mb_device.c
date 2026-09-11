// Mock of the unit-0xFF Modbus responder for the mb_self_rtu tests.
// Records the request it was handed and answers with a canned response, so the tests
// can assert WHICH bytes reached the responder and WHEN, without depending on the real
// register map (that has its own suite).

#include "mb_device.h"
#include "mock_mb_device.h"

#include <string.h>

mock_mb_device_calls_t mock_mb_device_calls = {0};

void mock_mb_device_reset(void)
{
    memset(&mock_mb_device_calls, 0, sizeof(mock_mb_device_calls));
    /* Default answer: a plausible 4-register FC03 response, CRC included. */
    static const uint8_t default_resp[] = {0xFF, 0x03, 0x04, 0x11, 0x22, 0x33, 0x44, 0x9A, 0xBC};
    memcpy(mock_mb_device_calls.resp, default_resp, sizeof(default_resp));
    mock_mb_device_calls.resp_len = sizeof(default_resp);
}

bool mb_device_is_self(uint8_t unit_id)
{
    return unit_id == MB_DEVICE_UNIT_ID;
}

size_t mb_device_rtu_handle_request(const uint8_t *req, size_t req_len, uint8_t *resp_buf)
{
    mock_mb_device_calls.called++;
    mock_mb_device_calls.last_req_len = req_len;
    if (req_len <= sizeof(mock_mb_device_calls.last_req)) {
        memcpy(mock_mb_device_calls.last_req, req, req_len);
    }

    if (mock_mb_device_calls.resp_len > 0) {
        memcpy(resp_buf, mock_mb_device_calls.resp, mock_mb_device_calls.resp_len);
    }
    return mock_mb_device_calls.resp_len;
}
