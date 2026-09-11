// Mock of the Airzone gateway store for the settings_manager unit tests.
// settings_manager.c reaches into this module for two things — refreshing the RAM cache the
// register block serves the four settings from, and bumping the settings counter the Z-Wave board
// watches — so the mock counts the calls instead of keeping a store. It also snapshots the four
// stored settings at the moment of the bump, and how many refreshes had happened by then: the
// board reads those registers only in the instant the counter moves, so both say exactly what the
// board would see, and a test can tell "values written first" from "counter moved first".

#include "airzone_gw.h"

#include "setting_items.h"

int mock_airzone_settings_counter = 0;
int mock_airzone_seen_slave = 0;
int mock_airzone_seen_zone = 0;
int mock_airzone_seen_baud_code = 0;
int mock_airzone_seen_product = 0;
int mock_airzone_reload_called = 0;
int mock_airzone_reloads_at_settings_bump = -1;

void mock_airzone_gw_reset(void)
{
    mock_airzone_settings_counter = 0;
    mock_airzone_seen_slave = 0;
    mock_airzone_seen_zone = 0;
    mock_airzone_seen_baud_code = 0;
    mock_airzone_seen_product = 0;
    mock_airzone_reload_called = 0;
    mock_airzone_reloads_at_settings_bump = -1;
}

void airzone_gw_inc_settings_counter(void)
{
    mock_airzone_settings_counter++;
    mock_airzone_seen_slave     = setting_items_read_int(KEY_AIRZONE_ADDRESS);
    mock_airzone_seen_zone      = setting_items_read_int(KEY_AIRZONE_ZONE);
    mock_airzone_seen_baud_code = setting_items_read_int(KEY_AIRZONE_SPEED);
    mock_airzone_seen_product   = setting_items_read_int(KEY_AIRZONE_PRODUCT);
    mock_airzone_reloads_at_settings_bump = mock_airzone_reload_called;
}

void airzone_gw_reload_settings(void)
{
    mock_airzone_reload_called++;
}
