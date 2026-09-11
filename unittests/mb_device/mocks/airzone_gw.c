// Mock of the Airzone gateway store for the mb_device unit tests.
// The settings and counters are plain settable values; the mirror block behaves like
// the real one (RAM, written by the board, read back as written) so the FC16 write path
// can be checked end to end.

#include "airzone_gw.h"

#include <string.h>

static uint16_t mock_address;
static uint16_t mock_zone;
static uint16_t mock_speed;
static uint16_t mock_product;
static uint16_t mock_inclusion_counter;
static uint16_t mock_settings_counter;

static uint16_t mock_mirror[AIRZONE_GW_MIRROR_COUNT];
static bool     mock_mirror_valid;

void airzone_gw_init(void)
{
}

void airzone_gw_reload_settings(void)
{
}

uint16_t airzone_gw_get_address(void)
{
    return mock_address;
}

uint16_t airzone_gw_get_zone(void)
{
    return mock_zone;
}

uint16_t airzone_gw_get_speed_code(void)
{
    return mock_speed;
}

uint16_t airzone_gw_get_product_type(void)
{
    return mock_product;
}

uint16_t airzone_gw_get_inclusion_counter(void)
{
    return mock_inclusion_counter;
}

uint16_t airzone_gw_get_settings_counter(void)
{
    return mock_settings_counter;
}

void airzone_gw_inc_inclusion_counter(void)
{
    mock_inclusion_counter++;
}

void airzone_gw_inc_settings_counter(void)
{
    mock_settings_counter++;
}

void airzone_gw_mirror_set_reg(unsigned index, uint16_t value)
{
    if (index >= AIRZONE_GW_MIRROR_COUNT) {
        return;
    }
    mock_mirror[index] = value;
    mock_mirror_valid  = true;
}

bool airzone_gw_mirror_get_reg(unsigned index, uint16_t *value_out)
{
    if ((index >= AIRZONE_GW_MIRROR_COUNT) || (value_out == NULL)) {
        return false;
    }
    *value_out = mock_mirror[index];
    return true;
}

void airzone_gw_mirror_get(airzone_gw_mirror_t *out)
{
    if (out == NULL) {
        return;
    }
    memcpy(out->values, mock_mirror, sizeof(out->values));
    out->valid = mock_mirror_valid;
    out->age_s = 0u;
}

/* ---- Test helpers -------------------------------------------------------- */

void mock_airzone_set_settings(uint16_t address, uint16_t zone, uint16_t speed, uint16_t product)
{
    mock_address = address;
    mock_zone    = zone;
    mock_speed   = speed;
    mock_product = product;
}

void mock_airzone_set_counters(uint16_t inclusion, uint16_t settings)
{
    mock_inclusion_counter = inclusion;
    mock_settings_counter  = settings;
}

uint16_t mock_airzone_get_mirror(unsigned index)
{
    return (index < AIRZONE_GW_MIRROR_COUNT) ? mock_mirror[index] : 0u;
}

void mock_airzone_reset(void)
{
    mock_address = 0;
    mock_zone    = 0;
    mock_speed   = 0;
    mock_product = 0;
    mock_inclusion_counter = 0;
    mock_settings_counter  = 0;
    memset(mock_mirror, 0, sizeof(mock_mirror));
    mock_mirror_valid = false;
}
