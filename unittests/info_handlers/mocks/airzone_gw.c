/* airzone_gw mock for the info_handlers unit test. Only airzone_gw_mirror_get() is referenced
 * (by info_get_handler); returns a snapshot the board has never written into. */

#include "airzone_gw.h"
#include <string.h>

void airzone_gw_mirror_get(airzone_gw_mirror_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
}
