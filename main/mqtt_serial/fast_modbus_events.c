#include "fast_modbus_events.h"
#include "modbus_frame.h"   /* modbus_crc16 */
#include <string.h>

/* Append CRC16 (low byte first, standard Modbus) after a `body_len`-byte body. */
static void put_crc(uint8_t *frame, int body_len)
{
    uint16_t crc = modbus_crc16(frame, body_len);
    frame[body_len]     = (uint8_t)(crc & 0xFF);
    frame[body_len + 1] = (uint8_t)(crc >> 8);
}

int fm_build_request(uint8_t *out, int out_sz,
                     uint8_t min_slave, uint8_t max_data_len,
                     uint8_t ack_slave, uint8_t ack_flag)
{
    if (!out || out_sz < 9) return -1;
    out[0] = FM_ADDR_BROADCAST;
    out[1] = FM_CMD;
    out[2] = FM_SUB_REQUEST;
    out[3] = min_slave;
    out[4] = max_data_len;
    out[5] = ack_slave;
    out[6] = ack_flag;
    put_crc(out, 7);
    return 9;
}

int fm_build_enable(uint8_t *out, int out_sz, uint8_t slave,
                    const fm_enable_range_t *ranges, int n_ranges)
{
    if (!out || !ranges || n_ranges <= 0) return -1;

    int settings_len = 0;
    for (int i = 0; i < n_ranges; i++) settings_len += 4 + ranges[i].count;
    if (settings_len > 255) return -1;             /* length field is one byte */

    int total = 4 + settings_len + 2;
    if (out_sz < total) return -1;

    out[0] = slave;
    out[1] = FM_CMD;
    out[2] = FM_SUB_ENABLE;
    out[3] = (uint8_t)settings_len;
    int p = 4;
    for (int i = 0; i < n_ranges; i++) {
        out[p++] = ranges[i].reg_type;
        out[p++] = (uint8_t)(ranges[i].addr >> 8);     /* big-endian address */
        out[p++] = (uint8_t)(ranges[i].addr & 0xFF);
        out[p++] = ranges[i].count;
        for (int r = 0; r < ranges[i].count; r++) out[p++] = ranges[i].priority;
    }
    put_crc(out, p);
    return p + 2;
}

int fm_parse_response(const uint8_t *buf, int len,
                      fm_event_t *events, int max_events,
                      uint8_t *out_slave, uint8_t *out_flag)
{
    if (!buf) return -1;

    /* Skip leading 0xFF arbitration padding. */
    int start = 0;
    while (start < len && buf[start] == 0xFF) start++;
    const uint8_t *f = buf + start;
    int flen = len - start;
    if (flen < 5) return -1;                       /* addr,cmd,sub + CRC minimum */

    /* Verify CRC over the frame excluding the trailing 2 CRC bytes. */
    uint16_t crc = modbus_crc16(f, flen - 2);
    uint16_t rx_crc = (uint16_t)f[flen - 2] | ((uint16_t)f[flen - 1] << 8);
    if (crc != rx_crc) return -1;

    if (f[1] != FM_CMD) return -1;
    if (f[2] == FM_SUB_NO_EVENTS) return 0;
    if (f[2] != FM_SUB_EVENTS)    return -1;

    /* 0x11: slave, cmd, sub, flag, count, data_len, events..., CRC */
    if (flen < 8) return -1;                        /* 6 header + 2 CRC */
    if (out_slave) *out_slave = f[0];
    if (out_flag)  *out_flag  = f[3];

    int data_len = f[5];
    int body_end = 6 + data_len;
    if (body_end > flen - 2) return -1;             /* events must fit before CRC */

    int n = 0;
    int p = 6;
    while (p < body_end && n < max_events) {
        int extra = f[p];
        if (p + 4 + extra > body_end) break;        /* truncated / malformed */
        fm_event_t *ev = &events[n];
        ev->data_len = (uint8_t)(extra > FM_MAX_EVENT_DATA ? FM_MAX_EVENT_DATA : extra);
        ev->type = f[p + 1];
        ev->id   = (uint16_t)((f[p + 2] << 8) | f[p + 3]);
        memset(ev->data, 0, sizeof(ev->data));
        memcpy(ev->data, &f[p + 4], ev->data_len);
        p += 4 + extra;
        n++;
    }
    return n;
}

uint32_t fm_event_value(const fm_event_t *ev)
{
    uint32_t v = 0;
    int n = ev->data_len; if (n > 4) n = 4;
    for (int i = 0; i < n; i++) v |= (uint32_t)ev->data[i] << (8 * i);  /* little-endian */
    return v;
}
