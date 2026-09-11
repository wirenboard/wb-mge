#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Store behind the Airzone gateway register block served on unit 0xFF.
 *
 * It holds three kinds of value, and they differ in lifetime on purpose:
 *
 *   - four installer SETTINGS (Airzone Modbus address, zone, line speed code and
 *     product type), persisted like any other setting;
 *   - two event COUNTERS, persisted and increment-only;
 *   - eleven MIRROR registers the Z-Wave board writes into this firmware, kept in
 *     RAM only.
 *
 * The counters are the whole protocol between the two boards: the Z-Wave board
 * remembers the value it last read and treats ANY difference as exactly one event.
 * That is why they must survive a reboot — a counter that came back as zero would
 * read as a fresh event on every boot and, for the inclusion counter, send the node
 * into inclusion each time. Wrapping at 65535 needs no special case for the same
 * reason: the board compares for difference, not for order.
 *
 * The mirror is deliberately NOT persisted: the Z-Wave board is the source of truth
 * for those values and rewrites the whole block within 10 s of any reboot. Until it
 * does, the block reads as invalid, and its age tells the UI how stale it is.
 */

/* Number of mirror registers the board writes (registers 541..551). */
#define AIRZONE_GW_MIRROR_COUNT   11u

/* Snapshot of the mirror block, as returned by airzone_gw_mirror_get(). */
typedef struct {
    uint16_t values[AIRZONE_GW_MIRROR_COUNT];
    bool     valid;   /* false until the board has written the block at least once */
    uint32_t age_s;   /* seconds since the last write; 0 while !valid */
} airzone_gw_mirror_t;

/* Load the persisted settings and counters into RAM and mark the mirror as never
 * written. Must run after setting_items_init(). */
void airzone_gw_init(void);

/*
 * Re-read the four installer settings into the RAM cache. Whoever writes them calls this
 * right after the write succeeded and BEFORE it increments the settings counter, so the
 * board reads the new values in the instant the counter moves.
 *
 * The cache is why it is needed at all: the getters below are reached from the UART event
 * task, which must not block, and an NVS read there would both stall the answer to the
 * board and contend for the global NVS mutex the HTTP task holds across a settings save.
 */
void airzone_gw_reload_settings(void);

/* The four installer settings, read back as stored — a plain RAM read, never NVS. */
uint16_t airzone_gw_get_address(void);
uint16_t airzone_gw_get_zone(void);
uint16_t airzone_gw_get_speed_code(void);
uint16_t airzone_gw_get_product_type(void);

/* The two event counters. */
uint16_t airzone_gw_get_inclusion_counter(void);
uint16_t airzone_gw_get_settings_counter(void);

/*
 * Increment a counter by one (wrapping at 65535) and persist it immediately. Both
 * only ever move on an installer action, so there is no flash-wear case for
 * deferring the write to the settings save timer, and a value that reached the
 * board but not the flash would be read as an extra event after the next reboot.
 * The flash is therefore written first and the board sees the new value only once
 * that succeeded: a failed write loses the event instead of repeating it later.
 *
 * On a settings change the four values must be written BEFORE the settings counter
 * is incremented: the board reads them only in the instant the counter moves, so the
 * reverse order hands it a half-filled form.
 */
void airzone_gw_inc_inclusion_counter(void);
void airzone_gw_inc_settings_counter(void);

/* Store one mirror register written by the board. index is 0-based within the
 * block. Out-of-range indices are ignored. */
void airzone_gw_mirror_set_reg(unsigned index, uint16_t value);

/* Read one mirror register. Returns false (and leaves *value_out alone) when index
 * is out of range. A register of a block the board has never written reads back as
 * 0 — use airzone_gw_mirror_get() to tell that apart from a real zero. */
bool airzone_gw_mirror_get_reg(unsigned index, uint16_t *value_out);

/* Whole-block snapshot with its validity flag and age. */
void airzone_gw_mirror_get(airzone_gw_mirror_t *out);
