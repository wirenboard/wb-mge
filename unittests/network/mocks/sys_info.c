// Storage for the sys_info global that network.c publishes its state into.
// main/sys_info.c is deliberately not linked: it pulls in efuse, PSRAM and the app
// descriptor, none of which the WiFi settings logic touches.

#include <string.h>

#include "sys_info.h"
#include "sys_info_mock.h"

sys_info_t sys_info = {0};

void mock_sys_info_reset(void)
{
    memset(&sys_info, 0, sizeof(sys_info));
}
