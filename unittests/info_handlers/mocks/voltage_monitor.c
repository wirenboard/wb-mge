/* voltage_monitor mock for the info_handlers unit test. Referenced by
 * info_get_handler; link-only stub. */

#include "voltage_monitor.h"

float voltage_monitor_get_sys_voltage(void)
{
    return 0.0f;
}
