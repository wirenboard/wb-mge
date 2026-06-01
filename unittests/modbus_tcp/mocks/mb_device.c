/* Unit-test mock for the gateway self-request handler (mb_device).
 * The real implementation pulls in sys_info / voltage_monitor / esp_heap_caps /
 * esp_reset_reason, which are not available in the host test environment. These
 * stubs satisfy the linker; modbus_tcp.c calls mb_device_handle_self_request()
 * for unit 0xFF, but no current modbus_tcp test addresses that path. */
#include "mb_device.h"
#include <string.h>

bool mb_device_is_self(uint8_t unit_id)
{
    return unit_id == MB_DEVICE_UNIT_ID;
}

size_t mb_device_handle_self_request(const uint8_t *req, size_t req_len,
                                     uint16_t task_stack_size_bytes, uint8_t *resp_buf)
{
    (void)req; (void)req_len; (void)task_stack_size_bytes;
    /* Minimal exception-shaped ADU (MBAP header + 1 byte): unit 0xFF, FC|0x80,
     * exception 0x02. Callers send whatever we return, so it must be > 0 bytes. */
    static const uint8_t adu[9] = {
        0x00, 0x00,  /* transaction id */
        0x00, 0x00,  /* protocol id    */
        0x00, 0x03,  /* length = 3     */
        0xFF,        /* unit id        */
        0x83,        /* FC03 | 0x80    */
        0x02,        /* exception code */
    };
    memcpy(resp_buf, adu, sizeof(adu));
    return sizeof(adu);
}
