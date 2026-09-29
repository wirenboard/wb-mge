#pragma once

/*
 * GPS-disciplined NTP server (stratum 1) for WB-MGU with a WBE2R-R-GPS module in its WBE2
 * slot: NMEA RMC on the port-2 UART names the second, the module's PPS on the port-2
 * direction pin (GPIO13) marks where it starts, and a UDP server on port 123 answers with it.
 *
 * The feature needs that board (PPS arrives on GPIO13 only there), a build in which no Modbus
 * role owns UART2 and its pins, and real hardware - so it is compiled in and started only in
 * that one combination, and everywhere else its sources compile to nothing.
 */

// Explicit: CONFIG_WB_MODBUS_ROLE_NONE comes from sdkconfig.h, and mb_role.h silently resolves
// to the slave role without it, which would compile the feature out on the one build it is for.
#include "sdkconfig.h"
#include "mb_role.h"

#if (WB_MB_ROLE_NONE && (!QEMU_BUILD) && defined(MODEL_mgu_v1))
    #define GPS_NTP_ENABLED     1
#else
    #define GPS_NTP_ENABLED     0
#endif

// Starts the GPS timebase and the NTP server. Never aborts: every failure is logged and leaves
// the rest of the firmware running. Call once, after network_init().
void gps_ntp_start(void);
