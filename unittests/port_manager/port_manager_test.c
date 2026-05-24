#include "unity.h"

#include "port_manager.h"
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
extern bool mock_sniffer_set_cache_active_value;
extern int mock_sniffer_set_cache_active_called;
void mock_sniffer_reset(void);

/* cache_multimaster.c mock */
extern int mock_cache_multimaster_init_called;
extern int mock_cache_multimaster_enable_called;
extern int mock_cache_multimaster_disable_called;
void mock_cache_multimaster_reset(void);

/* cache_modbus_server.c mock */
extern int mock_cache_modbus_server_init_called;
void mock_cache_modbus_server_reset(void);

/* serial.c mock */
extern int mock_serial_deinit_called[BRIDGES_COUNT];
extern int mock_serial_set_rx_timeout_called[BRIDGES_COUNT];
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

void test_mode_to_str_sniffer(void)
{
    TEST_ASSERT_EQUAL_STRING(PORT_MODE_SNIFFER_STR, port_manager_mode_to_str(PM_MODE_SNIFFER));
}

void test_mode_to_str_cache_bus(void)
{
    TEST_ASSERT_EQUAL_STRING(PORT_MODE_CACHE_BUS_STR, port_manager_mode_to_str(PM_MODE_CACHE_BUS));
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
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_SNIFFER);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PM_MODE_SNIFFER, port_manager_get_mode(0));
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

void test_set_mode_sniffer_serial_fail(void)
{
    mock_bridge_port_init_serial_only_should_fail = true;
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_SNIFFER);
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
}

void test_set_mode_sniffer_success(void)
{
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_SNIFFER);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PM_MODE_SNIFFER, port_manager_get_mode(0));
    /* serial-only init called */
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_port_init_serial_only_called);
    /* sniffer attached and enabled */
    TEST_ASSERT_EQUAL(1, mock_sniffer_attach_called[0]);
    TEST_ASSERT_EQUAL(1, mock_sniffer_enable_called[0]);
    /* rx timeout was set */
    TEST_ASSERT_EQUAL(1, mock_serial_set_rx_timeout_called[0]);
    /* cache not enabled for sniffer mode */
    TEST_ASSERT_EQUAL(0, mock_cache_multimaster_enable_called);
}

void test_set_mode_cache_bus_success(void)
{
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_CACHE_BUS);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PM_MODE_CACHE_BUS, port_manager_get_mode(0));
    /* serial-only init called */
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_port_init_serial_only_called);
    /* sniffer attached and enabled */
    TEST_ASSERT_EQUAL(1, mock_sniffer_attach_called[0]);
    TEST_ASSERT_EQUAL(1, mock_sniffer_enable_called[0]);
    /* cache enabled */
    TEST_ASSERT_EQUAL(1, mock_cache_multimaster_enable_called);
    /* sniffer_set_cache_active(true) called */
    TEST_ASSERT_EQUAL(true, mock_sniffer_set_cache_active_value);
    TEST_ASSERT_EQUAL(1, mock_sniffer_set_cache_active_called);
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

void test_deinit_sniffer(void)
{
    port_manager_set_mode(0, PM_MODE_SNIFFER);

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

void test_deinit_cache_bus_last_port(void)
{
    /* Only port 0 in CACHE_BUS — deinit should disable global cache */
    port_manager_set_mode(0, PM_MODE_CACHE_BUS);

    mock_cache_multimaster_reset();
    mock_sniffer_reset();

    port_manager_set_mode(0, PM_MODE_DISABLED);

    /* Last CACHE_BUS port → cache_multimaster_disable must be called */
    TEST_ASSERT_EQUAL(1, mock_cache_multimaster_disable_called);
    /* sniffer_set_cache_active(false) called */
    TEST_ASSERT_EQUAL(false, mock_sniffer_set_cache_active_value);
    TEST_ASSERT_EQUAL(1, mock_sniffer_set_cache_active_called);
}

void test_deinit_cache_bus_not_last_port(void)
{
    /* Both ports in CACHE_BUS */
    port_manager_set_mode(0, PM_MODE_CACHE_BUS);
    port_manager_set_mode(1, PM_MODE_CACHE_BUS);

    mock_cache_multimaster_reset();
    mock_sniffer_reset();

    /* Deinit only port 0 — port 1 still in CACHE_BUS */
    port_manager_set_mode(0, PM_MODE_DISABLED);

    /* Not the last CACHE_BUS port → disable must NOT be called */
    TEST_ASSERT_EQUAL(0, mock_cache_multimaster_disable_called);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 6. Mode switching sequences
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_switch_from_sniffer_to_tcp_bridge(void)
{
    /* Start in SNIFFER */
    port_manager_set_mode(0, PM_MODE_SNIFFER);

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
 * Unity runner
 * ═══════════════════════════════════════════════════════════════════════════ */

int port_manager_test(void)
{
    UNITY_BEGIN();

    /* 1 – mode_to_str */
    RUN_TEST(test_mode_to_str_disabled);
    RUN_TEST(test_mode_to_str_tcp_bridge);
    RUN_TEST(test_mode_to_str_sniffer);
    RUN_TEST(test_mode_to_str_cache_bus);
    RUN_TEST(test_mode_to_str_unknown);

    /* 2 – get_mode */
    RUN_TEST(test_get_mode_initial_disabled);
    RUN_TEST(test_get_mode_invalid_index);
    RUN_TEST(test_get_mode_after_set_mode);

    /* 3 – set_mode error paths */
    RUN_TEST(test_set_mode_invalid_index);
    RUN_TEST(test_set_mode_tcp_bridge_init_fail);
    RUN_TEST(test_set_mode_sniffer_serial_fail);

    /* 4 – port_init_mode for each mode */
    RUN_TEST(test_set_mode_disabled);
    RUN_TEST(test_set_mode_tcp_bridge_success);
    RUN_TEST(test_set_mode_sniffer_success);
    RUN_TEST(test_set_mode_cache_bus_success);

    /* 5 – port_deinit_mode for each mode */
    RUN_TEST(test_deinit_tcp_bridge);
    RUN_TEST(test_deinit_sniffer);
    RUN_TEST(test_deinit_cache_bus_last_port);
    RUN_TEST(test_deinit_cache_bus_not_last_port);

    /* 6 – mode switching sequences */
    RUN_TEST(test_switch_from_sniffer_to_tcp_bridge);
    RUN_TEST(test_switch_from_tcp_bridge_to_disabled);

    return UNITY_END();
}

int main(void)
{
    return port_manager_test();
}
