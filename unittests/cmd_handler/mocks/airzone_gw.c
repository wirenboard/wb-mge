/* airzone_gw mock for the cmd_handler unit test. One command reaches this module: zwave_include
 * bumps the inclusion counter the Z-Wave board polls. set_default_settings reaches the module too,
 * but through settings_factory_reset() — the settings counter and the settings reload it drives
 * are mocked and tested in the settings_update suite, which owns that function.
 *
 * Only the inclusion counter is defined here, so a settings-counter call that reappears in
 * cmd_handler.c fails to link rather than passing unnoticed. */

#include "airzone_gw.h"

int mock_airzone_inclusion_counter = 0;

void mock_airzone_gw_reset(void)
{
    mock_airzone_inclusion_counter = 0;
}

void airzone_gw_inc_inclusion_counter(void)
{
    mock_airzone_inclusion_counter++;
}
