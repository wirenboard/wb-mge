#include "gps_ntp.h"

#if GPS_NTP_ENABLED

#include "gps_time.h"
#include "ntp_server.h"

// The server is started even when the timebase could not come up: it then answers every
// request as unsynchronised, which a client reports instead of timing out.
void gps_ntp_start(void)
{
    gps_time_start();
    ntp_server_start();
}

#endif // GPS_NTP_ENABLED
