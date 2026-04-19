/* Stubs for POSIX functions missing from ESP-IDF lwIP */
#include <net/if.h>

unsigned int if_nametoindex(const char *ifname)
{
    (void)ifname;
    return 0; /* ESP32: single interface, no index lookup needed */
}
