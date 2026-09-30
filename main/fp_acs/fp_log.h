#pragma once

/*
 * The access log: every opening attempt, granted or not, from the sensor and from the Telegram
 * bot. A ring of FP_LOG_CAPACITY records in namespace "fp_log" of the "fp_nvs" partition (one
 * blob per slot, "e<slot>", slot = seq % FP_LOG_CAPACITY, and the u32 "next"), so it survives
 * reboots and power loss, with a RAM copy in PSRAM that every read is served from.
 *
 * seq is persistent and keeps growing across reboots, and it is the seq the event gets in GET
 * /fp/status as well. Records are stamped with the wall clock that SNTP sets, or with
 * FP_LOG_TIME_UNKNOWN while the clock has not been set since boot.
 *
 * Without the partition (fp_store_init() failed) the log lives in RAM only, and a failed NVS
 * write keeps the record in RAM: either way it is lost at the next reboot, and a warning says so.
 * Every function is safe to call from any task.
 */

#include "fp_acs.h"

#if FP_ACS_ENABLED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fp_store.h"

#define FP_LOG_CAPACITY             200
#define FP_LOG_TIME_UNKNOWN         0

typedef struct {
    uint32_t        seq;                                // 1 for the first record ever written
    uint32_t        time;                               // UTC unix seconds, or FP_LOG_TIME_UNKNOWN
    fp_acs_source_t source;
    fp_acs_result_t result;
    uint8_t         user_id;                            // FP_STORE_NO_USER when nobody
    char            user_name[FP_STORE_NAME_MAX + 1];   // the name at the time; empty when nobody
    bool            has_score;
    uint16_t        score;
} fp_log_entry_t;

// Loads the log from NVS. Call once, after fp_store_init(), which mounts the partition.
void     fp_log_init(void);
// Starts SNTP, which sets the clock the records are stamped with. Needs esp_netif_init() to have
// run (network_init()), and nothing more: SNTP keeps retrying until there is a network.
void     fp_log_clock_start(void);
// Appends the event, stamped with the current time, and returns the seq it got (event->seq is not
// read); 0 when the log is not running. Blocks for the NVS write.
uint32_t fp_log_append(const fp_acs_event_t *event);
// Copies up to max records, newest first, and returns how many.
size_t   fp_log_snapshot(fp_log_entry_t *out, size_t max);

#endif // FP_ACS_ENABLED
