#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * NMEA 0183 text helpers shared by the GPS modules: the checksum, splitting a sentence into
 * fields in place, and parsing those fields. Plain string code, no ESP-IDF dependency.
 */

#define GPS_NMEA_START                      '$'

// More than any sentence parsed here has: GSV with four satellites and the NMEA 4.1 signal ID
// has 21 fields, GSA with the system ID 19.
#define GPS_NMEA_FIELDS_MAX                 24

typedef struct {
    const char *field[GPS_NMEA_FIELDS_MAX];     // field[0] is the address, e.g. "$GPRMC"
    size_t count;
} gps_nmea_sentence_t;

// XOR of len characters: of what lies between '$' and '*'.
uint8_t gps_nmea_checksum(const char *data, size_t len);

// Checks "$<body>*hh" (hh in either case) and returns the length of "$<body>".
bool gps_nmea_verify(const char *line, size_t len, size_t *body_len);

// Writes "$<body>*hh\r\n" with the checksum computed here. Returns its length, or -1 when it
// does not fit command_size.
int gps_nmea_format_command(const char *body, char *command, size_t command_size);

// Splits a NUL-terminated "$<body>" in place at every ','; empty fields are kept, unlike with
// strtok(). False when it has more than GPS_NMEA_FIELDS_MAX fields.
bool gps_nmea_split(char *body, gps_nmea_sentence_t *sentence);

// Field `index`, or "" when the sentence has fewer fields.
const char *gps_nmea_field(const gps_nmea_sentence_t *sentence, size_t index);

// True for "$<talker><type>" with a two-letter talker, e.g. type "RMC" matches "$GNRMC".
bool gps_nmea_is_type(const gps_nmea_sentence_t *sentence, const char *type);

// True when the two-letter talker of the address is `talker`, e.g. "GP".
bool gps_nmea_talker_is(const gps_nmea_sentence_t *sentence, const char *talker);

// A whole field as a decimal integer within [min, max]. False on an empty field.
bool gps_nmea_parse_int(const char *field, long min, long max, long *value);

// A whole field as a finite decimal number. False on an empty field.
bool gps_nmea_parse_double(const char *field, double *value);

// "ddmm.mmmm" and "N"/"S", or "dddmm.mmmm" and "E"/"W", as signed decimal degrees.
bool gps_nmea_parse_latitude(const char *value, const char *hemisphere, double *degrees);
bool gps_nmea_parse_longitude(const char *value, const char *hemisphere, double *degrees);
