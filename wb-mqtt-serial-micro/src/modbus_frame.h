#pragma once
/*
 * Low-level Modbus RTU framing helpers.
 * Exported so unit tests can verify CRC and frame construction
 * without needing a real serial port.
 */
#include <stdint.h>

/* Standard Modbus CRC-16/IBM.
 * Returns 0xFFFF for empty input (buf=NULL and len=0 is valid and returns 0xFFFF).
 * buf must not be NULL if len > 0. */
uint16_t modbus_crc16(const uint8_t *buf, int len);

/*
 * Build a Modbus RTU request frame in out[].
 * pdu: bytes starting with function code (NOT including slave ID).
 * out must be at least pdu_len + 3 bytes.
 * Returns total frame length, or 0 on error.
 */
int modbus_make_frame(uint8_t slave, const uint8_t *pdu, int pdu_len,
                      uint8_t *out, int out_size);
