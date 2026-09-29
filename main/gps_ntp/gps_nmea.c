#include "gps_nmea.h"

#include "gps_ntp.h"

#if GPS_NTP_ENABLED

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define GPS_NMEA_CHECKSUM_MARK              '*'
#define GPS_NMEA_FIELD_SEPARATOR            ','
#define GPS_NMEA_CHECKSUM_DIGITS            2
#define GPS_NMEA_TALKER_OFFSET              1       // after the '$'
#define GPS_NMEA_TALKER_LEN                 2
#define GPS_NMEA_DECIMAL_BASE               10

// "ddmm.mmmm": the degrees are what lies above the last two integer digits.
#define GPS_NMEA_DEGREE_SCALE               100.0
#define GPS_NMEA_MINUTES_PER_DEGREE         60.0
#define GPS_NMEA_LATITUDE_MAX               90.0
#define GPS_NMEA_LONGITUDE_MAX              180.0
#define GPS_NMEA_NORTH                      'N'
#define GPS_NMEA_SOUTH                      'S'
#define GPS_NMEA_EAST                       'E'
#define GPS_NMEA_WEST                       'W'


uint8_t gps_nmea_checksum(const char *data, size_t len)
{
    uint8_t checksum = 0;

    for (size_t i = 0; i < len; i++) {
        checksum ^= (uint8_t)data[i];
    }
    return checksum;
}


bool gps_nmea_verify(const char *line, size_t len, size_t *body_len)
{
    if ((len == 0) || (line[0] != GPS_NMEA_START)) {
        return false;
    }

    const char *mark = memchr(line, GPS_NMEA_CHECKSUM_MARK, len);
    if (mark == NULL) {
        return false;
    }

    size_t mark_pos = (size_t)(mark - line);
    if ((mark_pos + 1 + GPS_NMEA_CHECKSUM_DIGITS) != len) {
        return false;
    }

    char expected[GPS_NMEA_CHECKSUM_DIGITS + 1];
    snprintf(expected, sizeof(expected), "%02X", (unsigned)gps_nmea_checksum(line + 1, mark_pos - 1));
    if (strncasecmp(mark + 1, expected, GPS_NMEA_CHECKSUM_DIGITS) != 0) {
        return false;
    }

    *body_len = mark_pos;
    return true;
}


int gps_nmea_format_command(const char *body, char *command, size_t command_size)
{
    int len = snprintf(command, command_size, "%c%s%c%02X\r\n", GPS_NMEA_START, body, GPS_NMEA_CHECKSUM_MARK,
                       (unsigned)gps_nmea_checksum(body, strlen(body)));
    if ((len < 0) || ((size_t)len >= command_size)) {
        return -1;
    }
    return len;
}


bool gps_nmea_split(char *body, gps_nmea_sentence_t *sentence)
{
    char *start = body;

    sentence->count = 0;
    while (1) {
        if (sentence->count >= GPS_NMEA_FIELDS_MAX) {
            return false;
        }
        sentence->field[sentence->count] = start;
        sentence->count++;

        char *separator = strchr(start, GPS_NMEA_FIELD_SEPARATOR);
        if (separator == NULL) {
            return true;
        }
        *separator = '\0';
        start = separator + 1;
    }
}


const char *gps_nmea_field(const gps_nmea_sentence_t *sentence, size_t index)
{
    return (index < sentence->count) ? sentence->field[index] : "";
}


bool gps_nmea_is_type(const gps_nmea_sentence_t *sentence, const char *type)
{
    if (sentence->count == 0) {
        return false;
    }

    const char *address = sentence->field[0];
    const size_t type_offset = GPS_NMEA_TALKER_OFFSET + GPS_NMEA_TALKER_LEN;
    return ((address[0] == GPS_NMEA_START) && (strlen(address) == (type_offset + strlen(type))) &&
            (strcmp(address + type_offset, type) == 0));
}


bool gps_nmea_talker_is(const gps_nmea_sentence_t *sentence, const char *talker)
{
    return ((sentence->count > 0) && (sentence->field[0][0] == GPS_NMEA_START) &&
            (strncmp(sentence->field[0] + GPS_NMEA_TALKER_OFFSET, talker, GPS_NMEA_TALKER_LEN) == 0));
}


bool gps_nmea_parse_int(const char *field, long min, long max, long *value)
{
    if (field[0] == '\0') {
        return false;
    }

    char *end = NULL;
    errno = 0;
    long parsed = strtol(field, &end, GPS_NMEA_DECIMAL_BASE);
    if ((errno != 0) || (*end != '\0') || (parsed < min) || (parsed > max)) {
        return false;
    }

    *value = parsed;
    return true;
}


bool gps_nmea_parse_double(const char *field, double *value)
{
    if (field[0] == '\0') {
        return false;
    }

    char *end = NULL;
    double parsed = strtod(field, &end);
    if ((*end != '\0') || (!isfinite(parsed))) {
        return false;
    }

    *value = parsed;
    return true;
}


static bool gps_nmea_parse_coordinate(const char *value, const char *hemisphere, char positive, char negative,
                                      double limit, double *degrees)
{
    double raw = 0.0;
    if ((!gps_nmea_parse_double(value, &raw)) || (raw < 0.0) || (hemisphere[0] == '\0') ||
        (hemisphere[1] != '\0'))
    {
        return false;
    }

    double whole = floor(raw / GPS_NMEA_DEGREE_SCALE);
    double minutes = raw - (whole * GPS_NMEA_DEGREE_SCALE);
    double result = whole + (minutes / GPS_NMEA_MINUTES_PER_DEGREE);
    if ((minutes >= GPS_NMEA_MINUTES_PER_DEGREE) || (result > limit)) {
        return false;
    }

    if (hemisphere[0] == negative) {
        result = -result;
    } else if (hemisphere[0] != positive) {
        return false;
    }

    *degrees = result;
    return true;
}


bool gps_nmea_parse_latitude(const char *value, const char *hemisphere, double *degrees)
{
    return gps_nmea_parse_coordinate(value, hemisphere, GPS_NMEA_NORTH, GPS_NMEA_SOUTH, GPS_NMEA_LATITUDE_MAX,
                                     degrees);
}


bool gps_nmea_parse_longitude(const char *value, const char *hemisphere, double *degrees)
{
    return gps_nmea_parse_coordinate(value, hemisphere, GPS_NMEA_EAST, GPS_NMEA_WEST, GPS_NMEA_LONGITUDE_MAX,
                                     degrees);
}

#endif // GPS_NTP_ENABLED
