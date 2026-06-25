/* sys_info mock for the info_handlers unit test. Provides the global sys_info
 * instance the translation unit references. Zero-initialised; the function
 * under test does not read it on the perm-disable path. */

#include "sys_info.h"

sys_info_t sys_info;
