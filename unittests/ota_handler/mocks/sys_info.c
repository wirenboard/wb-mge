/* sys_info mock for the ota_handler unit tests. Provides the global instance the translation unit
 * references; ota_handler.c reads only device_signature, which ota_test_env_reset() fills with the
 * signature the synthetic firmware carries. */

#include "sys_info.h"

sys_info_t sys_info;
