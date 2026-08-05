/* repeater mock for the info_handlers unit test. Only repeater_get_stats() is
 * referenced (by info_get_handler); returns a zeroed snapshot. */

#include "bridge/repeater.h"
#include <string.h>

void repeater_get_stats(repeater_stats_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
}
