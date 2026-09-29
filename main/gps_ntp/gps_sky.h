#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "gps_nmea.h"

/*
 * What the receiver reports about its fix and the sky, kept for the GET /gps page: GGA, GSA,
 * GSV and the navigation half of RMC. The NMEA reader task feeds it, the web server reads it;
 * every call takes the module's own mutex. A sentence or satellite list not refreshed for 5 s
 * is left out of the snapshot.
 */

typedef enum {
    GPS_SKY_SYSTEM_GPS = 0,
    GPS_SKY_SYSTEM_GLONASS,
    GPS_SKY_SYSTEM_GALILEO,
    GPS_SKY_SYSTEM_BEIDOU,
    GPS_SKY_SYSTEM_QZSS,
    GPS_SKY_SYSTEM_UNKNOWN,
    GPS_SKY_SYSTEM_COUNT
} gps_sky_system_t;

// A GSV sequence has at most 9 sentences of 4 satellites each.
#define GPS_SKY_SYSTEM_SATELLITES_MAX       36
#define GPS_SKY_SATELLITES_MAX              (GPS_SKY_SYSTEM_COUNT * GPS_SKY_SYSTEM_SATELLITES_MAX)

// Elevation, azimuth or SNR the sentence left empty.
#define GPS_SKY_NO_VALUE                    INT16_MIN

typedef struct {
    bool valid;
    double value;
} gps_sky_value_t;

typedef struct {
    uint8_t system;                         // gps_sky_system_t
    bool used;                              // listed in the latest GSA of its system
    uint16_t prn;
    int16_t elevation;                      // degrees, or GPS_SKY_NO_VALUE
    int16_t azimuth;                        // degrees, or GPS_SKY_NO_VALUE
    int16_t snr;                            // dB-Hz, or GPS_SKY_NO_VALUE
} gps_sky_satellite_t;

typedef struct {
    char rmc_status;                        // 'A' or 'V', '\0' when unknown
    gps_sky_value_t mode;                   // GSA: 1 no fix, 2 2D, 3 3D
    gps_sky_value_t quality;                // GGA fix quality
    gps_sky_value_t sats_used;              // GGA
    gps_sky_value_t sats_in_view;           // sum of the GSV totals of every system
    gps_sky_value_t pdop;
    gps_sky_value_t hdop;                   // GSA, else GGA
    gps_sky_value_t vdop;
    gps_sky_value_t lat;                    // RMC, signed degrees
    gps_sky_value_t lon;
    gps_sky_value_t alt_m;                  // GGA, above mean sea level
    gps_sky_value_t speed_kmh;              // RMC
    gps_sky_value_t course_deg;             // RMC
} gps_sky_fix_t;

typedef struct {
    gps_sky_fix_t fix;
    size_t satellite_count;
    gps_sky_satellite_t satellites[GPS_SKY_SATELLITES_MAX];
} gps_sky_snapshot_t;

// Creates the mutex. Until it succeeds every other call is a no-op and snapshots are empty.
esp_err_t gps_sky_init(void);

// Sentences that passed their checksum, split into fields.
void gps_sky_handle_gga(const gps_nmea_sentence_t *sentence);
void gps_sky_handle_gsa(const gps_nmea_sentence_t *sentence);
void gps_sky_handle_gsv(const gps_nmea_sentence_t *sentence);
void gps_sky_handle_rmc(const gps_nmea_sentence_t *sentence);

void gps_sky_get_snapshot(gps_sky_snapshot_t *snapshot);

// "GPS", "GLONASS", "Galileo", "BeiDou", "QZSS" or "unknown".
const char *gps_sky_system_name(gps_sky_system_t system);
