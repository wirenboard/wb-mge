/* cmd_handler mock for the ota_handler unit tests. On the device cmd_reboot_device() schedules a
 * task that restarts the chip a second later; here it only counts, because "was a reboot scheduled"
 * is the whole question the tests ask of it. */

#include "cmd_handler.h"
#include "ota_mocks.h"

int mock_cmd_reboot_device_call_count = 0;

void mock_cmd_handler_reset(void)
{
    mock_cmd_reboot_device_call_count = 0;
}

void cmd_reboot_device(void)
{
    mock_cmd_reboot_device_call_count++;
}
