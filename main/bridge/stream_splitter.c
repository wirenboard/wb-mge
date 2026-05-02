#include "stream_splitter.h"
#include "modbus_helpers.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Verify Modbus RTU CRC: last 2 bytes of frame must match CRC of preceding bytes.
 * modbus_crc16() returns big-endian value; RTU appends CRC low byte first. */
static bool frame_crc_ok(const uint8_t *buf, size_t len)
{
    if (len < 4) return false;
    uint16_t crc = modbus_crc16(buf, (uint16_t)(len - 2));
    uint8_t crc_lo = (uint8_t)(crc & 0xFF);
    uint8_t crc_hi = (uint8_t)(crc >> 8);
    return (buf[len - 2] == crc_lo) && (buf[len - 1] == crc_hi);
}

/* Compute expected frame length for Fast Modbus frames (slave_id == 0xFD,
 * fc == 0x46 or 0x60).  Returns 0 if length is unknown or exceeds avail. */
static size_t fm_expected_len(const uint8_t *buf, size_t avail)
{
    if (avail < 3) return 0;

    uint8_t fc     = buf[1];
    uint8_t subcmd = buf[2];
    size_t  result = 0;

    if (fc == 0x46) {
        switch (subcmd) {
            case 0x01: result = 9;  break;
            case 0x02: result = 9;  break;
            case 0x03: result = 10; break;
            case 0x04: result = 9;  break;
            case 0x10: result = 9;  break;
            case 0x12: result = 5;  break;
            default:   result = 0;  break; /* variable length */
        }
    } else if (fc == 0x60) {
        switch (subcmd) {
            case 0x01: result = 5;  break;
            case 0x02: result = 5;  break;
            case 0x03: result = 10; break;
            case 0x04: result = 5;  break;
            case 0x10: result = 9;  break;
            case 0x12: result = 5;  break;
            default:   result = 0;  break; /* variable length */
        }
    }

    if (result == 0 || result > avail) return 0;
    return result;
}

/* Return the expected full frame length (including slave_id + 2 CRC bytes) for
 * a Modbus RTU frame starting at buf[0..avail-1].
 * is_response selects the response-length formula for FCs that differ between
 * request and response (e.g. FC 01/02/03/04).
 * Returns 0 when the length cannot be determined or exceeds avail. */
static size_t frame_expected_len(const uint8_t *buf, size_t avail, bool is_response)
{
    if (avail < 2) return 0;

    uint8_t fc = buf[1];
    size_t  result = 0;

    /* Fast Modbus: slave_id == 0xFD, fc == 0x46 or 0x60 */
    if (buf[0] == 0xFD && (fc == 0x46 || fc == 0x60)) {
        return fm_expected_len(buf, avail);
    }

    /* Exception response: high bit set on fc byte */
    if (fc & 0x80) {
        result = 5;
        return (result > avail) ? 0 : result;
    }

    switch (fc) {
        /* FC 01/02: Read Coils / Read Discrete Inputs */
        case 0x01:
        case 0x02:
            if (!is_response) {
                result = 8; /* fixed request length */
            } else {
                if (avail < 3) return 0;
                result = 3 + buf[2] + 2; /* 3 header + byte_count data + 2 CRC */
            }
            break;

        /* FC 03/04: Read Holding / Input Registers */
        case 0x03:
        case 0x04:
            if (!is_response) {
                result = 8; /* fixed request length */
            } else {
                if (avail < 3) return 0;
                result = 3 + buf[2] + 2; /* 3 header + byte_count data + 2 CRC */
            }
            break;

        /* FC 05/06: Write Single Coil / Register */
        case 0x05:
        case 0x06:
            result = 8; /* request and response are both fixed 8 bytes */
            break;

        /* FC 08: Diagnostics */
        case 0x08:
            result = 8;
            break;

        /* FC 0F: Write Multiple Coils */
        case 0x0F:
            if (!is_response) {
                if (avail < 7) return 0;
                result = 7 + buf[6] + 2; /* 7 header + byte_count data + 2 CRC */
            } else {
                result = 8; /* fixed response */
            }
            break;

        /* FC 10: Write Multiple Registers */
        case 0x10:
            if (!is_response) {
                if (avail < 7) return 0;
                result = 7 + buf[6] + 2; /* 7 header + byte_count data + 2 CRC */
            } else {
                result = 8; /* fixed response */
            }
            break;

        default:
            return 0; /* unknown FC */
    }

    if (result == 0 || result > avail) return 0;
    return result;
}

/*
 * Split a merged Modbus RTU byte stream into individual frames.
 * See stream_splitter.h for full documentation.
 */
int stream_split(const uint8_t *buf, size_t len,
                 uint8_t context_slave, uint8_t context_fc,
                 stream_frame_t *out_frames)
{
    (void)context_slave; /* kept for API symmetry; context uses context_fc only */

    size_t pos   = 0;
    int    count = 0;
    bool   first_frame = true;

    while (pos < len && count < STREAM_SPLITTER_MAX_FRAMES - 1) {
        const uint8_t *rem     = buf + pos;
        size_t         rem_len = len - pos;
        size_t         frame_len = 0;

        /* Level 1: context hint — the first frame in a merged buffer is likely
         * a slave response to the last known master request. */
        if (first_frame && context_fc != 0) {
            first_frame = false;
            size_t ctx_len = frame_expected_len(rem, rem_len, /*is_response=*/true);
            if (ctx_len != 0 && frame_crc_ok(rem, ctx_len)) {
                frame_len = ctx_len;
            }
            /* If the context hint failed, fall through to Level 2. */
        } else {
            first_frame = false;
        }

        /* Level 2: length table — try request interpretation first, then response. */
        if (frame_len == 0) {
            size_t req_len = frame_expected_len(rem, rem_len, /*is_response=*/false);
            if (req_len != 0 && frame_crc_ok(rem, req_len)) {
                frame_len = req_len;
            }
        }
        if (frame_len == 0) {
            size_t res_len = frame_expected_len(rem, rem_len, /*is_response=*/true);
            if (res_len != 0 && frame_crc_ok(rem, res_len)) {
                frame_len = res_len;
            }
        }

        /* Level 3: CRC scan fallback — try every possible length from 4 up to
         * the remaining byte count and take the first one with a valid CRC. */
        if (frame_len == 0) {
            for (size_t try_len = 4; try_len <= rem_len; try_len++) {
                if (frame_crc_ok(rem, try_len)) {
                    frame_len = try_len;
                    break;
                }
            }
        }

        /* No valid frame boundary found: emit the remainder as a broken frame
         * and stop processing. */
        if (frame_len == 0) {
            out_frames[count].data      = rem;
            out_frames[count].len       = rem_len;
            out_frames[count].crc_valid = false;
            count++;
            pos = len; /* mark all bytes as consumed */
            break;
        }

        out_frames[count].data      = rem;
        out_frames[count].len       = frame_len;
        out_frames[count].crc_valid = true;
        count++;
        pos += frame_len;
    }

    /* Defensive tail: if bytes remain and we still have room, emit them as a
     * broken frame.  This should not happen when the CRC scan works correctly. */
    if (pos < len && count < STREAM_SPLITTER_MAX_FRAMES) {
        out_frames[count].data      = buf + pos;
        out_frames[count].len       = len - pos;
        out_frames[count].crc_valid = false;
        count++;
    }

    /* Always return at least 1 (the whole buffer as one frame if nothing else). */
    if (count == 0) {
        out_frames[0].data      = buf;
        out_frames[0].len       = len;
        out_frames[0].crc_valid = frame_crc_ok(buf, len);
        count = 1;
    }

    return count;
}
