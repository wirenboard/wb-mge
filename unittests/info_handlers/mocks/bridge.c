/* bridge mock for the info_handlers unit test. Only the active-connection
 * counter is referenced (by the RS485 info builder); link-only stub. */

#include "bridge.h"

int tcp_server_active_connections(tcp_server_num_t server_num)
{
    (void)server_num;
    return 0;
}
