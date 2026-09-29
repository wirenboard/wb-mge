#pragma once

#include <stdbool.h>
#include <stdint.h>

// PPS intervals the status keeps its minimum, maximum and average over.
#define GPS_TIME_PPS_HISTORY_LEN            60

typedef enum {
    GPS_TIME_ANTENNA_UNKNOWN = 0,
    GPS_TIME_ANTENNA_OK,
    GPS_TIME_ANTENNA_OPEN,
    GPS_TIME_ANTENNA_SHORT,
    GPS_TIME_ANTENNA_COUNT
} gps_time_antenna_t;

// A consistent copy of the timebase, PPS and NMEA link statistics for the GET /gps page.
// Ordered by field size, not by topic, to keep the padding down.
typedef struct {
    int64_t unix_us;                        // gps_time_now() time; meaningful when anchored
    int64_t anchor_age_us;                  // since the anchored edge; meaningful when anchored
    int64_t locked_for_us;                  // meaningful when locked
    int64_t last_pps_age_us;                // meaningful when pps_count > 0
    int64_t interval_last_us;               // the four interval fields: meaningful when interval_count > 0
    int64_t interval_min_us;
    int64_t interval_max_us;
    double interval_avg_us;                 // over plausible intervals only: meaningful when interval_avg_known
    bool interval_avg_known;
    int64_t rmc_delay_ms;                   // PPS edge to the completion of the last valid RMC
    uint64_t nmea_bytes;

    uint32_t lock_count;
    uint32_t loss_count;
    uint32_t pps_count;
    uint32_t interval_count;                // intervals the fields above cover, 0..GPS_TIME_PPS_HISTORY_LEN
    uint32_t baud;
    uint32_t sentences;                     // passed their checksum
    uint32_t checksum_errors;
    uint32_t rmc_valid;
    uint32_t rmc_invalid;
    gps_time_antenna_t antenna;

    bool synced;                            // gps_time_now() at the moment of the copy
    bool anchored;                          // an RMC was ever paired with a PPS edge
    bool locked;                            // synced, and the lock has been registered
    bool rmc_delay_known;
    bool link_ok;                           // false while the baud rate is being detected
} gps_time_status_t;

// Brings up the GPS receiver on the port-2 UART, arms the PPS interrupt and starts the NMEA
// reader task. The task finds the module at 115200 or 9600 baud (8N1), moves it to 115200 and
// sets its output to GGA, GSA, GSV, RMC and the antenna status. Logs and returns on any
// failure; never aborts.
void gps_time_start(void);

// Current UTC time as unix microseconds, derived from the latest PPS edge and the second it
// was paired with.
//
// Returns true (synced) while an anchor exists, the latest PPS edge is less than 2 s old and
// the anchor itself less than 10 s old. On false the values are still the best estimate the
// last anchor gives; when there was never an anchor both are 0.
//
// unix_us     - the current time
// ref_unix_us - the start of the second the latest PPS edge marked
bool gps_time_now(int64_t *unix_us, int64_t *ref_unix_us);

// Fills status; safe from any task.
void gps_time_get_status(gps_time_status_t *status);
