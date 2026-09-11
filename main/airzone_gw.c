#include "airzone_gw.h"

#include "setting_items.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

static const char *TAG = "airzone_gw";

#define AIRZONE_GW_US_PER_S   1000000

/* Counters are cached in RAM and written through on every increment, so a reader
 * never pays an NVS read and the stored value never lags the one the board saw. */
static uint16_t s_inclusion_counter;
static uint16_t s_settings_counter;

/* The four installer settings obey the same rule: cached here, refreshed by whoever
 * writes them. Every ordinary poll of the register block reads them from the UART
 * event task, which must not block, and an NVS read takes the global NVS mutex the
 * HTTP task holds across a settings save. */
static uint16_t s_address;
static uint16_t s_zone;
static uint16_t s_speed_code;
static uint16_t s_product_type;

/* Mirror block: RAM only. s_mirror_valid stays false until the board writes, and
 * s_mirror_written_s is the time of the last write, in seconds since boot.
 * Seconds and not the raw esp_timer microseconds on purpose: this is written by the
 * UART task and read by the HTTP task, and on a 32-bit core a 64-bit read is two
 * words, so a carry between them would hand the page an age of ~4295 s — "the board
 * stopped reporting" for a board that never stopped. A uint32_t is one word. */
static uint16_t s_mirror[AIRZONE_GW_MIRROR_COUNT];
static bool     s_mirror_valid;
static uint32_t s_mirror_written_s;

/* Read a counter from storage, clamped into the u16 the register carries. */
static uint16_t read_counter(const char *key)
{
    int stored = setting_items_read_int(key);
    if (stored < 0) {
        return 0;
    }
    if (stored > 0xFFFF) {
        return 0xFFFF;
    }
    return (uint16_t)stored;
}

/*
 * Persist the next value of one counter and publish it into its RAM copy only once the
 * write has succeeded.
 *
 * The order is what picks the failure mode. Publishing first would hand the board an
 * event the flash never recorded, so the next reboot would replay it — the "extra event
 * after the next reboot" this module exists to avoid. Persisting first means a failed
 * write costs the event entirely: the board never sees it and the installer presses
 * again, which is the harmless half of the choice.
 */
static uint16_t inc_counter(const char *key, uint16_t *counter)
{
    uint16_t next = (uint16_t)(*counter + 1u);   /* wraps at 65535 by design */
    esp_err_t ret = setting_items_save_int(key, (int)next);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist %s = %u: %s", key, (unsigned)next, esp_err_to_name(ret));
        return *counter;
    }
    *counter = next;
    return next;
}

void airzone_gw_init(void)
{
    s_inclusion_counter = read_counter(KEY_AIRZONE_INCL_CNT);
    s_settings_counter  = read_counter(KEY_AIRZONE_SET_CNT);

    airzone_gw_reload_settings();

    memset(s_mirror, 0, sizeof(s_mirror));
    s_mirror_valid     = false;
    s_mirror_written_s = 0;

    ESP_LOGI(TAG, "Airzone gateway store ready (inclusion=%u, settings=%u)",
             (unsigned)s_inclusion_counter, (unsigned)s_settings_counter);
}

void airzone_gw_reload_settings(void)
{
    s_address      = (uint16_t)setting_items_read_int(KEY_AIRZONE_ADDRESS);
    s_zone         = (uint16_t)setting_items_read_int(KEY_AIRZONE_ZONE);
    s_speed_code   = (uint16_t)setting_items_read_int(KEY_AIRZONE_SPEED);
    s_product_type = (uint16_t)setting_items_read_int(KEY_AIRZONE_PRODUCT);
}

uint16_t airzone_gw_get_address(void)
{
    return s_address;
}

uint16_t airzone_gw_get_zone(void)
{
    return s_zone;
}

uint16_t airzone_gw_get_speed_code(void)
{
    return s_speed_code;
}

uint16_t airzone_gw_get_product_type(void)
{
    return s_product_type;
}

uint16_t airzone_gw_get_inclusion_counter(void)
{
    return s_inclusion_counter;
}

uint16_t airzone_gw_get_settings_counter(void)
{
    return s_settings_counter;
}

void airzone_gw_inc_inclusion_counter(void)
{
    uint16_t value = inc_counter(KEY_AIRZONE_INCL_CNT, &s_inclusion_counter);
    ESP_LOGI(TAG, "Z-Wave inclusion requested, counter = %u", (unsigned)value);
}

void airzone_gw_inc_settings_counter(void)
{
    uint16_t value = inc_counter(KEY_AIRZONE_SET_CNT, &s_settings_counter);
    ESP_LOGI(TAG, "Airzone settings changed, counter = %u", (unsigned)value);
}

void airzone_gw_mirror_set_reg(unsigned index, uint16_t value)
{
    if (index >= AIRZONE_GW_MIRROR_COUNT) {
        return;
    }
    s_mirror[index]    = value;
    s_mirror_valid     = true;
    s_mirror_written_s = (uint32_t)(esp_timer_get_time() / AIRZONE_GW_US_PER_S);
}

bool airzone_gw_mirror_get_reg(unsigned index, uint16_t *value_out)
{
    if ((index >= AIRZONE_GW_MIRROR_COUNT) || (value_out == NULL)) {
        return false;
    }
    *value_out = s_mirror[index];
    return true;
}

void airzone_gw_mirror_get(airzone_gw_mirror_t *out)
{
    if (out == NULL) {
        return;
    }
    memcpy(out->values, s_mirror, sizeof(out->values));
    out->valid = s_mirror_valid;
    if (s_mirror_valid) {
        uint32_t now_s = (uint32_t)(esp_timer_get_time() / AIRZONE_GW_US_PER_S);
        out->age_s = (now_s > s_mirror_written_s) ? (now_s - s_mirror_written_s) : 0u;
    } else {
        out->age_s = 0u;
    }
}
