#pragma once

// Opens a UDP socket on INADDR_ANY:123 and starts the task that answers NTP client requests
// from gps_time_now(). Logs and returns on any failure; never aborts.
void ntp_server_start(void);
