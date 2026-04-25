#include "modbus_frame.h"
#include <string.h>
#include <stdint.h>

/* CRC-16/IBM (standard Modbus CRC).  Public so unit tests can call it. */
uint16_t modbus_crc16(const uint8_t *buf, int len)
{
    uint16_t crc = 0xFFFF;
    if (!buf || len <= 0) return crc;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else         crc >>= 1;
        }
    }
    return crc;
}

/* Build a Modbus RTU request frame in out[].
 * Returns frame length, or 0 on error (buffer too small). */
int modbus_make_frame(uint8_t slave, const uint8_t *pdu, int pdu_len,
                      uint8_t *out, int out_size)
{
    if (pdu_len + 3 > out_size) return 0;
    out[0] = slave;
    memcpy(out + 1, pdu, (size_t)pdu_len);
    uint16_t crc = modbus_crc16(out, pdu_len + 1);
    out[pdu_len + 1] = (uint8_t)(crc & 0xFF);
    out[pdu_len + 2] = (uint8_t)(crc >> 8);
    return pdu_len + 3;
}
