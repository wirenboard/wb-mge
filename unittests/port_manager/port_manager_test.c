#include "unity.h"

#include "port_manager.h"
#include "sniffer.h"
#include "setting_items.h"
#include "bridge_mock.h"
#include "repeater_mock.h"
#include "esp_http_server.h"      /* httpd_req_t, for the REST handler tests */
#include "freertos/semphr.h"      /* mock_xSemaphoreTake_hook, for the pm_lock race tests */

#include <stdio.h>                /* main.c boot-order check reads the source file */
#include <string.h>

/* Statics that port_manager.c exposes to the tests via PORT_MANAGER_STATIC */
int hex_str_to_bytes(const char *hex, uint8_t *out, size_t out_max);
esp_err_t port_set_mode_handler(httpd_req_t *req, unsigned port_index);

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
extern bool mock_sniffer_init_should_fail;
void mock_sniffer_reset(void);

/* cache_multimaster.c mock */
extern int mock_cache_multimaster_init_called;
extern int mock_cache_multimaster_enable_called;
extern int mock_cache_multimaster_disable_called;
extern bool mock_cache_multimaster_enabled;
extern bool mock_cache_multimaster_init_should_fail;
extern bool mock_cache_en[];  /* per-port cache_en NVS store (mocks/setting_items.c) */
void mock_cache_multimaster_reset(void);

/* setting_items.c mock helper */
void mock_setting_items_set_port_mode(unsigned index, const char *value);
const char *mock_setting_items_get_port_mode(unsigned index);

/* cache_modbus_server.c mock */
extern int mock_cache_modbus_server_init_called;
extern bool mock_cache_modbus_server_init_should_fail;
void mock_cache_modbus_server_reset(void);

/* serial.c mock */
extern int mock_serial_deinit_called[BRIDGES_COUNT];
extern int mock_serial_set_rx_timeout_called[BRIDGES_COUNT];
extern uint8_t mock_serial_set_rx_timeout_value[BRIDGES_COUNT];
extern int mock_serial_send_called;
extern uint8_t mock_serial_send_last_data[];
extern size_t mock_serial_send_last_len;
void mock_serial_reset(void);

/* setting_items.c mock */
extern bool mock_cache_server_enabled;
extern int mock_cache_port;
extern int mock_setting_items_save_called;
extern bool mock_setting_items_save_should_fail;
extern bool mock_setting_items_save_bool_should_fail;
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
extern const char *mock_json_utils_send_error_last_status;
extern int mock_json_utils_send_response_called;
void mock_json_utils_reset(void);
void mock_json_utils_inject_hex(const char *hex);
void mock_json_utils_inject_mode(const char *mode);

/* settings_manager.c mock — injectable result of the port-mode collision pre-check.
 * Defaults to ESP_OK; a test sets it to drive the handler's 409-collision branch. */
extern esp_err_t g_mock_port_mode_collision_ret;

/* repeater.c mock state is declared in repeater_mock.h */

/* ── setUp / tearDown ───────────────────────────────────────────────────── */

void setUp(void)
{
    mock_xSemaphoreTake_hook = NULL;
    port_manager_reset_for_test();
    mock_bridge_reset();
    mock_sniffer_reset();
    mock_cache_multimaster_reset();
    mock_cache_modbus_server_reset();
    mock_serial_reset();
    mock_setting_items_reset();
    mock_rs485_stats_reset_all();
    mock_json_utils_reset();
    mock_repeater_reset();
    g_mock_port_mode_collision_ret = ESP_OK;   // no collision unless a test opts in
}

void tearDown(void)
{
    g_mock_port_mode_collision_ret = ESP_OK;
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

void test_mode_to_str_repeater(void)
{
    TEST_ASSERT_EQUAL_STRING(PORT_MODE_REPEATER_STR, port_manager_mode_to_str(PM_MODE_REPEATER));
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

    /* persist-2: a failed init must NOT have persisted the new mode to NVS.
     * Initial NVS mode is "disabled"; it must stay "disabled", runtime must be
     * DISABLED, and there must be NO settings mismatch (otherwise
     * settings_update_task would re-apply and re-fail on every settings POST). */
    TEST_ASSERT_EQUAL_STRING_MESSAGE(PORT_MODE_DISABLED_STR,
        mock_setting_items_get_port_mode(0),
        "failed set_mode must not persist the new mode to NVS");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_setting_items_save_called,
        "failed set_mode must not call setting_items_save at all");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(0),
        "runtime mode must remain DISABLED after a failed init");
    TEST_ASSERT_FALSE_MESSAGE(port_manager_check_settings_changed(0),
        "NVS and runtime must agree after a failed set_mode (no permanent re-apply loop)");
}

/* persist-2: failing a mode change FROM a working mode must not corrupt NVS.
 * NVS keeps the previous (working) mode so the next apply converges back to it. */
void test_set_mode_init_fail_keeps_previous_nvs_mode(void)
{
    /* Start from a working PASSIVE mode (NVS = passive, runtime = PASSIVE). */
    esp_err_t ok = port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, ok);
    TEST_ASSERT_EQUAL_STRING(PORT_MODE_PASSIVE_STR, mock_setting_items_get_port_mode(0));

    /* Now attempt a switch to TCP_BRIDGE that fails at init. */
    mock_bridge_port_init_should_fail = true;
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_TCP_BRIDGE);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);

    /* NVS must still hold the previous working mode, not the failed target. */
    TEST_ASSERT_EQUAL_STRING_MESSAGE(PORT_MODE_PASSIVE_STR,
        mock_setting_items_get_port_mode(0),
        "failed switch must leave the previous mode in NVS");

    /* And the runtime must self-heal back to the previous working mode
     * immediately (rollback), not be left DISABLED, so NVS and runtime agree. */
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "failed switch must roll the runtime back to the previous working mode");
    TEST_ASSERT_FALSE_MESSAGE(port_manager_check_settings_changed(0),
        "after rollback NVS and runtime must agree (no re-apply loop)");
}

void test_set_mode_passive_serial_fail(void)
{
    mock_bridge_port_init_serial_only_should_fail = true;
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_port_init_serial_only_called);
}

/* persist-2 + rollback double-failure: switching to a new mode whose init fails
 * AND whose rollback to the previous mode ALSO fails must leave the port DISABLED,
 * keep NVS at the previous mode (no save happens on a failed init), and return the
 * (error) status. Drives both legs by failing two distinct bridge init routes:
 *   - new mode  PM_MODE_TCP_BRIDGE → bridge_port_init()             (fails)
 *   - prev mode PM_MODE_PASSIVE    → bridge_port_init_serial_only() (fails on rollback)
 * (port_manager.c set_mode rollback ~482-493.) */
void test_set_mode_double_init_failure_leaves_disabled(void)
{
    /* Establish a working PASSIVE mode first (NVS = passive, runtime = PASSIVE). */
    esp_err_t ok = port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, ok);
    TEST_ASSERT_EQUAL(PM_MODE_PASSIVE, port_manager_get_mode(0));
    TEST_ASSERT_EQUAL_STRING(PORT_MODE_PASSIVE_STR, mock_setting_items_get_port_mode(0));

    /* Snapshot counters so we can assert the failed switch in isolation
     * (the setup set_mode above already incremented serial_only init once). */
    int saves_before              = mock_setting_items_save_called;
    int bridge_init_before        = mock_bridge_calls[0].bridge_port_init_called;
    int serial_only_init_before   = mock_bridge_calls[0].bridge_port_init_serial_only_called;

    /* Fail BOTH init routes: the new TCP_BRIDGE init and the PASSIVE rollback. */
    mock_bridge_port_init_should_fail             = true;  /* kills TCP_BRIDGE init  */
    mock_bridge_port_init_serial_only_should_fail = true;  /* kills PASSIVE rollback */

    esp_err_t ret = port_manager_set_mode(0, PM_MODE_TCP_BRIDGE);

    /* set_mode must surface the (error) status from the failed new-mode init. */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, ret,
        "a double-failure set_mode must return the failed-init error status");

    /* Both legs must actually have been attempted exactly once during the switch. */
    TEST_ASSERT_EQUAL_MESSAGE(bridge_init_before + 1,
        mock_bridge_calls[0].bridge_port_init_called,
        "the new mode's init (bridge_port_init) must have been attempted");
    TEST_ASSERT_EQUAL_MESSAGE(serial_only_init_before + 1,
        mock_bridge_calls[0].bridge_port_init_serial_only_called,
        "the rollback to the previous mode (bridge_port_init_serial_only) must have been attempted");

    /* With both inits failed, the port is left DISABLED (port_init_mode leaves the
     * mode unset on failure, and the failed rollback cannot restore PASSIVE). */
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(0),
        "after a double init failure the port must end DISABLED");

    /* persist-2: NVS is only written after a successful init, so it must still hold
     * the previous mode — never the failed new mode. */
    TEST_ASSERT_EQUAL_STRING_MESSAGE(PORT_MODE_PASSIVE_STR,
        mock_setting_items_get_port_mode(0),
        "NVS must keep the previous mode, not the failed new mode");
    TEST_ASSERT_EQUAL_MESSAGE(saves_before, mock_setting_items_save_called,
        "a failed set_mode must not write the port mode to NVS");
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

void test_set_mode_repeater_success(void)
{
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_REPEATER);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PM_MODE_REPEATER, port_manager_get_mode(0));
    /* repeater_init_port called for this port */
    TEST_ASSERT_EQUAL(1, mock_repeater_calls[0].init_called);
    /* sniffer attached (orthogonal overlay), same as PASSIVE/TCP_BRIDGE */
    TEST_ASSERT_EQUAL(1, mock_sniffer_attach_called[0]);
    /* Repeater owns the RX timeout: it sets the PROXY inter-frame timeout once. */
    TEST_ASSERT_EQUAL(1, mock_serial_set_rx_timeout_called[0]);
    TEST_ASSERT_EQUAL(SERIAL_RX_TOUT_PROXY, mock_serial_set_rx_timeout_value[0]);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 4b. port_manager_set_mode_transient() — runtime-only, never persisted
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_set_mode_transient_invalid_index(void)
{
    esp_err_t ret = port_manager_set_mode_transient(BRIDGES_COUNT, PM_MODE_DISABLED);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

void test_set_mode_transient_applies_live_but_does_not_persist(void)
{
    /* Configured (persisted) mode is PASSIVE. */
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));
    TEST_ASSERT_EQUAL(PM_MODE_PASSIVE, port_manager_get_mode(0));

    int saves_before = mock_setting_items_save_called;

    /* The factory test disables the port transiently. */
    esp_err_t ret = port_manager_set_mode_transient(0, PM_MODE_DISABLED);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Live mode changed... */
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(0),
        "transient set_mode must apply the new mode at runtime");
    /* ...but NVS was not touched at all: a power loss during the test must not
     * wipe the configured port mode. */
    TEST_ASSERT_EQUAL_MESSAGE(saves_before, mock_setting_items_save_called,
        "transient set_mode must not call setting_items_save");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(PORT_MODE_PASSIVE_STR,
        mock_setting_items_get_port_mode(0),
        "transient set_mode must leave the persisted port mode untouched");
}

void test_set_mode_transient_restored_from_nvs_on_exit(void)
{
    /* Full factory-test cycle: PASSIVE -> transient DISABLED -> restore. */
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode_transient(0, PM_MODE_DISABLED));
    TEST_ASSERT_EQUAL(PM_MODE_DISABLED, port_manager_get_mode(0));

    /* Exit path used by wb_test: re-read the mode from NVS and re-init the port. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "exiting the test must restore the mode persisted in NVS");
}

void test_set_mode_transient_init_fail_rolls_back(void)
{
    /* Start from a working PASSIVE port. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode(0, PM_MODE_PASSIVE));
    int saves_before = mock_setting_items_save_called;

    /* A transient switch whose init fails must roll the runtime back and still
     * leave NVS alone. */
    mock_bridge_port_init_should_fail = true;
    esp_err_t ret = port_manager_set_mode_transient(0, PM_MODE_TCP_BRIDGE);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    mock_bridge_port_init_should_fail = false;

    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "failed transient switch must roll back to the previous mode");
    TEST_ASSERT_EQUAL_MESSAGE(saves_before, mock_setting_items_save_called,
        "failed transient switch must not call setting_items_save");
}

/* ── Factory-test port freeze (clock_out) ──────────────────────────────────
 *
 * During the 100 kHz clock_out test both ports are transiently DISABLED while
 * the LEDC drives their TX/DE pins, but NVS still holds the user's mode. The
 * freeze flag must stop that runtime/NVS mismatch from letting an unrelated
 * POST /settings re-init the ports on top of the running waveform.
 */

/* Put port 0 in the state the factory test leaves it in: configured PASSIVE in
 * NVS, transiently DISABLED at runtime, ports frozen. */
static void enter_clock_out_test_on_port0(void)
{
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));
    TEST_ASSERT_EQUAL(PM_MODE_PASSIVE, port_manager_get_mode(0));

    port_manager_set_ports_frozen(true);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode_transient(0, PM_MODE_DISABLED));
    TEST_ASSERT_EQUAL(PM_MODE_DISABLED, port_manager_get_mode(0));
}

void test_frozen_check_settings_changed_reports_no_change(void)
{
    enter_clock_out_test_on_port0();

    /* Runtime mode is DISABLED while NVS says "passive" — without the freeze
     * this mismatch alone makes check_settings_changed() return true, which is
     * what dragged settings_update_task into re-initialising the port. */
    TEST_ASSERT_FALSE_MESSAGE(port_manager_check_settings_changed(0),
        "frozen ports must never report changed settings");
    TEST_ASSERT_FALSE_MESSAGE(port_manager_check_settings_changed(1),
        "frozen ports must never report changed settings");
}

void test_frozen_apply_settings_does_not_bring_port_up(void)
{
    enter_clock_out_test_on_port0();

    mock_bridge_reset();
    mock_serial_reset();

    /* This is what settings_update_task would do for a port it thinks changed —
     * e.g. a settings_update already in flight when the test started. */
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, port_manager_apply_settings(0),
        "apply_settings on a frozen port must succeed as a no-op");

    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(0),
        "apply_settings must not re-init a frozen port");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_bridge_calls[0].bridge_port_init_serial_only_called,
        "a frozen port must not re-open its serial port (LEDC owns the TX pin)");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_bridge_calls[0].bridge_port_init_called,
        "a frozen port must not re-init its bridge");
}

/* The release half of settings_update's two-phase apply must be frozen exactly like the apply
 * half. If it were not, a settings write landing during the factory test would tear the port down
 * — and apply_settings(), being a no-op while frozen, would not bring it back: the port would stay
 * dead until the test ended. */
void test_frozen_release_does_not_tear_port_down(void)
{
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));
    TEST_ASSERT_EQUAL(PM_MODE_PASSIVE, port_manager_get_mode(0));

    /* Freeze WITHOUT the transient disable: the port is still up and running when the
     * settings_update task reaches its release phase. */
    port_manager_set_ports_frozen(true);

    mock_bridge_reset();
    mock_serial_reset();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, port_manager_release(0),
        "release on a frozen port must succeed as a no-op");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "release must not tear a frozen port down (apply_settings would not bring it back)");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_deinit_called[0],
        "a frozen port must not have its serial closed (LEDC owns the TX pin)");

    port_manager_set_ports_frozen(false);
}

/* The normal, unfrozen path: release() really does tear the port down, so the acquire phase can
 * bind a socket another subsystem has just given up. */
void test_release_deinits_running_port(void)
{
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));
    TEST_ASSERT_EQUAL(PM_MODE_PASSIVE, port_manager_get_mode(0));

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_release(0));
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(0),
        "a released port is down until the acquire phase brings it back");

    /* Releasing an already-released port is a no-op, not an error. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_release(0));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, port_manager_release(BRIDGES_COUNT));
}

void test_frozen_ports_do_not_touch_nvs(void)
{
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));

    int saves_before = mock_setting_items_save_called;

    port_manager_set_ports_frozen(true);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode_transient(0, PM_MODE_DISABLED));
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));

    /* Losing power at any point of the test must leave the configured mode intact. */
    TEST_ASSERT_EQUAL_MESSAGE(saves_before, mock_setting_items_save_called,
        "the factory test must not write to NVS");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(PORT_MODE_PASSIVE_STR,
        mock_setting_items_get_port_mode(0),
        "the factory test must leave the persisted port mode untouched");
}

void test_unfreeze_restores_mode_from_nvs(void)
{
    enter_clock_out_test_on_port0();

    /* Exit path used by wb_test: unfreeze, then re-apply from NVS. */
    port_manager_set_ports_frozen(false);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));

    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "leaving the test must restore the mode persisted in NVS");
}

void test_unfreeze_picks_up_settings_written_during_test(void)
{
    enter_clock_out_test_on_port0();

    /* A POST /settings during the test persists a new mode but must not apply it. */
    mock_setting_items_set_port_mode(0, PORT_MODE_TCP_BRIDGE_STR);
    TEST_ASSERT_FALSE(port_manager_check_settings_changed(0));
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(0),
        "a settings write during the test must not raise the port");

    /* On exit the new mode is picked straight out of NVS. */
    port_manager_set_ports_frozen(false);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_TCP_BRIDGE, port_manager_get_mode(0),
        "settings written during the test must be applied when it ends");
}

void test_frozen_set_mode_rejected(void)
{
    enter_clock_out_test_on_port0();

    mock_bridge_reset();
    mock_serial_reset();
    int saves_before = mock_setting_items_save_called;

    /* POST /ports/1/mode during the test: re-initialising the port would take the
     * TX/DE pins back from the LEDC that is driving them. The refusal carries the
     * dedicated freeze code, not a generic ESP_ERR_INVALID_STATE. */
    TEST_ASSERT_EQUAL_MESSAGE(PM_ERR_PORTS_FROZEN,
        port_manager_set_mode(0, PM_MODE_TCP_BRIDGE),
        "a persisting set_mode must be refused while the ports are frozen");

    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(0),
        "a rejected set_mode must leave the frozen port DISABLED");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_bridge_calls[0].bridge_port_init_called,
        "a rejected set_mode must not re-init the port (LEDC owns the TX pin)");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_bridge_calls[0].bridge_port_init_serial_only_called,
        "a rejected set_mode must not re-open the serial port");
    TEST_ASSERT_EQUAL_MESSAGE(saves_before, mock_setting_items_save_called,
        "a rejected set_mode must not write the new mode to NVS");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(PORT_MODE_PASSIVE_STR,
        mock_setting_items_get_port_mode(0),
        "a rejected set_mode must leave the persisted mode untouched");
}

void test_frozen_set_mode_transient_still_allowed(void)
{
    /* The test itself drives the ports through the transient path, so the freeze
     * must not block it (that would make entering the test impossible). */
    enter_clock_out_test_on_port0();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK,
        port_manager_set_mode_transient(0, PM_MODE_DISABLED),
        "the transient path must stay open while the ports are frozen");
    TEST_ASSERT_EQUAL(PM_MODE_DISABLED, port_manager_get_mode(0));
}

void test_frozen_set_mode_handler_returns_409(void)
{
    enter_clock_out_test_on_port0();
    mock_json_utils_reset();

    httpd_req_t req = {0};
    mock_json_utils_inject_mode(PORT_MODE_TCP_BRIDGE_STR);

    TEST_ASSERT_EQUAL(ESP_OK, port_set_mode_handler(&req, 0));

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_json_utils_send_error_called,
        "POST /ports/1/mode during the factory test must fail");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("409 Conflict",
        mock_json_utils_send_error_last_status,
        "a mode change blocked by the factory test is a state conflict, not a bad request");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_json_utils_send_response_called,
        "a rejected mode change must not report success");
    TEST_ASSERT_EQUAL(PM_MODE_DISABLED, port_manager_get_mode(0));
}

void test_unfrozen_set_mode_handler_succeeds(void)
{
    /* Regression guard: the 409 path must not leak into normal operation. */
    TEST_ASSERT_FALSE(port_manager_ports_frozen());
    mock_json_utils_reset();

    httpd_req_t req = {0};
    mock_json_utils_inject_mode(PORT_MODE_PASSIVE_STR);

    TEST_ASSERT_EQUAL(ESP_OK, port_set_mode_handler(&req, 0));

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_json_utils_send_error_called,
        "a mode change on unfrozen ports must not error out");
    TEST_ASSERT_EQUAL(1, mock_json_utils_send_response_called);
    TEST_ASSERT_EQUAL(PM_MODE_PASSIVE, port_manager_get_mode(0));
}

/* T22: a switch whose new mode would collide with another local TCP listener (web server, cache
 * Modbus server, or the other RS-485 bridge) is rejected up front with 409, BEFORE the mode is
 * applied — settings_manager_check_port_mode_collision() (mocked here) returns ESP_ERR_INVALID_STATE.
 * The handler must answer 409 Conflict, must NOT report success, and must not reach the port init
 * path (the collision check runs before port_manager_set_mode, so req_json is released on the error
 * return and the live mode is left untouched). */
void test_set_mode_handler_collision_returns_409(void)
{
    TEST_ASSERT_FALSE(port_manager_ports_frozen());
    mock_json_utils_reset();

    /* Inject a collision for the requested switch to tcp_bridge. */
    g_mock_port_mode_collision_ret = ESP_ERR_INVALID_STATE;

    pm_mode_t mode_before = port_manager_get_mode(0);

    httpd_req_t req = {0};
    mock_json_utils_inject_mode(PORT_MODE_TCP_BRIDGE_STR);

    TEST_ASSERT_EQUAL(ESP_OK, port_set_mode_handler(&req, 0));

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_json_utils_send_error_called,
        "a mode change that would collide with another listener must fail");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("409 Conflict",
        mock_json_utils_send_error_last_status,
        "a port-collision rejection is a state conflict, not a bad request");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_json_utils_send_response_called,
        "a rejected mode change must not report success (error path deletes req_json)");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_bridge_calls[0].bridge_port_init_called,
        "a collision must be caught before the port init path runs");
    TEST_ASSERT_EQUAL_MESSAGE(mode_before, port_manager_get_mode(0),
        "a rejected mode change must leave the live mode untouched");
}

/* ESP_ERR_INVALID_STATE is NOT exclusive to the freeze: bridge_port_init() returns
 * exactly that code for a tcp_bridge whose persisted bridge_mode is invalid/legacy
 * (bridge.c), and it reaches the handler through port_init_mode(). On a device with a
 * corrupt bridge_mode in NVS, an ordinary POST /ports/1/mode must NOT be answered with
 * 409 "the clock_out factory test is active" — that is a false status and a false
 * diagnosis of a test that is not even running. */
void test_init_fail_invalid_state_is_not_reported_as_409(void)
{
    TEST_ASSERT_FALSE(port_manager_ports_frozen());
    mock_json_utils_reset();

    /* Corrupt/legacy bridge_mode: bridge_port_init() refuses with ESP_ERR_INVALID_STATE. */
    mock_bridge_port_init_should_fail = true;
    mock_bridge_port_init_fail_err = ESP_ERR_INVALID_STATE;

    httpd_req_t req = {0};
    mock_json_utils_inject_mode(PORT_MODE_TCP_BRIDGE_STR);

    TEST_ASSERT_EQUAL(ESP_OK, port_set_mode_handler(&req, 0));

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_json_utils_send_error_called,
        "a mode change whose init fails must be reported as an error");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0,
        strcmp(mock_json_utils_send_error_last_status, "409 Conflict"),
        "a failed init must not be reported as a clock_out test conflict");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("500 Internal Server Error",
        mock_json_utils_send_error_last_status,
        "a valid mode the device cannot bring up is a server-side fault, not a conflict");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_json_utils_send_response_called,
        "a failed mode change must not report success");
}

/* The body is fully validated before port_manager_set_mode() is called (the port index
 * comes from the URI registration, an unknown mode string is rejected earlier), so ANY
 * failure that comes back from set_mode() is server-side and must be a 500 — not only
 * ESP_ERR_INVALID_STATE. A plain ESP_FAIL from the serial init ("UART driver already
 * installed") is a device fault, and reporting it as 400 Bad Request would blame the
 * client for a request that was perfectly valid. */
void test_init_fail_esp_fail_is_reported_as_500(void)
{
    TEST_ASSERT_FALSE(port_manager_ports_frozen());
    mock_json_utils_reset();

    /* PASSIVE goes through bridge_port_init_serial_only() → ESP_FAIL. */
    mock_bridge_port_init_serial_only_should_fail = true;

    httpd_req_t req = {0};
    mock_json_utils_inject_mode(PORT_MODE_PASSIVE_STR);

    TEST_ASSERT_EQUAL(ESP_OK, port_set_mode_handler(&req, 0));

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_json_utils_send_error_called,
        "a mode change whose serial init fails must be reported as an error");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("500 Internal Server Error",
        mock_json_utils_send_error_last_status,
        "a failed port init is a server-side fault, not a bad request");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_json_utils_send_response_called,
        "a failed mode change must not report success");
}

/* persist-6 through the REST layer: the mode came up live but could not be written to
 * NVS. The request was valid, so this is a server-side failure too — 500, not 400. */
void test_persist_fail_is_reported_as_500(void)
{
    TEST_ASSERT_FALSE(port_manager_ports_frozen());
    mock_json_utils_reset();

    mock_setting_items_save_should_fail = true;

    httpd_req_t req = {0};
    mock_json_utils_inject_mode(PORT_MODE_PASSIVE_STR);

    TEST_ASSERT_EQUAL(ESP_OK, port_set_mode_handler(&req, 0));

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_json_utils_send_error_called,
        "a mode change that could not be persisted must be reported as an error");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("500 Internal Server Error",
        mock_json_utils_send_error_last_status,
        "a failed NVS write is a server-side fault, not a bad request");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_json_utils_send_response_called,
        "a mode change that could not be persisted must not report success");
}

/* The 400 path must stay: a genuinely invalid request body is still a bad request. */
void test_unknown_mode_value_is_reported_as_400(void)
{
    mock_json_utils_reset();

    httpd_req_t req = {0};
    mock_json_utils_inject_mode("nonsense_mode");

    TEST_ASSERT_EQUAL(ESP_OK, port_set_mode_handler(&req, 0));

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_json_utils_send_error_called,
        "an unknown mode value must be rejected");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("400 Bad Request",
        mock_json_utils_send_error_last_status,
        "an invalid request body is the one case that still returns 400");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_bridge_calls[0].bridge_port_init_called,
        "an invalid mode value must not reach the port init path");
}

/* ── The freeze must be observed under pm_lock, not before it ───────────────
 *
 * The race: settings_update_task enters apply_settings(), reads the freeze flag as
 * false, and is preempted before it can take pm_lock. wb_test then freezes the
 * ports, disables them and starts the LEDC on their TX/DE pins. settings_update_task
 * resumes, takes the lock and re-inits the port straight on top of the live waveform.
 *
 * The mutex mock lets us reproduce that window deterministically: the hook runs
 * inside xSemaphoreTake(), i.e. exactly while the caller is "waiting for the lock",
 * and freezes the ports there. A flag read before the lock still sees false and the
 * port comes up; a flag read after the lock sees the freeze and the port stays down.
 */
static void freeze_ports_while_waiting_for_lock(void)
{
    /* One-shot: pm_lock is not the only mutex taken further down this call path. */
    mock_xSemaphoreTake_hook = NULL;
    port_manager_set_ports_frozen(true);
}

void test_freeze_landing_during_lock_wait_stops_apply_settings(void)
{
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode_transient(0, PM_MODE_DISABLED));

    mock_bridge_reset();
    mock_serial_reset();

    /* settings_update_task calls apply_settings() just as the factory test freezes. */
    mock_xSemaphoreTake_hook = freeze_ports_while_waiting_for_lock;
    esp_err_t ret = port_manager_apply_settings(0);
    mock_xSemaphoreTake_hook = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(port_manager_ports_frozen());
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(0),
        "a freeze that lands while apply_settings waits for pm_lock must still stop it");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_bridge_calls[0].bridge_port_init_serial_only_called,
        "the port must not be re-opened on top of the LEDC output");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_bridge_calls[0].bridge_port_init_called,
        "the port must not be re-inited on top of the LEDC output");
}

void test_freeze_landing_during_lock_wait_stops_check_settings_changed(void)
{
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode_transient(0, PM_MODE_DISABLED));

    /* Runtime is DISABLED, NVS says "passive": reported as changed only if the
     * freeze is missed. */
    mock_xSemaphoreTake_hook = freeze_ports_while_waiting_for_lock;
    bool changed = port_manager_check_settings_changed(0);
    mock_xSemaphoreTake_hook = NULL;

    TEST_ASSERT_TRUE(port_manager_ports_frozen());
    TEST_ASSERT_FALSE_MESSAGE(changed,
        "a freeze that lands while check_settings_changed waits for pm_lock must be honoured");
}

void test_freeze_landing_during_lock_wait_stops_set_mode(void)
{
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));

    mock_bridge_reset();
    int saves_before = mock_setting_items_save_called;

    /* POST /ports/1/mode racing the start of the factory test. */
    mock_xSemaphoreTake_hook = freeze_ports_while_waiting_for_lock;
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_TCP_BRIDGE);
    mock_xSemaphoreTake_hook = NULL;

    TEST_ASSERT_EQUAL_MESSAGE(PM_ERR_PORTS_FROZEN, ret,
        "a freeze that lands while set_mode waits for pm_lock must reject the switch");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_bridge_calls[0].bridge_port_init_called,
        "the rejected switch must not init the new mode");
    TEST_ASSERT_EQUAL_MESSAGE(saves_before, mock_setting_items_save_called,
        "the rejected switch must not write to NVS");
}

void test_unfrozen_check_settings_changed_still_detects_mode_change(void)
{
    /* Regression guard for the normal (non-test) path: the freeze must not
     * suppress ordinary settings-driven re-inits. */
    TEST_ASSERT_FALSE(port_manager_ports_frozen());

    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));
    TEST_ASSERT_FALSE(port_manager_check_settings_changed(0));

    mock_setting_items_set_port_mode(0, PORT_MODE_TCP_BRIDGE_STR);
    TEST_ASSERT_TRUE_MESSAGE(port_manager_check_settings_changed(0),
        "an unfrozen port must still report a mode change");

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_TCP_BRIDGE, port_manager_get_mode(0),
        "an unfrozen port must still be re-inited by apply_settings");
}

void test_switch_passive_repeater_disabled(void)
{
    /* passive -> repeater -> disabled */
    port_manager_set_mode(0, PM_MODE_PASSIVE);

    mock_bridge_reset();
    mock_sniffer_reset();
    mock_serial_reset();
    mock_repeater_reset();

    /* Switch PASSIVE -> REPEATER: deinit passive (sniffer_detach + serial_deinit),
     * then init repeater. */
    esp_err_t ret = port_manager_set_mode(0, PM_MODE_REPEATER);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PM_MODE_REPEATER, port_manager_get_mode(0));
    TEST_ASSERT_EQUAL(1, mock_sniffer_detach_called[0]);   /* passive deinit */
    TEST_ASSERT_EQUAL(1, mock_serial_deinit_called[0]);    /* passive deinit */
    TEST_ASSERT_EQUAL(1, mock_repeater_calls[0].init_called);

    mock_sniffer_reset();
    mock_repeater_reset();

    /* Switch REPEATER -> DISABLED: deinit repeater (sniffer_detach + repeater_deinit). */
    ret = port_manager_set_mode(0, PM_MODE_DISABLED);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PM_MODE_DISABLED, port_manager_get_mode(0));
    TEST_ASSERT_EQUAL(1, mock_sniffer_detach_called[0]);
    TEST_ASSERT_EQUAL(1, mock_repeater_calls[0].deinit_called);
}

/* U-P1: repeater survives reboot — NVS "repeater" is reverse-parsed at init. */
void test_init_brings_up_repeater_from_nvs(void)
{
    // NVS persisted "repeater" for port 1 → on boot the port comes up as REPEATER.
    mock_setting_items_set_port_mode(0, PORT_MODE_REPEATER_STR);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());
    TEST_ASSERT_EQUAL(PM_MODE_REPEATER, port_manager_get_mode(0));
    TEST_ASSERT_EQUAL(1, mock_repeater_calls[0].init_called);
}

void test_init_unknown_nvs_mode_falls_back_to_disabled(void)
{
    // Garbage NVS value → DISABLED; repeater must NOT be started.
    mock_setting_items_set_port_mode(0, "bogus_mode");
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());
    TEST_ASSERT_EQUAL(PM_MODE_DISABLED, port_manager_get_mode(0));
    TEST_ASSERT_EQUAL(0, mock_repeater_calls[0].init_called);
}

/* U-P5: transitions into/out of repeater from a non-passive mode. */
void test_switch_tcp_bridge_repeater_passive(void)
{
    /* tcp_bridge -> repeater */
    port_manager_set_mode(0, PM_MODE_TCP_BRIDGE);
    mock_bridge_reset(); mock_sniffer_reset(); mock_serial_reset(); mock_repeater_reset();

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode(0, PM_MODE_REPEATER));
    TEST_ASSERT_EQUAL(PM_MODE_REPEATER, port_manager_get_mode(0));
    TEST_ASSERT_EQUAL(1, mock_sniffer_detach_called[0]);                  /* tcp_bridge deinit */
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_port_deinit_called); /* tcp_bridge deinit */
    TEST_ASSERT_EQUAL(1, mock_repeater_calls[0].init_called);            /* repeater init */
    TEST_ASSERT_EQUAL(1, mock_sniffer_attach_called[0]);
    TEST_ASSERT_EQUAL(1, mock_serial_set_rx_timeout_called[0]);
    TEST_ASSERT_EQUAL(SERIAL_RX_TOUT_PROXY, mock_serial_set_rx_timeout_value[0]);

    /* repeater -> passive */
    mock_bridge_reset(); mock_sniffer_reset(); mock_serial_reset(); mock_repeater_reset();

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode(0, PM_MODE_PASSIVE));
    TEST_ASSERT_EQUAL(PM_MODE_PASSIVE, port_manager_get_mode(0));
    TEST_ASSERT_EQUAL(1, mock_sniffer_detach_called[0]);                          /* repeater deinit */
    TEST_ASSERT_EQUAL(1, mock_repeater_calls[0].deinit_called);                   /* via repeater_deinit_port, NOT serial_deinit */
    TEST_ASSERT_EQUAL(1, mock_bridge_calls[0].bridge_port_init_serial_only_called); /* passive init */
    TEST_ASSERT_EQUAL(1, mock_serial_set_rx_timeout_called[0]);
    TEST_ASSERT_EQUAL(SERIAL_RX_TOUT_SNIFFER, mock_serial_set_rx_timeout_value[0]);
}

/* U-P6: a repeated set_mode(REPEATER) is exactly one deinit->init cycle. */
void test_repeated_set_mode_repeater_single_cycle(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode(0, PM_MODE_REPEATER));
    mock_repeater_reset();

    /* A second set_mode(REPEATER) is exactly one deinit->init cycle, never a double init. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode(0, PM_MODE_REPEATER));
    TEST_ASSERT_EQUAL(1, mock_repeater_calls[0].deinit_called);
    TEST_ASSERT_EQUAL(1, mock_repeater_calls[0].init_called);
}

/* U-P4: repeater teardown resets RS-485 stats. */
void test_repeater_teardown_resets_rs485_stats(void)
{
    port_manager_set_mode(0, PM_MODE_REPEATER);
    mock_sniffer_reset(); mock_repeater_reset(); mock_rs485_stats_reset_all();

    port_manager_set_mode(0, PM_MODE_DISABLED);

    TEST_ASSERT_EQUAL(1, mock_sniffer_detach_called[0]);
    TEST_ASSERT_EQUAL(1, mock_repeater_calls[0].deinit_called);
    TEST_ASSERT_EQUAL(1, mock_rs485_busy_monitor_reset_called[0]);
    TEST_ASSERT_EQUAL(1, mock_rs485_stats_reset_called[0]);
}

/* U-P3: get_port_serial_desc(REPEATER) wired through port_manager_send_raw. */
void test_send_raw_repeater_port_calls_serial_send(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode(0, PM_MODE_REPEATER));
    mock_serial_reset();   /* isolate the send */
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_send_raw(0, data, sizeof(data)));
    TEST_ASSERT_EQUAL(1, mock_serial_send_called);
    TEST_ASSERT_EQUAL(sizeof(data), mock_serial_send_last_len);
}

/* U-P2: check_settings_changed(REPEATER) — mode sub-branch. */
void test_check_settings_changed_repeater_mode_branch(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode(0, PM_MODE_REPEATER));
    /* NVS mode now matches runtime → no change. */
    TEST_ASSERT_FALSE(port_manager_check_settings_changed(0));
    /* NVS mode changed to passive → change detected (mode sub-branch). */
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    TEST_ASSERT_TRUE(port_manager_check_settings_changed(0));
}

/* U-P2: check_settings_changed(REPEATER) — serial-params sub-branch (via R3). */
void test_check_settings_changed_repeater_serial_params(void)
{
    /* Inject config A, bring up REPEATER (snapshot = A). */
    serial_config_t cfgA; memset(&cfgA, 0, sizeof(cfgA)); cfgA.baudrate = 9600;
    mock_bridge_set_serial_config(0, &cfgA);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode(0, PM_MODE_REPEATER));
    TEST_ASSERT_FALSE(port_manager_check_settings_changed(0));   /* same config → no change */

    /* NVS serial params now differ → change detected (serial-params sub-branch). */
    serial_config_t cfgB = cfgA; cfgB.baudrate = 115200;
    mock_bridge_set_serial_config(0, &cfgB);
    TEST_ASSERT_TRUE(port_manager_check_settings_changed(0));
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

void test_set_cache_second_port_rejected(void)
{
    /* Single-port cache invariant (review #51): the cache feeds from only one
     * RS-485 port. Enabling the overlay on a second port while another already has
     * it must be rejected, leaving the second port's overlay and its NVS untouched
     * and triggering no extra pool enable. (Replaces the old two-port deinit test,
     * whose "both ports feed the cache" premise is now impossible.) */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));
    TEST_ASSERT_TRUE(port_manager_get_cache(0));
    int enable_before = mock_cache_multimaster_enable_called;

    esp_err_t ret = port_manager_set_cache(1, true);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    TEST_ASSERT_FALSE(port_manager_get_cache(1));   /* overlay not set */
    TEST_ASSERT_FALSE(mock_cache_en[1]);            /* NVS untouched */
    TEST_ASSERT_EQUAL(enable_before, mock_cache_multimaster_enable_called);
}

void test_set_cache_second_port_allowed_after_first_disabled(void)
{
    /* Once the first port releases the cache overlay, the second may take it. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, false));
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(1, true));
    TEST_ASSERT_TRUE(port_manager_get_cache(1));
    TEST_ASSERT_FALSE(port_manager_get_cache(0));
}

void test_init_normalises_dual_cache_nvs(void)
{
    /* Legacy NVS may carry the overlay on BOTH ports. port_manager_init() must
     * normalise to a single port (lowest index wins) and rewrite the loser's NVS
     * to false, so the single-port invariant holds from boot. */
    mock_cache_en[0] = true;
    mock_cache_en[1] = true;

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());

    TEST_ASSERT_TRUE(port_manager_get_cache(0));    /* kept */
    TEST_ASSERT_FALSE(port_manager_get_cache(1));   /* cleared in memory */
    TEST_ASSERT_TRUE(mock_cache_en[0]);             /* NVS kept */
    TEST_ASSERT_FALSE(mock_cache_en[1]);            /* NVS rewritten to false */
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

/* persist-6: if the NVS write fails, set_cache must NOT report success — the
 * live overlay is applied but a reboot would silently revert it, so the caller
 * has to know. */
void test_set_cache_persist_failure_surfaced(void)
{
    mock_setting_items_save_bool_should_fail = true;

    esp_err_t ret = port_manager_set_cache(0, true);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, ret,
        "set_cache must surface the NVS write failure, not report success (persist-6)");
    /* The live overlay is still applied despite the persistence failure. */
    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(0),
        "the live cache overlay must still be applied even when persistence failed");
}

/* persist-6: if the NVS write fails, set_mode must NOT report success even
 * though the mode initialised live. */
void test_set_mode_persist_failure_surfaced(void)
{
    mock_setting_items_save_should_fail = true;

    esp_err_t ret = port_manager_set_mode(0, PM_MODE_PASSIVE);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, ret,
        "set_mode must surface the NVS write failure, not report success (persist-6)");
    /* The mode initialised live despite the persistence failure. */
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "the mode must still be live even when persistence failed");
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
 * 10. Boot order — port_manager_init_subsystems()
 *
 * http_server_init() runs before the wait-for-network loop in main.c, and the URI
 * handlers it registers reach into FreeRTOS handles owned by the sniffer and the
 * multimaster cache without a NULL check (FreeRTOS configASSERTs on a NULL handle,
 * which is a panic and a reboot in this build). So everything those handlers touch
 * has to be up before httpd starts, and only what genuinely needs a network
 * interface may stay behind the loop with port_manager_init().
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_init_subsystems_starts_all_network_independent_subsystems(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init_subsystems());

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_sniffer_init_called,
        "sniffer (WS mutex, queue, per-port timers, WS task) must be up before httpd starts");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_init_called,
        "the cache mutex must be up before httpd starts");
    TEST_ASSERT_EQUAL(1, mock_repeater_global_init_called);
    TEST_ASSERT_EQUAL(1, mock_rs485_busy_monitor_init_called);
    TEST_ASSERT_EQUAL(1, mock_rs485_stats_init_called);
}

void test_init_subsystems_does_not_touch_network_dependent_parts(void)
{
    /* Both of these would need an interface to bind to, and this call runs before
     * there is one. */
    mock_cache_server_enabled = true;
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    mock_setting_items_set_port_mode(1, PORT_MODE_TCP_BRIDGE_STR);

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init_subsystems());

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_modbus_server_init_called,
        "the cache Modbus server binds a TCP socket — it must wait for port_manager_init()");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(0),
        "ports must not come up before the network does");
    TEST_ASSERT_EQUAL(PM_MODE_DISABLED, port_manager_get_mode(1));
    TEST_ASSERT_EQUAL(0, mock_bridge_calls[0].bridge_port_init_serial_only_called);
    TEST_ASSERT_EQUAL(0, mock_bridge_calls[1].bridge_port_init_called);
}

void test_init_subsystems_is_idempotent(void)
{
    /* None of the subsystems is idempotent on its own: a second sniffer_init()
     * would create a second queue, a second pair of timers and a second WS task,
     * leaking the first set. Both entry points (main.c and port_manager_init())
     * may call this, so the guard has to hold. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init_subsystems());
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init_subsystems());

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_sniffer_init_called,
        "a repeated init must not create a second sniffer queue/timers/task");
    TEST_ASSERT_EQUAL(1, mock_cache_multimaster_init_called);
    TEST_ASSERT_EQUAL(1, mock_repeater_global_init_called);
    TEST_ASSERT_EQUAL(1, mock_rs485_busy_monitor_init_called);
    TEST_ASSERT_EQUAL(1, mock_rs485_stats_init_called);
}

void test_init_after_subsystems_does_not_reinit_them(void)
{
    /* The real boot sequence: subsystems before httpd, ports once the network is up. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init_subsystems());
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_sniffer_init_called,
        "port_manager_init() must not re-init the subsystems main.c already brought up");
    TEST_ASSERT_EQUAL(1, mock_cache_multimaster_init_called);
    TEST_ASSERT_EQUAL(1, mock_repeater_global_init_called);
    TEST_ASSERT_EQUAL(1, mock_rs485_busy_monitor_init_called);
    TEST_ASSERT_EQUAL(1, mock_rs485_stats_init_called);
}

void test_init_alone_still_starts_subsystems(void)
{
    /* port_manager_init() stays self-contained for callers that use it alone. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());

    TEST_ASSERT_EQUAL(1, mock_sniffer_init_called);
    TEST_ASSERT_EQUAL(1, mock_cache_multimaster_init_called);
    TEST_ASSERT_EQUAL(1, mock_repeater_global_init_called);
    TEST_ASSERT_EQUAL(1, mock_rs485_busy_monitor_init_called);
    TEST_ASSERT_EQUAL(1, mock_rs485_stats_init_called);
}

void test_init_subsystems_partial_failure_is_not_retried(void)
{
    /* A subsystem running out of memory must not turn the guard into a one-shot that
     * never fired: the flag means "the attempt has been made", not "it went well".
     * Otherwise main.c's failed call would leave the flag clear and port_manager_init()
     * would run the whole sequence again — a second sniffer queue, a second pair of
     * timers and a second WS task, with the first set leaked and still holding the
     * serial callbacks. */
    mock_cache_multimaster_init_should_fail = true;
    esp_err_t ret = port_manager_init_subsystems();
    mock_cache_multimaster_init_should_fail = false;

    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, ret,
        "the caller must be told, so it can log the degraded state");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_sniffer_init_called,
        "the subsystems before the failing one must have been started");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_init_called,
        "an independent subsystem must still be attempted after another one failed");

    /* Whatever comes next — a retry from main.c, or port_manager_init() — must not
     * re-run anything. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init_subsystems());
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_sniffer_init_called,
        "a partial failure must not be retried — that would create a second sniffer");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_init_called,
        "nor a second cache mutex");
    TEST_ASSERT_EQUAL(1, mock_repeater_global_init_called);
    TEST_ASSERT_EQUAL(1, mock_rs485_busy_monitor_init_called);
    TEST_ASSERT_EQUAL(1, mock_rs485_stats_init_called);
}

void test_init_subsystems_sniffer_failure_does_not_skip_the_cache(void)
{
    /* The sniffer and the cache are independent, and the sequence deliberately does not
     * bail out on the first failure: losing the sniffer must not silently disable the
     * cache as well. The failing subsystem is the FIRST one here, which is the half the
     * cache-fails test above cannot see — there it is the last one and everything before
     * it had already run. */
    mock_sniffer_init_should_fail = true;
    esp_err_t ret = port_manager_init_subsystems();
    mock_sniffer_init_should_fail = false;

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_sniffer_init_called, "the sniffer must have been tried");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_init_called,
        "the cache must still be attempted after the sniffer failed — they are independent");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, ret,
        "the sniffer's own error is what the caller must be told about");
}

void test_init_subsystems_reports_the_first_error_when_both_fail(void)
{
    /* "Report the first error" is only a real rule while a second one can disagree with
     * it. The two mocks fail with different codes on purpose (sniffer: ESP_FAIL, the WS
     * task that would not start; cache: ESP_ERR_NO_MEM, the mutex), so returning the
     * later error instead of the first one is visible here. */
    mock_sniffer_init_should_fail = true;
    mock_cache_multimaster_init_should_fail = true;
    esp_err_t ret = port_manager_init_subsystems();
    mock_sniffer_init_should_fail = false;
    mock_cache_multimaster_init_should_fail = false;

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_sniffer_init_called, "the sniffer must have been tried");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_init_called,
        "and the cache after it, even though there was already an error to report");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, ret,
        "the first error wins — a later one must not overwrite it");
}

void test_init_still_brings_ports_up_after_subsystem_failure(void)
{
    /* The ports are what the device is installed for. A sniffer or cache mutex that
     * would not allocate must not keep them down, and must not come back out of
     * port_manager_init() as a failure at all — main.c only logs one now (it no longer
     * aborts on it), so a false error there reads as a degraded boot that never happened. */
    mock_cache_multimaster_init_should_fail = true;
    (void)port_manager_init_subsystems();
    mock_cache_multimaster_init_should_fail = false;

    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, port_manager_init(),
        "a degraded subsystem must not be reported as a port-manager failure");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "the port must come up regardless");
}

void test_init_still_brings_ports_up_after_cache_modbus_server_failure(void)
{
    /* The cache Modbus server sits ABOVE the port loop, and its failure used to leave
     * this function through ESP_RETURN_ON_ERROR: the ports were never reached and the
     * error landed on main.c's ESP_ERROR_CHECK — abort, reboot, RS-485 down. Unlike the
     * subsystems, this one does not fail only on out-of-memory: when cache_modbus_port
     * equals a bridge port, both listeners are AF_INET/INADDR_ANY, so lwIP's tcp_listen()
     * really does return ERR_USE and tcp_server_init() returns ESP_FAIL — and a reboot
     * only swaps which of the two loses the port. Both ports are configured here, since
     * the abort took every one of them down, not just the port that carries the cache. */
    mock_cache_server_enabled = true;
    mock_cache_modbus_server_init_should_fail = true;
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    mock_setting_items_set_port_mode(1, PORT_MODE_TCP_BRIDGE_STR);

    esp_err_t ret = port_manager_init();

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_modbus_server_init_called,
        "the server must still be attempted — it is enabled in NVS");
    /* The ports come first: they are what the early return actually cost, and asserting
     * them before the return code is what makes a reintroduced ESP_RETURN_ON_ERROR report
     * the damage rather than just the error code. */
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "the ports must come up regardless — routing Modbus is what the device is for");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_TCP_BRIDGE, port_manager_get_mode(1),
        "including the port that does not carry the cache");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "a dead cache Modbus server must not be reported as a port-manager failure: "
        "main.c would abort the boot on it");
}

/* Overwrite the content of every //... and /*...*\/ comment with spaces, in place. Length,
 * newlines and every non-comment byte are left alone, so positions in the buffer still line
 * up with the file and the ordering test's before/after comparisons stay meaningful.
 *
 * Why: both checks below are strstr() over source text, where a string in a COMMENT counts
 * as a hit. main.c already talks about ESP_ERROR_CHECK in prose three times (main.c:171,185,
 * 223) and passes only because the macro name happens to be followed by ',' or ':' there
 * rather than '('. Documenting this very rule as `ESP_ERROR_CHECK(port_manager_init())` in a
 * comment is the natural thing to write, and it would fail a build whose code is correct.
 *
 * String literals are deliberately NOT tracked — a "//" inside one would blank the real code
 * after it. That error points the safe way: the guard gets STRICTER, so main.c growing such a
 * literal costs a false failure (noisy, and visible) rather than a silent pass. */
static void blank_out_comments(char *s)
{
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == '/' && s[i + 1] == '/') {
            /* To end of line; the '\n' itself is left for the outer loop to step over. */
            while (s[i] != '\0' && s[i] != '\n') s[i++] = ' ';
            if (s[i] == '\0') break;
        } else if (s[i] == '/' && s[i + 1] == '*') {
            s[i] = ' ';
            s[i + 1] = ' ';
            i += 2;
            while (s[i] != '\0' && !(s[i] == '*' && s[i + 1] == '/')) {
                if (s[i] != '\n') s[i] = ' ';
                i++;
            }
            if (s[i] == '\0') break;   /* unterminated block comment: nothing left to keep */
            s[i] = ' ';
            s[i + 1] = ' ';
            i++;
        }
    }
}

/* Load main/main.c for the source-text checks below, or fail the test trying, and hand
 * back the buffer it lives in, comments blanked. Split out because both of them need the
 * whole file and the same "did it all arrive" reasoning; the buffer is static HERE rather
 * than one per test, because two 64 KB statics is 128 KB of BSS in the host binary for the
 * same file, and neither caller keeps the text past its own test. */
static const char *read_main_c_source(void)
{
    static char src[64 * 1024];
    FILE *f = fopen("../../main/main.c", "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "main/main.c not readable — run this test from unittests/port_manager");
    size_t n = fread(src, 1, sizeof(src) - 1, f);
    fclose(f);
    src[n] = '\0';
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, n, "main/main.c is empty");
    /* A short read is the only proof the whole file arrived: fread() filling the buffer
     * to the brim looks exactly like a file that is one byte too long, and the anchors
     * in the callers would then be searched in a silently truncated copy — a missing
     * statement would read as "the call is gone" and a missing closing brace as "the call
     * is outside app_main()". Both are wrong answers, so refuse to answer at all. */
    TEST_ASSERT_LESS_THAN_MESSAGE(sizeof(src) - 1, n,
        "main/main.c no longer fits in src[] — grow the buffer, do not trust a truncated read");
    blank_out_comments(src);
    return src;
}

/* The ordering half of the invariant lives in main.c, which has no host test
 * harness: nothing links app_main() — the file is not in any unit test's SRC and
 * could not be, since it pulls in the whole firmware — so the only thing a unit test
 * can inspect is the source text. Reading a production source file from a test is a
 * deliberate exception, made here because the alternative is not checking the order
 * at all. Crude, but it is what catches the two calls being swapped back — and a swap
 * puts the sniffer WS endpoint back on the air ahead of the handles it drives.
 * Anchored on the statements, not on the identifiers, and read_main_c_source() blanks
 * comment content out, so the comments around them (which name both functions) cannot
 * satisfy the check at all rather than merely being unlikely to. Bracketed by app_main()'s
 * opening line and its closing brace, so hoisting either call into a helper — above
 * app_main() or below it — does not quietly pass either.
 *
 * What this does NOT prove: that the two statements are on the same execution path.
 * Both could sit in mutually exclusive #if branches, or one inside an if() the other
 * is not, and the text order would still read correctly. Establishing that needs a
 * parser, not strstr(); the swap this guards against is the failure that actually
 * happened. */
void test_main_c_inits_subsystems_before_starting_httpd(void)
{
    const char *src = read_main_c_source();

    /* "\nvoid app_main(" and not "void app_main(": the plain form matches the first mention
     * anywhere in the file, so anything naming the function ahead of its definition would
     * move the anchor up and hand the position checks below a meaningless bound. The leading
     * '\n' pins the match to column 0, which the definition (main.c:108) reaches and the
     * mentions that survive do not — a call is indented inside a body, and a mention inside a
     * string literal sits behind the opening quote (blank_out_comments() strips comments, not
     * literals, by design). A bare file-scope prototype would still match, but main.c carries
     * none: app_main() is declared by ESP-IDF's headers. */
    const char *app_main   = strstr(src, "\nvoid app_main(");
    const char *subsystems = strstr(src, "= port_manager_init_subsystems();");
    const char *httpd      = strstr(src, "= http_server_init();");

    TEST_ASSERT_NOT_NULL_MESSAGE(app_main,
        "main.c must still define app_main() at file scope");
    TEST_ASSERT_NOT_NULL_MESSAGE(subsystems,
        "main.c must call port_manager_init_subsystems() — the URI handlers registered by "
        "http_server_init() drive the FreeRTOS handles it creates");
    TEST_ASSERT_NOT_NULL_MESSAGE(httpd, "main.c must still call http_server_init()");

    /* Where app_main()'s body ends: the first closing brace at column 0 after its
     * opening line. Everything inside the body is indented, so this is the function's
     * own terminator. */
    const char *app_main_end = strstr(app_main + 1, "\n}");
    TEST_ASSERT_NOT_NULL_MESSAGE(app_main_end,
        "app_main() must still be a brace-terminated function at file scope");

    /* Both calls must be inside app_main() itself. Hoisting them into a helper — defined
     * before app_main() or after it — would keep the two in the right order relative to
     * each other while saying nothing about the order they run in. */
    TEST_ASSERT_TRUE_MESSAGE(app_main < subsystems && subsystems < app_main_end,
        "port_manager_init_subsystems() must be called from inside app_main(), not from a helper");
    TEST_ASSERT_TRUE_MESSAGE(app_main < httpd && httpd < app_main_end,
        "http_server_init() must be called from inside app_main(), not from a helper");
    TEST_ASSERT_TRUE_MESSAGE(subsystems < httpd,
        "port_manager_init_subsystems() must be called BEFORE http_server_init() in main.c");
}

/* The other half of "a failed init must not cost the device its ports" is main.c's side
 * of the call, and it is the same source-text exception as the ordering check above.
 * port_manager_init() returns only ESP_OK today, so this guards the future: the moment a
 * new error path appears in it, ESP_ERROR_CHECK would turn that path back into an
 * abort() — reboot with the RS-485 ports down, on a cause that meets the next boot
 * unchanged (out of memory) or that only moves to the other port (a cache/bridge port
 * collision). Also insists the result is still assigned to pm_ret by that exact name, so the
 * alternative regression (dropping the check entirely, `(void)port_manager_init();`) does not
 * pass either, and a rename cannot hollow out the pm_ret check below. */

/* Is there an ESP_ERROR_CHECK in src whose argument text starts with `name`?
 *
 * The regression class this closes: matching the literal "ESP_ERROR_CHECK(port_manager_init())"
 * alone is trivial to walk past. `esp_err_t pm_ret = port_manager_init(); ESP_ERROR_CHECK(pm_ret);`
 * keeps the required "= port_manager_init();" and never contains the literal, and so does
 * `ESP_ERROR_CHECK( port_manager_init() )` with spaces inside the parens. Both abort the boot
 * exactly as the removed call did. So skip whitespace after the macro name and after the '(',
 * and check every occurrence — only one of them has to be the bad one.
 *
 * What it still cannot prove: this is strstr(), not a parser. Comments no longer count —
 * read_main_c_source() blanks them — but a hit inside a string literal still does, and the same
 * abort reached through another macro, a helper, or `if (pm_ret != ESP_OK) abort();` does not.
 * The prefix match also fires on
 * ESP_ERROR_CHECK(port_manager_init_subsystems()) — which main.c avoids for the same reason,
 * so that is a wanted hit rather than a false one. */
static bool esp_error_check_wraps(const char *src, const char *name)
{
    const char *macro = "ESP_ERROR_CHECK";
    const size_t name_len = strlen(name);

    for (const char *p = strstr(src, macro); p != NULL; p = strstr(p + 1, macro)) {
        const char *arg = p + strlen(macro);
        while (*arg == ' ' || *arg == '\t' || *arg == '\n' || *arg == '\r') arg++;
        if (*arg != '(') continue;   /* ESP_ERROR_CHECK_WITHOUT_ABORT and friends */
        arg++;
        while (*arg == ' ' || *arg == '\t' || *arg == '\n' || *arg == '\r') arg++;
        if (strncmp(arg, name, name_len) == 0) return true;
    }
    return false;
}

void test_main_c_does_not_abort_the_boot_on_a_port_manager_init_failure(void)
{
    const char *src = read_main_c_source();

    TEST_ASSERT_NULL_MESSAGE(strstr(src, "ESP_ERROR_CHECK(port_manager_init())"),
        "port_manager_init() must not be called under ESP_ERROR_CHECK — an abort() here reboots "
        "the gateway instead of logging one degraded feature");
    /* The full declaration, not the looser "= port_manager_init();": the esp_error_check_wraps()
     * call below is keyed on the literal name pm_ret, so a rename in main.c would turn that guard
     * into a tautology and let `ESP_ERROR_CHECK(ret);` through while the loose anchor still
     * matched. Pinning the name here makes the rename fail loudly instead. Also still covers the
     * other regression, dropping the result entirely: `(void)port_manager_init();`. */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "esp_err_t pm_ret = port_manager_init();"),
        "main.c must keep port_manager_init()'s result in pm_ret — the ESP_ERROR_CHECK(pm_ret) "
        "guard below is keyed on that name");

    TEST_ASSERT_FALSE_MESSAGE(esp_error_check_wraps(src, "port_manager_init"),
        "no ESP_ERROR_CHECK may wrap port_manager_init(), whitespace inside the parens included");
    TEST_ASSERT_FALSE_MESSAGE(esp_error_check_wraps(src, "pm_ret"),
        "port_manager_init()'s result must not be handed to ESP_ERROR_CHECK either — "
        "`pm_ret = port_manager_init(); ESP_ERROR_CHECK(pm_ret);` aborts just the same");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "port_manager_init failed"),
        "main.c must still LOG the failure it no longer aborts on — dropping the ESP_LOGE while "
        "keeping the assignment turns a lost gateway feature into silence");
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
    RUN_TEST(test_mode_to_str_repeater);
    RUN_TEST(test_mode_to_str_unknown);

    /* 2 – get_mode */
    RUN_TEST(test_get_mode_initial_disabled);
    RUN_TEST(test_get_mode_invalid_index);
    RUN_TEST(test_get_mode_after_set_mode);

    /* 3 – set_mode error paths */
    RUN_TEST(test_set_mode_invalid_index);
    RUN_TEST(test_set_mode_tcp_bridge_init_fail);
    RUN_TEST(test_set_mode_init_fail_keeps_previous_nvs_mode);
    RUN_TEST(test_set_mode_passive_serial_fail);
    RUN_TEST(test_set_mode_double_init_failure_leaves_disabled);

    /* 4 – port_init_mode for each mode + cache overlay */
    RUN_TEST(test_set_mode_disabled);
    RUN_TEST(test_set_mode_tcp_bridge_success);
    RUN_TEST(test_set_mode_passive_success);
    RUN_TEST(test_set_mode_repeater_success);

    RUN_TEST(test_set_mode_transient_invalid_index);
    RUN_TEST(test_set_mode_transient_applies_live_but_does_not_persist);
    RUN_TEST(test_set_mode_transient_restored_from_nvs_on_exit);
    RUN_TEST(test_set_mode_transient_init_fail_rolls_back);
    /* factory-test port freeze (clock_out) */
    RUN_TEST(test_frozen_check_settings_changed_reports_no_change);
    RUN_TEST(test_frozen_apply_settings_does_not_bring_port_up);
    RUN_TEST(test_frozen_release_does_not_tear_port_down);
    RUN_TEST(test_release_deinits_running_port);
    RUN_TEST(test_frozen_ports_do_not_touch_nvs);
    RUN_TEST(test_frozen_set_mode_rejected);
    RUN_TEST(test_frozen_set_mode_transient_still_allowed);
    RUN_TEST(test_frozen_set_mode_handler_returns_409);
    RUN_TEST(test_unfrozen_set_mode_handler_succeeds);
    RUN_TEST(test_set_mode_handler_collision_returns_409);
    RUN_TEST(test_init_fail_invalid_state_is_not_reported_as_409);
    RUN_TEST(test_init_fail_esp_fail_is_reported_as_500);
    RUN_TEST(test_persist_fail_is_reported_as_500);
    RUN_TEST(test_unknown_mode_value_is_reported_as_400);
    RUN_TEST(test_freeze_landing_during_lock_wait_stops_apply_settings);
    RUN_TEST(test_freeze_landing_during_lock_wait_stops_check_settings_changed);
    RUN_TEST(test_freeze_landing_during_lock_wait_stops_set_mode);
    RUN_TEST(test_unfreeze_restores_mode_from_nvs);
    RUN_TEST(test_unfreeze_picks_up_settings_written_during_test);
    RUN_TEST(test_unfrozen_check_settings_changed_still_detects_mode_change);
    /* U-P1/U-P2/U-P3/U-P4/U-P5/U-P6 — repeater integration */
    RUN_TEST(test_init_brings_up_repeater_from_nvs);
    RUN_TEST(test_init_unknown_nvs_mode_falls_back_to_disabled);
    RUN_TEST(test_switch_tcp_bridge_repeater_passive);
    RUN_TEST(test_repeated_set_mode_repeater_single_cycle);
    RUN_TEST(test_repeater_teardown_resets_rs485_stats);
    RUN_TEST(test_send_raw_repeater_port_calls_serial_send);
    RUN_TEST(test_check_settings_changed_repeater_mode_branch);
    RUN_TEST(test_check_settings_changed_repeater_serial_params);
    RUN_TEST(test_set_mode_passive_with_cache_overlay_enables_cache);
    RUN_TEST(test_set_mode_tcp_bridge_with_cache_overlay_enables_cache);

    /* 5 – port_deinit_mode + cache overlay control */
    RUN_TEST(test_deinit_tcp_bridge);
    RUN_TEST(test_deinit_passive);
    RUN_TEST(test_deinit_cache_overlay_last_port);
    RUN_TEST(test_set_cache_second_port_rejected);
    RUN_TEST(test_set_cache_second_port_allowed_after_first_disabled);
    RUN_TEST(test_init_normalises_dual_cache_nvs);
    RUN_TEST(test_cache_overlay_survives_transport_change);
    RUN_TEST(test_set_cache_invalid_port);
    RUN_TEST(test_set_cache_disable_clears_reason_and_disables_cache);
    RUN_TEST(test_get_cache_invalid_port);
    RUN_TEST(test_set_cache_persist_failure_surfaced);
    RUN_TEST(test_set_mode_persist_failure_surfaced);
    RUN_TEST(test_apply_settings_preserves_cache_on_same_port_reinit);

    /* 6 – mode switching sequences */
    RUN_TEST(test_switch_from_passive_to_tcp_bridge);
    RUN_TEST(test_switch_from_tcp_bridge_to_disabled);
    RUN_TEST(test_switch_passive_repeater_disabled);

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
    RUN_TEST(test_send_raw_tcp_bridge_port_calls_serial_send);

    /* 9 – port_send_handler integration */
    RUN_TEST(test_port_send_handler_valid_hex);
    RUN_TEST(test_port_send_handler_odd_length_rejected);
    RUN_TEST(test_port_send_handler_nonhex_rejected);
    RUN_TEST(test_port_send_handler_too_long_rejected);

    /* 10 – boot order: network-independent subsystems before httpd */
    RUN_TEST(test_init_subsystems_starts_all_network_independent_subsystems);
    RUN_TEST(test_init_subsystems_does_not_touch_network_dependent_parts);
    RUN_TEST(test_init_subsystems_is_idempotent);
    RUN_TEST(test_init_after_subsystems_does_not_reinit_them);
    RUN_TEST(test_init_alone_still_starts_subsystems);
    RUN_TEST(test_init_subsystems_partial_failure_is_not_retried);
    RUN_TEST(test_init_subsystems_sniffer_failure_does_not_skip_the_cache);
    RUN_TEST(test_init_subsystems_reports_the_first_error_when_both_fail);
    RUN_TEST(test_init_still_brings_ports_up_after_subsystem_failure);
    RUN_TEST(test_init_still_brings_ports_up_after_cache_modbus_server_failure);
    RUN_TEST(test_main_c_inits_subsystems_before_starting_httpd);
    RUN_TEST(test_main_c_does_not_abort_the_boot_on_a_port_manager_init_failure);

    return UNITY_END();
}

int main(void)
{
    return port_manager_test();
}
