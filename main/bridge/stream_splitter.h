#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Maximum number of sub-frames extractable from one merged stream buffer */
#define STREAM_SPLITTER_MAX_FRAMES 16

/* One sub-frame discovered inside a merged Modbus RTU byte stream */
typedef struct {
    const uint8_t *data; /* pointer into the original buffer */
    size_t         len;  /* byte count of this sub-frame */
    bool           crc_valid; /* true if CRC check passed */
} stream_frame_t;

/*
 * Split a merged Modbus RTU byte stream into individual frames.
 *
 * buf          — input buffer (may contain multiple back-to-back frames)
 * len          — total byte count in buf
 * context_slave — slave_id from the last known master request; 0 means no context
 * context_fc    — function code from the last known master request; 0 means no context
 * out_frames    — caller-allocated array of at least STREAM_SPLITTER_MAX_FRAMES entries
 *
 * Returns the number of frames written to out_frames (>= 1).
 * The last frame may have crc_valid == false if the tail could not be parsed.
 *
 * Guarantees: all bytes in buf are covered by the returned frames
 * (sum of frame lengths == len).
 */
int stream_split(const uint8_t *buf, size_t len,
                 uint8_t context_slave, uint8_t context_fc,
                 stream_frame_t *out_frames);
