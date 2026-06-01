/* Unit-test mock for the gateway device-info register builder (mb_device).
 * The real implementation pulls in sys_info / voltage_monitor / esp_heap_caps /
 * esp_reset_reason, which are not available in the host test environment. These
 * stubs satisfy the linker; no current test addresses unit_id 0xFF, so the
 * builder body is never exercised. */
#include "mb_device.h"

bool mb_device_is_self(uint8_t unit_id)
{
    return unit_id == MB_DEVICE_UNIT_ID;
}

size_t mb_device_build_read_response(uint8_t unit_id, uint8_t fc,
                                     uint16_t transaction_id_net,
                                     uint16_t start_addr, uint16_t count,
                                     uint16_t task_stack_size_bytes,
                                     uint8_t *resp_buf, uint8_t *exc_out)
{
    (void)unit_id; (void)fc; (void)transaction_id_net;
    (void)start_addr; (void)count; (void)task_stack_size_bytes; (void)resp_buf;
    if (exc_out != NULL) {
        *exc_out = 0x02; /* illegal data address */
    }
    return 0;
}
