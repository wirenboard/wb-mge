#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NTP_SERVER_CLIENT_TEXT_LEN          16      // "255.255.255.255" and its NUL

typedef struct {
    uint32_t requests;                      // datagrams received on port 123
    uint32_t answered_synced;               // replies sent with LI=0, stratum 1
    uint32_t answered_unsynced;             // replies sent with LI=3, stratum 0
    uint32_t ignored;                       // datagrams shorter than an NTP header or not from a client
    bool has_request;                       // the two below are meaningful
    char last_client[NTP_SERVER_CLIENT_TEXT_LEN];
    int64_t last_request_age_us;
} ntp_server_stats_t;

// Opens a UDP socket on INADDR_ANY:123 and starts the task that answers NTP client requests
// from gps_time_now(). Logs and returns on any failure; never aborts.
void ntp_server_start(void);

// Fills stats; safe from any task.
void ntp_server_get_stats(ntp_server_stats_t *stats);
