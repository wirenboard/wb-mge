#pragma once

#include <stdbool.h>
#include <stdint.h>

// Brings up the GPS receiver on the port-2 UART (NMEA 9600 8N1), switches its output to RMC
// only, arms the PPS interrupt and starts the NMEA reader task. Logs and returns on any
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
