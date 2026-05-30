#include "unity.h"

#include "port_manager.h"
#include "sniffer.h"
#include "setting_items.h"
#include "bridge_mock.h"

#include <string.h>

/* ── Expose mock state from each mock file ─────────────────────────────── */

/* bridge mock state is declared in bridge_mock.h */

/* sniffer.c mock */
extern int mock_sniffer_init_called;
extern int mock_sniffer_attach_called[BRIDGES_COUNT];
extern int mock_sniffer_detach_called[BRIDGES_COUNT];
extern int mock_sniffer_enable_called[BRIDGES_COUNT];
extern int mock_sniffer_disable_called[BRIDGES_COUNT];
extern uint8_t mock_sniffer_reasons[BRIDGES_COUNT];
extern uint8_t mock_sniffer_enable_last_reason[BRIDGES_COUNT];
extern uint8_t mock_sniffer_disable_last_reason[BRIDGES_COUNT];
extern int mock_sniffer_inject_tx_called[BRIDGES_COUNT];
extern uint8_t mock_sniffer_inject_tx_last_data[][256];
extern size_t mock_sniffer_inject_tx_last_len[];
void mock_sniffer_reset(void);

/* cache_multimaster.c mock */
extern int mock_cache_multimaster_init_called;
extern int mock_cache_multimaster_enable_called;
extern int mock_cache_multimaster_disable_called;
extern bool mock_cache_multimaster_enabled;
void mock_cache_multimaster_reset(void);

/* setting_items.c mock helpers for migration tests */
extern bool mock_cache_en[BRIDGES_COUNT];
void mock_setting_items_set_port_mode(unsigned index, const char *value);
const char *mock_setting_items_get_port_mode(unsigned index);

/* cache_modbus_server.c mock */
extern int mock_cache_modbus_server_init_called;
void mock_cache_modbus_server_reset(void);

/* serial.c mock */
extern int mock_serial_deinit_called[BRIDGES_COUNT];
extern int mock_serial_set_rx_timeout_called[BRIDGES_COUNT];
extern uint8_t mock_serial_set_rx_timeout_value[BRIDGES_COUNT];
extern int mock_serial_send_called;
extern uint8_t mock_serial_send_last_data[];
extern size_t mock_serial_send_last_len;
extern esp_err_t mock_serial_send_ret;
void mock_serial_reset(void);

/* setting_items.c mock */
extern bool mock_cache_server_enabled;
extern int mock_cache_port;
extern int mock_setting_items_save_called;
void mock_setting_items_reset(void);

/* rs485_stats.c mock */
extern int mock_rs485_busy_monitor_init_called;
extern int mock_rs485_busy_monitor_reset_called[BRIDGES_COUNT];
extern int mock_rs485_stats_init_called;
extern int mock_rs485_stats_reset_called[BRIDGES_COUNT];
void mock_rs485_stats_reset_all(void);

/* json_utils.c mock */
extern int mock_json_utils_send_error_called;
extern const char *mock_json_utils_send_error_last_msg;
extern int mock_json_utils_send_response_called;
void mock_json_utils_reset(void);
void mock_json_utils_inject_hex(const char *hex);

/* ── setUp / tearDown ───────────────────────────────────────────────────── */

void setUp(void)
{
    port_manager_reset_for_test();
    mock_bridge_reset();
    mock_sniffer_reset();
    mock_cache_multimaster_reset();
    mock_cache_modbus_server_reset();
    mock_serial_reset();
    mock_setting_items_reset();
    mock_rs485_stats_reset_all();
    mock_json_utils_reset();
}

void tearDown(void)
{
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 1. port_manager_mode_to_str()
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_mode_to_str_disabled(void)
{
    TEST_ASSERT_EQUAL_STRING(PORT_MODE_DISABLED_STR, port_manager_mode_to_str(PM_MODE_DISABLED));
}

void test_mode_to_str_tcp_bridge(void)
{
    TEST_ASSERT_EQUAL_STRING(PORT_MODE_TCP_BRIDGE_STR, port_manager_mode_to_str(PM_MODE_TCP_BRIDGE));
}

void test_mode_to_str_passive(void)
{
    TEST_ASSERT_EQUAL_STRING(PORT_MODE_PASSIVE_STR, port_manager_mode_to_str(PM_MODE_PASSIVE));
}

void test_mode_to_str_unknown(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown", port_manager_mode_to_str((pm_mode_t)99));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 2. port_manager_get_mode()
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_get_mode_initial_disabled(void)
{
    /* After reset both ports must be DISABLED */
    TEST_ASSERT_EQUAL(PM_MODE_DISABLED, port_manager_get_mode(0));
    TEST_ASSERT_EQUAL(PM_MODE_DISABLED, port_manager_get_mode(1));
}

void test_get_mode_invalid_index(void)
{
    TEST_ASSERT_EQUAL(PM_MODE_DISABLED, port_manager_get_mode(BRIDGES_COUNT));
    TEST_ASSERT_EQUAL(PM_MODE_DISABLED, port_manager_get_mode(100));
}

void test_get_mode_after_set_mode(void)
{
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PM_MODE_PASSIVE, port_manager_get_mode(0));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 3. port_manager_set_mode() error paths
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_set_mode_invalid_index(void)
{
    esp_err_t ret = port_manager_set_mode(BRIDGES_COUNT, PM_MODE_DISABLED);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

void test_set_mode_tcp_bridge_init_fail(void)
{
    mock_bridge_port_init_should_fail = true;
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_TCP_BRIDGE);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    /* bridge_port_init must have been called */
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_port_init_called);
}

void test_set_mode_passive_serial_fail(void)
{
    mock_bridge_port_init_serial_only_should_fail = true;
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_port_init_serial_only_called);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 4. port_init_mode for each mode (via port_manager_set_mode)
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_set_mode_disabled(void)
{
    /* Setting DISABLED must not call bridge or sniffer */
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_DISABLED);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(0, mock_bridge_calls[0].bridge_port_init_called);
    TEST_ASSERT_EQUAL(0, mock_bridge_calls[0].bridge_port_init_serial_only_called);
    TEST_ASSERT_EQUAL(0, mock_sniffer_attach_called[0]);
}

void test_set_mode_tcp_bridge_success(void)
{
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_TCP_BRIDGE);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PM_MODE_TCP_BRIDGE, port_manager_get_mode(0));
    /* bridge_port_init called once */
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_port_init_called);
    /* bridge_get_serial_desc was queried for attaching sniffer */
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_get_serial_desc_called);
    /* sniffer_attach called for port 0 */
    TEST_ASSERT_EQUAL(1, mock_sniffer_attach_called[0]);
    /* TCP bridge owns its RX timeout: port_init_mode sets the longer PROXY
     * inter-frame timeout exactly once (transport-owned, not overlay). */
    TEST_ASSERT_EQUAL(1, mock_serial_set_rx_timeout_called[0]);
    TEST_ASSERT_EQUAL(SERIAL_RX_TOUT_PROXY, mock_serial_set_rx_timeout_value[0]);
}

void test_set_mode_passive_success(void)
{
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PM_MODE_PASSIVE, port_manager_get_mode(0));
    /* serial-only init called */
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_port_init_serial_only_called);
    /* sniffer attached but NOT enabled (no reason active yet) */
    TEST_ASSERT_EQUAL(1, mock_sniffer_attach_called[0]);
    TEST_ASSERT_EQUAL(0, mock_sniffer_enable_called[0]);
    /* cache not enabled without an overlay */
    TEST_ASSERT_EQUAL(0, mock_cache_multimaster_enable_called);
    /* Passive listener owns the RX timeout: port_init_mode sets the short
     * sniffer inter-frame timeout exactly once (transport-owned, not overlay). */
    TEST_ASSERT_EQUAL(1, mock_serial_set_rx_timeout_called[0]);
    TEST_ASSERT_EQUAL(SERIAL_RX_TOUT_SNIFFER, mock_serial_set_rx_timeout_value[0]);
}

void test_set_mode_passive_with_cache_overlay_enables_cache(void)
{
    /* The global pool now follows persisted INTENT (overlay), not serial state:
     * setting the overlay enables the pool immediately (have:false→want:true),
     * even before any port serial is open. */
    esp_err_t ret = port_manager_set_cache(0, true);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(port_manager_get_cache(0));
    TEST_ASSERT_EQUAL(1, mock_cache_multimaster_enable_called);
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);

    /* Now bring the port up in PASSIVE: the pool is already enabled (no redundant
     * enable → no wipe), and the sniffer CACHE reason is wired in. */
    ret = port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    /* No second enable() — the live pool must not be wiped by a redundant enable. */
    TEST_ASSERT_EQUAL(1, mock_cache_multimaster_enable_called);
    TEST_ASSERT_EQUAL(1, mock_sniffer_enable_called[0]);
    TEST_ASSERT_EQUAL(SNIFF_REASON_CACHE, mock_sniffer_enable_last_reason[0]);
}

void test_set_mode_tcp_bridge_with_cache_overlay_enables_cache(void)
{
    port_manager_set_cache(0, true);
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_TCP_BRIDGE);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    /* TCP bridge with cache overlay also feeds the cache. */
    TEST_ASSERT_EQUAL(1, mock_cache_multimaster_enable_called);
    TEST_ASSERT_EQUAL(SNIFF_REASON_CACHE, mock_sniffer_enable_last_reason[0]);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 5. port_deinit_mode for each mode (via switching to DISABLED)
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_deinit_tcp_bridge(void)
{
    /* First set to TCP_BRIDGE */
    port_manager_set_mode(0, PM_MODE_TCP_BRIDGE);

    /* Reset call counts so we only measure deinit effects */
    mock_bridge_reset();
    mock_sniffer_reset();
    mock_rs485_stats_reset_all();

    /* Switch to DISABLED — triggers deinit of TCP_BRIDGE */
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_DISABLED);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    TEST_ASSERT_EQUAL(1, mock_sniffer_detach_called[0]);
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_port_deinit_called);
    TEST_ASSERT_EQUAL(1, mock_rs485_busy_monitor_reset_called[0]);
    TEST_ASSERT_EQUAL(1, mock_rs485_stats_reset_called[0]);
}

void test_deinit_passive(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);

    mock_bridge_reset();
    mock_sniffer_reset();
    mock_serial_reset();
    mock_rs485_stats_reset_all();

    esp_err_t ret = port_manager_set_mode(0, PM_MODE_DISABLED);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    TEST_ASSERT_EQUAL(1, mock_sniffer_detach_called[0]);
    /* serial_deinit must have been called (tracked in slot 0 by mock) */
    TEST_ASSERT_EQUAL(1, mock_serial_deinit_called[0]);
    TEST_ASSERT_EQUAL(1, mock_rs485_busy_monitor_reset_called[0]);
    TEST_ASSERT_EQUAL(1, mock_rs485_stats_reset_called[0]);
}

void test_deinit_cache_overlay_last_port(void)
{
    /* Pool lifetime now follows persisted intent, not transport state. Switching
     * the sole caching port to DISABLED keeps its overlay set, so the pool (and
     * its accumulated data) must be PRESERVED — disable() must NOT be called. */
    port_manager_set_cache(0, true);
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);

    mock_cache_multimaster_reset();
    mock_cache_multimaster_enabled = true; /* restore live state after counter reset */
    mock_sniffer_reset();

    port_manager_set_mode(0, PM_MODE_DISABLED);

    /* Overlay still set → pool intent unchanged → no disable, pool stays alive. */
    TEST_ASSERT_EQUAL(0, mock_cache_multimaster_disable_called);
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);
    /* The overlay setting itself must survive the transport-mode change. */
    TEST_ASSERT_TRUE(port_manager_get_cache(0));
}

void test_deinit_cache_overlay_not_last_port(void)
{
    /* Both ports feed the cache. */
    port_manager_set_cache(0, true);
    port_manager_set_cache(1, true);
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    port_manager_set_mode(1, PM_MODE_PASSIVE);

    mock_cache_multimaster_reset();
    mock_cache_multimaster_enabled = true;
    mock_sniffer_reset();

    /* Deinit only port 0 — port 1 still feeds the cache. */
    port_manager_set_mode(0, PM_MODE_DISABLED);

    /* Not the last cache-feeding port → disable must NOT be called. */
    TEST_ASSERT_EQUAL(0, mock_cache_multimaster_disable_called);
}

void test_cache_overlay_survives_transport_change(void)
{
    /* Enable cache overlay on a PASSIVE port, then switch transport to TCP_BRIDGE. */
    port_manager_set_cache(0, true);
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_TRUE(port_manager_get_cache(0));

    port_manager_set_mode(0, PM_MODE_TCP_BRIDGE);
    /* Overlay persists and the cache is re-applied on the new transport. */
    TEST_ASSERT_TRUE(port_manager_get_cache(0));
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);
}

void test_set_cache_invalid_port(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, port_manager_set_cache(BRIDGES_COUNT, true));
}

void test_set_cache_disable_clears_reason_and_disables_cache(void)
{
    port_manager_set_cache(0, true);
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);

    mock_sniffer_reset();
    mock_cache_multimaster_reset();
    mock_cache_multimaster_enabled = true;

    /* Disabling the overlay on the only cache-feeding port disables the global cache. */
    esp_err_t ret = port_manager_set_cache(0, false);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(port_manager_get_cache(0));
    TEST_ASSERT_EQUAL(1, mock_sniffer_disable_called[0]);
    TEST_ASSERT_EQUAL(SNIFF_REASON_CACHE, mock_sniffer_disable_last_reason[0]);
    TEST_ASSERT_EQUAL(1, mock_cache_multimaster_disable_called);
}

void test_get_cache_invalid_port(void)
{
    TEST_ASSERT_FALSE(port_manager_get_cache(BRIDGES_COUNT));
}

/* Problem 2 regression: changing serial params on the SOLE caching port does a
 * deinit+init on that port. While the overlay stays set, the global pool — and
 * its accumulated data — must survive the re-init. disable() must NOT be called. */
void test_apply_settings_preserves_cache_on_same_port_reinit(void)
{
    port_manager_set_cache(0, true);
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);

    mock_cache_multimaster_reset();
    mock_cache_multimaster_enabled = true; /* restore live state after counter reset */
    mock_sniffer_reset();

    /* Re-apply settings (same mode, only serial params would have changed). */
    esp_err_t ret = port_manager_apply_settings(0);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Overlay unchanged → pool intent unchanged → no disable, no wipe. */
    TEST_ASSERT_EQUAL(0, mock_cache_multimaster_disable_called);
    TEST_ASSERT_EQUAL(0, mock_cache_multimaster_enable_called);
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);
    TEST_ASSERT_TRUE(port_manager_get_cache(0));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 6. Mode switching sequences
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_switch_from_passive_to_tcp_bridge(void)
{
    /* Start in PASSIVE */
    port_manager_set_mode(0, PM_MODE_PASSIVE);

    /* Reset counts so only transition effects are measured */
    mock_bridge_reset();
    mock_sniffer_reset();
    mock_serial_reset();

    /* Switch to TCP_BRIDGE — should deinit sniffer then init bridge */
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_TCP_BRIDGE);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Deinit: sniffer_detach + serial_deinit */
    TEST_ASSERT_EQUAL(1, mock_sniffer_detach_called[0]);
    TEST_ASSERT_EQUAL(1, mock_serial_deinit_called[0]);

    /* Init: bridge_port_init */
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_port_init_called);

    TEST_ASSERT_EQUAL(PM_MODE_TCP_BRIDGE, port_manager_get_mode(0));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 6b. Legacy NVS migration
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_migrate_legacy_sniffer_to_passive(void)
{
    /* Simulate a deployed device that stored the legacy "sniffer" mode. */
    mock_setting_items_set_port_mode(0, "sniffer");

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());

    /* Stored value rewritten to "passive"; transport is PASSIVE; no cache. */
    TEST_ASSERT_EQUAL_STRING(PORT_MODE_PASSIVE_STR, mock_setting_items_get_port_mode(0));
    TEST_ASSERT_EQUAL(PM_MODE_PASSIVE, port_manager_get_mode(0));
    TEST_ASSERT_FALSE(port_manager_get_cache(0));
}

void test_migrate_legacy_cache_bus_to_passive_plus_cache(void)
{
    /* Simulate a deployed device that stored the legacy "cache_bus" mode. */
    mock_setting_items_set_port_mode(0, "cache_bus");

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());

    /* Stored value rewritten to "passive"; transport is PASSIVE; cache overlay on. */
    TEST_ASSERT_EQUAL_STRING(PORT_MODE_PASSIVE_STR, mock_setting_items_get_port_mode(0));
    TEST_ASSERT_EQUAL(PM_MODE_PASSIVE, port_manager_get_mode(0));
    TEST_ASSERT_TRUE(port_manager_get_cache(0));
    TEST_ASSERT_TRUE(mock_cache_en[0]); /* persisted */
}

void test_switch_from_tcp_bridge_to_disabled(void)
{
    port_manager_set_mode(0, PM_MODE_TCP_BRIDGE);

    mock_bridge_reset();
    mock_sniffer_reset();

    esp_err_t ret = port_manager_set_mode(0, PM_MODE_DISABLED);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Deinit TCP_BRIDGE: sniffer_detach + bridge_port_deinit */
    TEST_ASSERT_EQUAL(1, mock_sniffer_detach_called[0]);
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_port_deinit_called);

    /* No new init calls for DISABLED */
    TEST_ASSERT_EQUAL(0, mock_bridge_calls[0].bridge_port_init_called);

    TEST_ASSERT_EQUAL(PM_MODE_DISABLED, port_manager_get_mode(0));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 7. hex_str_to_bytes
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_hex_str_to_bytes_valid(void)
{
    uint8_t out[16];
    int n = hex_str_to_bytes("0102FF", out, sizeof(out));
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[2]);
}

void test_hex_str_to_bytes_odd_length(void)
{
    uint8_t out[16];
    TEST_ASSERT_EQUAL(-1, hex_str_to_bytes("010", out, sizeof(out)));
}

void test_hex_str_to_bytes_non_hex(void)
{
    uint8_t out[16];
    TEST_ASSERT_EQUAL(-1, hex_str_to_bytes("0G", out, sizeof(out)));
}

void test_hex_str_to_bytes_empty(void)
{
    uint8_t out[16];
    TEST_ASSERT_EQUAL(0, hex_str_to_bytes("", out, sizeof(out)));
}

void test_hex_str_to_bytes_at_out_max(void)
{
    uint8_t out[3];
    int n = hex_str_to_bytes("AABBCC", out, 3);
    TEST_ASSERT_EQUAL(3, n);
}

void test_hex_str_to_bytes_exceeds_out_max(void)
{
    uint8_t out[2];
    /* 3 bytes → 6 hex chars → exceeds out_max=2 */
    TEST_ASSERT_EQUAL(-1, hex_str_to_bytes("AABBCC", out, 2));
}

void test_hex_str_to_bytes_uppercase_and_lowercase(void)
{
    uint8_t out_upper[2], out_lower[2];
    hex_str_to_bytes("AABB", out_upper, sizeof(out_upper));
    hex_str_to_bytes("aabb", out_lower, sizeof(out_lower));
    TEST_ASSERT_EQUAL_HEX8(out_upper[0], out_lower[0]);
    TEST_ASSERT_EQUAL_HEX8(out_upper[1], out_lower[1]);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 8. port_manager_send_raw
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_send_raw_invalid_port(void)
{
    uint8_t data[] = {0x01};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, port_manager_send_raw(BRIDGES_COUNT, data, 1));
    TEST_ASSERT_EQUAL(0, mock_serial_send_called);
}

void test_send_raw_disabled_port_no_serial_desc(void)
{
    /* Port stays DISABLED — no serial_desc, should return ESP_FAIL */
    uint8_t data[] = {0x01};
    TEST_ASSERT_EQUAL(ESP_FAIL, port_manager_send_raw(0, data, 1));
    TEST_ASSERT_EQUAL(0, mock_serial_send_called);
}

void test_send_raw_sniffer_port_calls_serial_send(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    mock_serial_reset(); /* reset after set_mode to isolate */
    mock_sniffer_reset();

    uint8_t data[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
    esp_err_t ret = port_manager_send_raw(0, data, sizeof(data));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, mock_serial_send_called);
    TEST_ASSERT_EQUAL(sizeof(data), mock_serial_send_last_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, mock_serial_send_last_data, sizeof(data));
    /* TX visibility for the sniffer is now fed inside serial_send() (verified in the
     * serial unit suite), NOT re-injected here — so port_manager_send_raw must NOT
     * call sniffer_inject_tx (R5: no double-feed). */
    TEST_ASSERT_EQUAL(0, mock_sniffer_inject_tx_called[0]);
}

void test_send_raw_tx_disabled_no_sniffer_inject(void)
{
    /* With TX disabled, send_raw still delegates to serial_send (which drops the bytes
     * internally and, in the real serial layer, skips the TX sniffer feed). send_raw
     * itself never calls sniffer_inject_tx — TX visibility moved into serial_send. */
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    port_manager_set_tx_disabled(0, true);
    mock_serial_reset();
    mock_sniffer_reset();

    uint8_t data[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
    esp_err_t ret = port_manager_send_raw(0, data, sizeof(data));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    /* serial_send was called (but silently dropped bytes inside the real serial layer) */
    TEST_ASSERT_EQUAL(1, mock_serial_send_called);
    /* sniffer_inject_tx must NOT have been called by port_manager */
    TEST_ASSERT_EQUAL(0, mock_sniffer_inject_tx_called[0]);
}

void test_send_raw_serial_send_failure_no_sniffer_inject(void)
{
    /* When serial_send() fails the error is propagated to the caller. The TX sniffer
     * feed lives inside serial_send (skipped on failure there); port_manager_send_raw
     * never calls sniffer_inject_tx regardless of the serial_send result. */
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    mock_serial_reset();
    mock_sniffer_reset();
    mock_serial_send_ret = ESP_FAIL;

    uint8_t data[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
    esp_err_t ret = port_manager_send_raw(0, data, sizeof(data));
    /* The failure must be propagated to the caller */
    TEST_ASSERT_EQUAL(ESP_FAIL, ret);
    /* serial_send was attempted */
    TEST_ASSERT_EQUAL(1, mock_serial_send_called);
    /* sniffer_inject_tx must NOT have been called by port_manager */
    TEST_ASSERT_EQUAL(0, mock_sniffer_inject_tx_called[0]);
}

void test_send_raw_tcp_bridge_port_calls_serial_send(void)
{
    port_manager_set_mode(0, PM_MODE_TCP_BRIDGE);
    mock_serial_reset();

    uint8_t data[] = {0xAA, 0xBB};
    esp_err_t ret = port_manager_send_raw(0, data, sizeof(data));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, mock_serial_send_called);
    TEST_ASSERT_EQUAL(2u, mock_serial_send_last_len);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 9. port_send_handler integration (via hex_str_to_bytes + port_manager_send_raw)
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_port_send_handler_valid_hex(void)
{
    /* Inject a valid 8-byte FC03 request */
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    mock_serial_reset();
    mock_json_utils_reset();

    /* CRC of [01 03 00 00 00 02] = C4 0B (lo=0xC4, hi=0x0B) */
    const char *hex = "010300000002C40B";
    uint8_t decoded[8];
    int n = hex_str_to_bytes(hex, decoded, sizeof(decoded));
    TEST_ASSERT_EQUAL(8, n);
    esp_err_t ret = port_manager_send_raw(0, decoded, (size_t)n);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, mock_serial_send_called);
    TEST_ASSERT_EQUAL(8u, mock_serial_send_last_len);
    TEST_ASSERT_EQUAL_HEX8(0x01, mock_serial_send_last_data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03, mock_serial_send_last_data[1]);
}

void test_port_send_handler_odd_length_rejected(void)
{
    uint8_t out[16];
    TEST_ASSERT_EQUAL(-1, hex_str_to_bytes("010", out, sizeof(out)));
}

void test_port_send_handler_nonhex_rejected(void)
{
    uint8_t out[16];
    TEST_ASSERT_EQUAL(-1, hex_str_to_bytes("ZZZZ", out, sizeof(out)));
}

void test_port_send_handler_too_long_rejected(void)
{
    /* Build a hex string longer than out_max: 129 bytes = 258 hex chars; out_max=128 → -1 */
    uint8_t out[128];
    char hex[259];
    for (int i = 0; i < 258; i++) hex[i] = 'A';
    hex[258] = '\0';
    TEST_ASSERT_EQUAL(-1, hex_str_to_bytes(hex, out, sizeof(out)));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Unity runner
 * ═══════════════════════════════════════════════════════════════════════════ */

int port_manager_test(void)
{
    UNITY_BEGIN();

    /* 1 – mode_to_str */
    RUN_TEST(test_mode_to_str_disabled);
    RUN_TEST(test_mode_to_str_tcp_bridge);
    RUN_TEST(test_mode_to_str_passive);
    RUN_TEST(test_mode_to_str_unknown);

    /* 2 – get_mode */
    RUN_TEST(test_get_mode_initial_disabled);
    RUN_TEST(test_get_mode_invalid_index);
    RUN_TEST(test_get_mode_after_set_mode);

    /* 3 – set_mode error paths */
    RUN_TEST(test_set_mode_invalid_index);
    RUN_TEST(test_set_mode_tcp_bridge_init_fail);
    RUN_TEST(test_set_mode_passive_serial_fail);

    /* 4 – port_init_mode for each mode + cache overlay */
    RUN_TEST(test_set_mode_disabled);
    RUN_TEST(test_set_mode_tcp_bridge_success);
    RUN_TEST(test_set_mode_passive_success);
    RUN_TEST(test_set_mode_passive_with_cache_overlay_enables_cache);
    RUN_TEST(test_set_mode_tcp_bridge_with_cache_overlay_enables_cache);

    /* 5 – port_deinit_mode + cache overlay control */
    RUN_TEST(test_deinit_tcp_bridge);
    RUN_TEST(test_deinit_passive);
    RUN_TEST(test_deinit_cache_overlay_last_port);
    RUN_TEST(test_deinit_cache_overlay_not_last_port);
    RUN_TEST(test_cache_overlay_survives_transport_change);
    RUN_TEST(test_set_cache_invalid_port);
    RUN_TEST(test_set_cache_disable_clears_reason_and_disables_cache);
    RUN_TEST(test_get_cache_invalid_port);
    RUN_TEST(test_apply_settings_preserves_cache_on_same_port_reinit);

    /* 6 – mode switching sequences */
    RUN_TEST(test_switch_from_passive_to_tcp_bridge);
    RUN_TEST(test_switch_from_tcp_bridge_to_disabled);

    /* 6b – legacy NVS migration */
    RUN_TEST(test_migrate_legacy_sniffer_to_passive);
    RUN_TEST(test_migrate_legacy_cache_bus_to_passive_plus_cache);

    /* 7 – hex_str_to_bytes */
    RUN_TEST(test_hex_str_to_bytes_valid);
    RUN_TEST(test_hex_str_to_bytes_odd_length);
    RUN_TEST(test_hex_str_to_bytes_non_hex);
    RUN_TEST(test_hex_str_to_bytes_empty);
    RUN_TEST(test_hex_str_to_bytes_at_out_max);
    RUN_TEST(test_hex_str_to_bytes_exceeds_out_max);
    RUN_TEST(test_hex_str_to_bytes_uppercase_and_lowercase);

    /* 8 – port_manager_send_raw */
    RUN_TEST(test_send_raw_invalid_port);
    RUN_TEST(test_send_raw_disabled_port_no_serial_desc);
    RUN_TEST(test_send_raw_sniffer_port_calls_serial_send);
    RUN_TEST(test_send_raw_tx_disabled_no_sniffer_inject);
    RUN_TEST(test_send_raw_serial_send_failure_no_sniffer_inject);
    RUN_TEST(test_send_raw_tcp_bridge_port_calls_serial_send);

    /* 9 – port_send_handler integration */
    RUN_TEST(test_port_send_handler_valid_hex);
    RUN_TEST(test_port_send_handler_odd_length_rejected);
    RUN_TEST(test_port_send_handler_nonhex_rejected);
    RUN_TEST(test_port_send_handler_too_long_rejected);

    return UNITY_END();
}

int main(void)
{
    return port_manager_test();
}
