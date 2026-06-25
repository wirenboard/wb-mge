/* port_manager mock for the info_handlers unit test. Referenced by the RS485
 * info builder (not the function under test); benign link-only stubs. */

#include "bridge/port_manager.h"

pm_mode_t port_manager_get_mode(unsigned port_index)
{
    (void)port_index;
    return PM_MODE_DISABLED;
}

const char *port_manager_mode_to_str(pm_mode_t mode)
{
    (void)mode;
    return "disabled";
}

bool port_manager_get_cache(unsigned port_index)
{
    (void)port_index;
    return false;
}
