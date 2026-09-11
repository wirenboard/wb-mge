/* airzone_gw mock for the settings_update unit test. settings_factory_reset() reaches this module
 * twice: it reloads the cached Airzone settings the reset put back to their defaults, and then
 * bumps the settings counter the Z-Wave board polls. The mock counts both, and records how many
 * reloads had happened when the settings counter moved — that is what tells "values first, then
 * the counter" apart from the reverse order.
 *
 * The inclusion counter is here for the opposite reason: nothing in settings_update.c may ever
 * touch it, and a mock that does not define it would let such a call be a link error instead of a
 * failing assertion with a message. */

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
