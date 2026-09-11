/* airzone_gw mock for the cmd_handler unit test. Two commands reach this module: zwave_include
 * bumps the inclusion counter the Z-Wave board polls, and set_default_settings reloads the cached
 * settings and then bumps the settings counter. The mock counts both, and records how many
 * reloads had happened when the settings counter moved — that is what tells "values first, then
 * the counter" apart from the reverse order. */

#include "airzone_gw.h"

int mock_airzone_inclusion_counter = 0;
int mock_airzone_settings_counter = 0;
int mock_airzone_reload_called = 0;
int mock_airzone_reloads_at_settings_bump = -1;

void mock_airzone_gw_reset(void)
{
    mock_airzone_inclusion_counter = 0;
    mock_airzone_settings_counter = 0;
    mock_airzone_reload_called = 0;
    mock_airzone_reloads_at_settings_bump = -1;
}

void airzone_gw_inc_inclusion_counter(void)
{
    mock_airzone_inclusion_counter++;
}

void airzone_gw_inc_settings_counter(void)
{
    mock_airzone_settings_counter++;
    mock_airzone_reloads_at_settings_bump = mock_airzone_reload_called;
}

void airzone_gw_reload_settings(void)
{
    mock_airzone_reload_called++;
}
