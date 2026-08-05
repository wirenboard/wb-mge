#include "unity.h"

#include "port_manager.h"
#include "sniffer.h"
#include "setting_items.h"
#include "bridge_mock.h"
#include "repeater_mock.h"
#include "esp_http_server.h"      /* httpd_req_t, for the REST handler tests */
#include "freertos/semphr.h"      /* mock_xSemaphoreTake_hook, for the pm_lock race tests */

#include <ctype.h>
#include <stdio.h>                /* main.c boot-order check reads the source file */
#include <string.h>

/* Statics that port_manager.c exposes to the tests via PORT_MANAGER_STATIC */
int hex_str_to_bytes(const char *hex, uint8_t *out, size_t out_max);
esp_err_t port_set_mode_handler(httpd_req_t *req, unsigned port_index);
esp_err_t port_set_cache_handler(httpd_req_t *req, unsigned port_index);

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
extern int mock_cache_multimaster_clear_called;
extern bool mock_cache_multimaster_enabled;
extern bool mock_cache_multimaster_init_should_fail;
extern bool mock_cache_multimaster_enable_should_fail;
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
extern const char *mock_setting_items_save_bool_fail_key;  /* fail one key only */
extern int mock_setting_items_save_bool_called;
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
void mock_json_utils_inject_enabled(bool enabled);

/* settings_manager.c mock — injectable result of the port-mode collision pre-check.
 * Defaults to ESP_OK; a test sets it to drive the handler's 409-collision branch. */
extern esp_err_t g_mock_port_mode_collision_ret;

/* repeater.c mock state is declared in repeater_mock.h */

/* ── setUp / tearDown ───────────────────────────────────────────────────── */

void setUp(void)
{
    /* Full reset rather than just `mock_xSemaphoreTake_hook = NULL`: every knob in this mock
     * is global state, and a TEST_ASSERT failure longjmps straight out of the test that set
     * one, past whatever restore it had planned at the end. Leaving that to the tests means
     * one red test can turn every later test in the binary red for an unrelated reason. Runs
     * BEFORE port_manager_reset_for_test() so the lock creations that reset performs are
     * visible to a test that wants to count them. */
    mock_freertos_semaphore_reset();
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

/* B5: the cache source must be movable in BOTH directions in a single call.
 *
 * The cache stays single-port (review #51) — the pool is keyed by slave_id and the
 * Cache-TCP interface answers by unit_id, so two ports would collide same-address
 * slaves from two buses. The invariant is now upheld by the enable path itself rather
 * than by a refusal: enabling the overlay MOVES it off whatever port holds it. Refusing
 * made the outcome depend on call order — a client that handles port 1 before port 2
 * could move the source 1 -> 2 but not 2 -> 1.
 * The move is not atomic (release and enable run under two different pm_locks), so the
 * invariant holds only while calls are serialised, as the single esp_http_server request
 * task makes them today; see the locking note in port_manager_set_cache(). These tests
 * are single-threaded and pin the sequential behaviour, not that premise. */
void test_set_cache_enable_moves_overlay_from_the_other_port(void)
{
    /* Both ports open, so the live data flow (sniffer) is observable on both. */
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    port_manager_set_mode(1, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(1, true));
    TEST_ASSERT_TRUE(port_manager_get_cache(1));
    TEST_ASSERT_TRUE(mock_cache_en[1]);
    mock_sniffer_reset();
    mock_cache_multimaster_reset();
    mock_cache_multimaster_enabled = true;   /* restore live state after counter reset */

    esp_err_t ret = port_manager_set_cache(0, true);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "enabling the overlay must move it, not be refused");
    /* Target holds it, old holder released — in memory AND in NVS, so a reboot does
     * not hand it back (port_manager_init() keeps the lowest-index port that asks). */
    TEST_ASSERT_TRUE(port_manager_get_cache(0));
    TEST_ASSERT_FALSE(port_manager_get_cache(1));
    TEST_ASSERT_TRUE(mock_cache_en[0]);
    TEST_ASSERT_FALSE_MESSAGE(mock_cache_en[1],
        "the released port's NVS key must be rewritten to false, not just its memory");
    /* The old holder's live data flow is unwired and the target's is wired. */
    TEST_ASSERT_EQUAL(1, mock_sniffer_disable_called[1]);
    TEST_ASSERT_EQUAL(SNIFF_REASON_CACHE, mock_sniffer_disable_last_reason[1]);
    TEST_ASSERT_EQUAL(1, mock_sniffer_enable_called[0]);
    TEST_ASSERT_EQUAL(SNIFF_REASON_CACHE, mock_sniffer_enable_last_reason[0]);
    /* The pool is NOT cycled. Some port wants it before the move and some port wants it
     * after, so the single sync that runs once both overlay flags are final sees no
     * transition at all — no free, no 32 KB reallocation, and no window with the cache
     * off. A per-half sync (the shape this replaces) would free the pool between the
     * release and the enable and then try to get it back: a call that only moves a
     * WORKING cache would be able to destroy it, on the one allocation here most likely
     * to fail. */
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_disable_called,
        "a move must not free the pool — it is wanted before and after");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_enable_called,
        "a move must not reallocate the pool it never freed");
    /* The CONTENTS are still dropped: they describe the bus the cache was moved away
     * from, and the pool has no port dimension to keep them apart. clear() does that
     * without freeing (see CM-U-008b in the cache_multimaster suite). */
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_clear_called,
        "a move must drop the values read from the old bus");
    TEST_ASSERT_TRUE_MESSAGE(mock_cache_multimaster_enabled,
        "the cache must stay on across the move, not blink off and back");
}

/* Samples the overlay state of both ports at every mutex acquisition inside
 * port_manager_set_cache() — i.e. on entry to each port's pm_lock critical section and
 * inside each cache_sync_global(). This is a sampler, not a continuous watch: a
 * both-held state that appears and disappears entirely between two acquisitions is
 * invisible to it. What it does guarantee is that a both-held state still standing at
 * the NEXT acquisition is caught, and that is enough for the orderings this test is
 * meant to kill — enabling the target before releasing the old holder, or dropping the
 * release entirely, both leave the second port set across at least one later
 * acquisition (the pm_lock of the port being released, or the cache_decision_mutex
 * taken by cache_sync_global() from inside the enable). */
static int cache_both_ports_held_samples;
static void sample_cache_holders_on_lock(void)
{
    if (port_manager_get_cache(0) && port_manager_get_cache(1)) {
        cache_both_ports_held_samples++;
    }
}

void test_set_cache_move_never_leaves_both_ports_enabled(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    port_manager_set_mode(1, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));

    cache_both_ports_held_samples = 0;
    mock_xSemaphoreTake_hook = sample_cache_holders_on_lock;
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(1, true));   /* move 1 -> 2 */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));   /* and back 2 -> 1 */
    mock_xSemaphoreTake_hook = NULL;

    TEST_ASSERT_EQUAL_MESSAGE(0, cache_both_ports_held_samples,
        "the overlay must never be held by two ports at once, not even mid-move");
    TEST_ASSERT_TRUE(port_manager_get_cache(0));
    TEST_ASSERT_FALSE(port_manager_get_cache(1));
}

/* B5, the exact failing scenario: the source sits on port 2 and the client applies its
 * per-port calls in fixed 1-then-2 order (the frontend does). The enable on port 1 moves
 * the source, and the disable on port 2 that follows must be a harmless no-op — it must
 * not take the cache back down. */
void test_set_cache_move_back_to_port1_in_port_order(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    port_manager_set_mode(1, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(1, true));

    /* Port 1 first: enable → takes the source over from port 2. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));
    mock_cache_multimaster_reset();
    mock_cache_multimaster_enabled = true;   /* restore live state after counter reset */
    mock_sniffer_reset();

    /* Port 2 second: disable → already released, nothing left to undo. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(1, false));

    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(0),
        "the trailing disable on the other port must not strip the new source");
    TEST_ASSERT_FALSE(port_manager_get_cache(1));
    TEST_ASSERT_TRUE(mock_cache_en[0]);
    TEST_ASSERT_FALSE(mock_cache_en[1]);
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_disable_called,
        "port 1 still wants the pool, so the no-op disable must not free it");
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_sniffer_disable_called[0],
        "the new source's data flow must stay wired");
}

/* Re-enabling the port that already holds the overlay is NOT a no-op — it re-writes the
 * NVS key and re-arms the sniffer, exactly as port_manager.h documents. What it must not
 * do is CYCLE the pool: cache_multimaster_enable() wipes the pool it (re)initialises, so
 * a redundant free/realloc would silently throw the accumulated values away. That, and
 * only that, is what this test pins. */
void test_set_cache_enable_on_current_holder_does_not_cycle_the_pool(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));
    mock_cache_multimaster_reset();
    mock_cache_multimaster_enabled = true;   /* restore live state after counter reset */
    mock_sniffer_reset();
    int save_bool_before = mock_setting_items_save_bool_called;

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));

    TEST_ASSERT_TRUE(port_manager_get_cache(0));
    TEST_ASSERT_FALSE(port_manager_get_cache(1));
    TEST_ASSERT_TRUE(mock_cache_en[0]);
    TEST_ASSERT_FALSE(mock_cache_en[1]);
    /* The pool is untouched — neither freed nor re-initialised. */
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_disable_called,
        "re-enabling the current holder must not free the live pool");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_enable_called,
        "re-enabling the current holder must not wipe the live pool");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_clear_called,
        "nothing was moved, so the accumulated values must not be dropped either");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_sniffer_disable_called[0],
        "re-enabling the current holder must not unwire its data flow");
    /* But the redundant work IS done, and the name must not pretend otherwise: exactly
     * one NVS write (this port's own key, no release write for the other port) and the
     * sniffer re-armed. */
    TEST_ASSERT_EQUAL_MESSAGE(save_bool_before + 1, mock_setting_items_save_bool_called,
        "the current holder's NVS key is rewritten, and only it");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_sniffer_enable_called[0],
        "the sniffer is re-armed on the port that already holds the overlay");
    TEST_ASSERT_EQUAL(SNIFF_REASON_CACHE, mock_sniffer_enable_last_reason[0]);
}

/* persist-6, the release half of a move: a move writes TWO NVS keys, and a failure on
 * either one must reach the caller. This isolates the RELEASED port's write — the
 * target's own write succeeds, so the only thing that can make the call non-OK is the
 * release result being propagated. Without that propagation the client is told the move
 * is durable when it is not: the old holder's key still reads "enabled", and if the move
 * had gone the other way (to the higher-index port) the boot-time normalisation in
 * port_manager_init(), which keeps the LOWEST-index port that asks, would hand the
 * overlay straight back. */
void test_set_cache_move_surfaces_a_failed_release_write(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    port_manager_set_mode(1, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(1, true));
    TEST_ASSERT_TRUE(mock_cache_en[1]);

    /* Only the released port's key fails; the target's key still writes fine. */
    mock_setting_items_save_bool_fail_key = KEY_CACHE_EN_2;

    esp_err_t ret = port_manager_set_cache(0, true);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, ret,
        "a failed NVS write on the RELEASED port must be surfaced, not swallowed");
    /* The move itself still happened live, on both ports. */
    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(0),
        "the target must still hold the overlay in memory despite the release write failing");
    TEST_ASSERT_FALSE_MESSAGE(port_manager_get_cache(1),
        "the old holder must still be released in memory");
    TEST_ASSERT_TRUE_MESSAGE(mock_cache_en[0],
        "the target's own NVS key was written and must not be rolled back");
    /* ...but the released port's key was NOT rewritten — which is exactly the durability
     * gap the non-OK return is there to report. */
    TEST_ASSERT_TRUE_MESSAGE(mock_cache_en[1],
        "the released port's NVS key must be left stale by the failed write");
}

void test_set_cache_disable_on_non_holder_leaves_the_holder_alone(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));
    mock_cache_multimaster_reset();
    mock_cache_multimaster_enabled = true;   /* restore live state after counter reset */

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(1, false));

    TEST_ASSERT_TRUE(port_manager_get_cache(0));
    TEST_ASSERT_TRUE(mock_cache_en[0]);
    TEST_ASSERT_EQUAL(0, mock_cache_multimaster_disable_called);
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);
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

/* ═══════════════════════════════════════════════════════════════════════════
 * 6b. port_manager_apply_cache_settings() — the runtime apply of a POST /settings
 *
 * POST /settings maps rs485_N.cache_en straight onto the NVS keys cache_en_N
 * (settings_manager.c's rs485_base_mappings) and stops there. Until this entry point
 * existed nothing moved the runtime overlay to match, and nothing at runtime ever
 * reconciled the two: a device answered /settings with rs485_2.cache_en true, /info with
 * rs485_1.cache_enabled true, and /cache/status with packets_processed stuck at 0 while
 * port 2 carried traffic — for the rest of its uptime. Only a POST /ports/N/cache or a
 * reboot (through the boot normalisation) fixed it.
 *
 * These tests drive mock_cache_en[] directly: it IS the stored value, so writing it is
 * exactly what a settings write leaves behind for this function to find.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* What a POST /settings leaves in NVS, and nothing else. */
static void write_cache_en_nvs(bool port1, bool port2)
{
    mock_cache_en[0] = port1;
    mock_cache_en[1] = port2;
}

/* The single-port invariant, restated as something a test can count. */
static unsigned cache_overlay_holders(void)
{
    unsigned held = 0;
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        if (port_manager_get_cache(i)) {
            held++;
        }
    }
    return held;
}

/* THE regression test: the exact hardware scenario. Caching runs on port 1; the user moves it
 * to port 2 with a single POST /settings; the runtime overlay must follow. Without the apply
 * the two stayed apart permanently — NVS on port 2, the sniffer on port 1, nothing recorded. */
void test_apply_cache_settings_moves_the_overlay_to_the_port_nvs_names(void)
{
    /* Both ports open, so each one's live data flow is observable. */
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    port_manager_set_mode(1, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));
    mock_sniffer_reset();
    mock_cache_multimaster_reset();
    mock_cache_multimaster_enabled = true;   /* restore live state after counter reset */

    /* POST /settings {"rs485_1":{"cache_en":false},"rs485_2":{"cache_en":true}} */
    write_cache_en_nvs(false, true);

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_cache_settings());

    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(1),
        "the runtime overlay must end up on the port NVS names");
    TEST_ASSERT_FALSE_MESSAGE(port_manager_get_cache(0),
        "and must not stay on the port the settings write moved it away from");
    TEST_ASSERT_EQUAL_MESSAGE(1, cache_overlay_holders(),
        "exactly one port may carry the overlay — the pool is keyed by slave_id with no port "
        "dimension, so two feeding buses collide on same-address slaves (review #51)");
    /* The stored side is left consistent with the runtime one, so the next boot agrees. */
    TEST_ASSERT_FALSE(mock_cache_en[0]);
    TEST_ASSERT_TRUE(mock_cache_en[1]);
    /* The live data flow followed the overlay — this is what "not one packet reached the
     * cache" was about: the sniffer's CACHE reason stayed armed on the wrong port. */
    TEST_ASSERT_EQUAL(1, mock_sniffer_disable_called[0]);
    TEST_ASSERT_EQUAL(SNIFF_REASON_CACHE, mock_sniffer_disable_last_reason[0]);
    TEST_ASSERT_EQUAL(1, mock_sniffer_enable_called[1]);
    TEST_ASSERT_EQUAL(SNIFF_REASON_CACHE, mock_sniffer_enable_last_reason[1]);
    /* The single-sync design (C9) must survive the new entry point: a port wants the pool
     * before and after, so the one sync sees no transition — no free, no 32 KB realloc. */
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_disable_called,
        "a settings-driven move must not free the pool — it is wanted before and after");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_enable_called,
        "nor reallocate the pool it never freed");
    TEST_ASSERT_TRUE_MESSAGE(mock_cache_multimaster_enabled,
        "the cache must stay on across the move, not blink off and back");
    /* The CONTENTS still go: they describe the bus the cache was moved away from. */
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_clear_called,
        "a settings-driven move must drop the values read from the old bus");
}

/* The other direction, and the factory-reset path: set_default_settings() writes cache_en_N
 * false and calls settings_update(). Without the apply the cache kept running on a device the
 * user had just reset. */
void test_apply_cache_settings_drops_the_overlay_when_no_port_asks_for_it(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));
    mock_sniffer_reset();
    mock_cache_multimaster_reset();
    mock_cache_multimaster_enabled = true;   /* restore live state after counter reset */

    write_cache_en_nvs(false, false);

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_cache_settings());

    TEST_ASSERT_EQUAL_MESSAGE(0, cache_overlay_holders(),
        "no port asks for the overlay, so no port may keep it");
    TEST_ASSERT_EQUAL(1, mock_sniffer_disable_called[0]);
    TEST_ASSERT_EQUAL(SNIFF_REASON_CACHE, mock_sniffer_disable_last_reason[0]);
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_disable_called,
        "the last holder gave the overlay up, so the 32 KB pool must be freed");
    TEST_ASSERT_FALSE(mock_cache_multimaster_enabled);
}

/* And enabling from nothing, which is the one transition that really does allocate the pool. */
void test_apply_cache_settings_enables_the_overlay_from_no_holder(void)
{
    port_manager_set_mode(1, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL_MESSAGE(0, cache_overlay_holders(), "precondition: nothing is caching");
    mock_sniffer_reset();
    mock_cache_multimaster_reset();

    write_cache_en_nvs(false, true);

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_cache_settings());

    TEST_ASSERT_TRUE(port_manager_get_cache(1));
    TEST_ASSERT_EQUAL(1, cache_overlay_holders());
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_enable_called,
        "want false -> true is the transition that allocates the pool");
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);
    TEST_ASSERT_EQUAL(1, mock_sniffer_enable_called[1]);
    TEST_ASSERT_EQUAL(SNIFF_REASON_CACHE, mock_sniffer_enable_last_reason[1]);
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_clear_called,
        "nothing was moved off another bus, so there are no stale values to drop");
}

/* Every POST /settings comes through here, including the ones that only change the Wi-Fi
 * password. When the runtime already matches NVS this must do NOTHING — above all it must not
 * clear() the pool, which would throw the accumulated register values away on an unrelated
 * settings write, and must not rewrite the NVS key or re-arm the sniffer. */
void test_apply_cache_settings_leaves_a_matching_overlay_completely_alone(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));
    mock_sniffer_reset();
    mock_cache_multimaster_reset();
    mock_cache_multimaster_enabled = true;   /* restore live state after counter reset */
    int save_bool_before = mock_setting_items_save_bool_called;

    /* NVS says what the runtime already does. */
    write_cache_en_nvs(true, false);

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_cache_settings());

    TEST_ASSERT_TRUE(port_manager_get_cache(0));
    TEST_ASSERT_EQUAL(1, cache_overlay_holders());
    TEST_ASSERT_EQUAL_MESSAGE(save_bool_before, mock_setting_items_save_bool_called,
        "an unrelated settings write must not rewrite the cache_en keys");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_clear_called,
        "and must not drop the accumulated values — this runs on EVERY POST /settings");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_enable_called,
        "nor cycle the pool");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_disable_called, "in either direction");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_sniffer_enable_called[0],
        "nor re-arm a sniffer reason that is already armed");
    TEST_ASSERT_EQUAL(0, mock_sniffer_disable_called[0]);
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);
}

/* One request can set cache_en on BOTH ports — settings_manager writes the two keys
 * independently and knows nothing about the invariant. The move alone would not fix that: it
 * releases by RUNTIME overlay, and the loser may not hold it, so its stored key would survive
 * and /settings would keep reporting caching on a port /info reports as idle — the same
 * divergence, just stored instead of live. Lowest index wins, matching the boot normalisation,
 * so a reboot cannot reinterpret the result. */
void test_apply_cache_settings_normalises_a_request_that_enabled_both_ports(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    port_manager_set_mode(1, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(1, true));   /* port 2 is the holder */
    mock_sniffer_reset();

    write_cache_en_nvs(true, true);

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_cache_settings());

    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(0),
        "lowest index wins, exactly as the boot normalisation resolves the same NVS");
    TEST_ASSERT_EQUAL_MESSAGE(1, cache_overlay_holders(),
        "a request that asked for both ports must still leave exactly one holder");
    TEST_ASSERT_TRUE(mock_cache_en[0]);
    TEST_ASSERT_FALSE_MESSAGE(mock_cache_en[1],
        "the surplus stored key must be cleared too, or the next boot hands the overlay back "
        "and /settings keeps reporting caching on a port that is not caching");
    TEST_ASSERT_EQUAL(1, mock_sniffer_disable_called[1]);
    TEST_ASSERT_EQUAL(1, mock_sniffer_enable_called[0]);
}

/* Same shape, without a move to hide behind: the runtime already holds the port NVS asks for,
 * and the only thing wrong is the surplus key. The "nothing to do" fast path must not skip it. */
void test_apply_cache_settings_clears_a_surplus_key_without_moving_anything(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));
    mock_sniffer_reset();
    mock_cache_multimaster_reset();
    mock_cache_multimaster_enabled = true;   /* restore live state after counter reset */

    write_cache_en_nvs(true, true);

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_cache_settings());

    TEST_ASSERT_TRUE(port_manager_get_cache(0));
    TEST_ASSERT_EQUAL(1, cache_overlay_holders());
    TEST_ASSERT_FALSE_MESSAGE(mock_cache_en[1],
        "the surplus key is cleared even when the holder does not change");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_clear_called,
        "nothing moved between buses, so the accumulated values stay");
}

/* C9's failure mode reaches this entry point too: the pool would not allocate, the overlay is
 * recorded and the cache is NOT running. Reported, not swallowed — POST /settings turns it into
 * a response warning, and nothing else would ever tell the user. */
void test_apply_cache_settings_surfaces_a_failed_pool_allocation(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    mock_cache_multimaster_enable_should_fail = true;

    write_cache_en_nvs(true, false);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_NO_MEM, port_manager_apply_cache_settings(),
        "a failed pool allocation must reach the caller, not be reported as success");
    TEST_ASSERT_FALSE_MESSAGE(mock_cache_multimaster_enabled, "the cache is off");
    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(0),
        "the intent is still recorded, which is what keeps want true for the next retry");
}

/* A surplus key that will not clear is a persistence failure like any other (persist-6): the
 * runtime is right, the stored state is not, and a reboot would hand the overlay back. */
void test_apply_cache_settings_surfaces_a_failed_surplus_clear(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));

    write_cache_en_nvs(true, true);
    mock_setting_items_save_bool_fail_key = KEY_CACHE_EN_2;

    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, port_manager_apply_cache_settings(),
        "a stored key that still names a second port must not be reported as success");
    TEST_ASSERT_TRUE_MESSAGE(mock_cache_en[1],
        "the write really did fail — the assertion above cannot pass for the wrong reason");
    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(0),
        "and the runtime side is still correct");
    TEST_ASSERT_EQUAL(1, cache_overlay_holders());
}

/* Precedence, same rule as port_manager_set_cache()'s own: "the cache is not running" outranks
 * "the stored state is stale". The caller acts on the worse of the two. */
void test_apply_cache_settings_pool_failure_outranks_a_failed_surplus_clear(void)
{
    write_cache_en_nvs(true, true);
    mock_setting_items_save_bool_fail_key = KEY_CACHE_EN_2;
    mock_cache_multimaster_enable_should_fail = true;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_NO_MEM, port_manager_apply_cache_settings(),
        "with both failing, the pool failure must win — it is the one a retry can fix");
    TEST_ASSERT_TRUE_MESSAGE(mock_cache_en[1], "the surplus key really did stay set");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_enable_called,
        "and the allocation really was attempted");
}

/* An out-of-range index cannot come from NVS, but the reverse can: an overlay recorded on a
 * port whose serial is closed. The apply must still record the intent, so the port comes up
 * caching when it is next brought up (port_init_mode() arms the reason from this flag). */
void test_apply_cache_settings_records_the_overlay_on_a_disabled_port(void)
{
    /* Both ports DISABLED — the state during the factory clock_out test, among others. */
    write_cache_en_nvs(false, true);

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_cache_settings());

    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(1),
        "the intent is recorded even with no serial to wire it into");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_sniffer_enable_called[1],
        "and nothing is armed on a port that has no descriptor");

    /* Bringing the port up now arms it, with no second settings write needed. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode(1, PM_MODE_PASSIVE));
    TEST_ASSERT_EQUAL(1, mock_sniffer_enable_called[1]);
    TEST_ASSERT_EQUAL(SNIFF_REASON_CACHE, mock_sniffer_enable_last_reason[1]);
}

void test_init_normalises_dual_cache_nvs(void)
{
    /* Legacy NVS may carry the overlay on BOTH ports. A boot must normalise to a single port
     * (lowest index wins) and rewrite the loser's NVS to false, so the single-port invariant
     * holds from boot. Reached here through port_manager_init(), which calls
     * port_manager_init_subsystems() where the normalisation actually lives — that WHERE is
     * pinned separately by test_cache_overlay_is_normalised_before_httpd_starts(). */
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

/* C9, the core of it: a pool allocation that fails must not come back as success.
 * The overlay is recorded (in memory and in NVS) but nothing is being cached — the
 * sniffer is armed and decoding, every on_request/on_response bails on
 * !cache_multimaster_is_enabled(), and /cache/status says enabled:false. A caller told
 * ESP_OK here has no way to find that out except by polling another endpoint. */
void test_set_cache_surfaces_a_failed_pool_allocation(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    mock_cache_multimaster_enable_should_fail = true;

    esp_err_t ret = port_manager_set_cache(0, true);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_NO_MEM, ret,
        "a failed pool allocation must reach the caller, not be reported as success");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_enable_called,
        "the allocation must actually have been attempted");
    TEST_ASSERT_FALSE_MESSAGE(mock_cache_multimaster_enabled,
        "the cache must be OFF after a failed enable");
    /* The intent is still recorded — that is what makes the retry below possible, and
     * it is also the divergence the caller has to be told about: /info reports this
     * port's cache_enabled as true while /cache/status reports the cache as disabled. */
    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(0),
        "the overlay intent is still recorded on the port");
    TEST_ASSERT_TRUE(mock_cache_en[0]);
}

/* The self-healing half of the same story: nothing retries the allocation on a timer,
 * but the overlay flag keeps want true, so the NEXT sync tries again. A repeated
 * POST /ports/N/cache is the cheapest trigger; a port re-init (POST /ports/N/mode,
 * apply_settings) and a reboot go through port_init_mode()'s sync and work the same. */
void test_set_cache_retries_the_pool_allocation_after_a_failure(void)
{
    port_manager_set_mode(0, PM_MODE_PASSIVE);
    mock_cache_multimaster_enable_should_fail = true;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, port_manager_set_cache(0, true));

    /* Memory freed up in the meantime; the client repeats the request. */
    mock_cache_multimaster_enable_should_fail = false;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, port_manager_set_cache(0, true),
        "the repeated request must retry the allocation, not short-circuit on the "
        "overlay already being set");
    TEST_ASSERT_TRUE_MESSAGE(mock_cache_multimaster_enabled,
        "the second attempt must actually bring the pool up");
    TEST_ASSERT_EQUAL(2, mock_cache_multimaster_enable_called);
}

/* When BOTH failures happen at once, WHICH code comes back is the API contract, not an
 * implementation detail. The rule: a pool failure OUTRANKS a persistence failure. It is
 * the worse news — "the cache is not running at all" against "it is running but will not
 * survive a reboot" — and it is the only one of the two that a retry can fix. The handler
 * turns it into 503 "come back later, the allocation is attempted again"; the persistence
 * failure becomes 500 "repeating this will not help". Return them the other way round and
 * a client that could have had its cache back by repeating the request is told not to. */
void test_set_cache_pool_failure_outranks_a_persist_failure(void)
{
    mock_cache_multimaster_enable_should_fail = true;
    mock_setting_items_save_bool_should_fail = true;

    esp_err_t ret = port_manager_set_cache(0, true);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_NO_MEM, ret,
        "with both failing, the pool failure must win — the NVS error would be answered "
        "with a 500 that tells the client retrying cannot help, and here it can");
    /* Both really did fail, so the assertion above cannot pass for the wrong reason. */
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_enable_called,
        "the pool allocation was really attempted");
    TEST_ASSERT_FALSE_MESSAGE(mock_cache_en[0],
        "and the NVS write really did fail, so there were two errors to choose from");
    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(0),
        "the live overlay is applied either way — it is what keeps want true for the retry");
}

/* The same rule against the other persistence failure a move can produce: the write that
 * RELEASES the old holder. Masked for the same reason, but with a consequence the plain
 * case does not have — the retry that finally gets the pool will not rewrite the released
 * port's key (its in-memory overlay is already false, so the release loop no longer runs),
 * so that retry answers 200 while NVS still names the old port, and the boot-time
 * normalisation — lowest index wins, see the loop in port_manager_init() — hands the
 * overlay back. port_manager_set_cache() logs the masked error for exactly that reason.
 * What is pinned here is only the returned code; the log is not observable from a test. */
void test_set_cache_pool_failure_outranks_a_failed_release_write(void)
{
    /* Port 1 holds the overlay with the pool off — the state an earlier OOM leaves behind. */
    mock_cache_multimaster_enable_should_fail = true;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, port_manager_set_cache(0, true));
    TEST_ASSERT_TRUE(mock_cache_en[0]);

    /* Now move it to port 2, with the RELEASED port's NVS key failing to write. */
    mock_setting_items_save_bool_fail_key = KEY_CACHE_EN_1;

    esp_err_t ret = port_manager_set_cache(1, true);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_NO_MEM, ret,
        "the pool failure must outrank the failed release write too: the cache not "
        "running is worse than a stored key that still names the old port");
    TEST_ASSERT_TRUE_MESSAGE(mock_cache_en[0],
        "the released port's stored key really did stay set — the failure being masked");
    TEST_ASSERT_TRUE_MESSAGE(mock_cache_en[1], "while the new holder's key was written");
    TEST_ASSERT_FALSE_MESSAGE(port_manager_get_cache(0),
        "the move is live: the old holder no longer has the overlay in memory");
    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(1), "and the new one does");
}

/* A port re-init is the other self-heal trigger, and it must NOT turn a failed pool
 * allocation into a failed mode change: port_init_mode()'s return value is what
 * port_set_mode_impl() rolls the port back on, so propagating it here would tear a
 * working UART down over a 32 KB overlay. Log and continue, exactly like the subsystem
 * init in port_manager_init(). */
void test_set_mode_is_not_failed_by_an_unavailable_cache_pool(void)
{
    /* Port still DISABLED, so this only records the intent and brings the pool up. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);

    /* The pool goes away behind port_manager's back (the device is out of memory by the
     * time the port is re-initialised) and cannot be got back. */
    mock_cache_multimaster_reset();
    mock_cache_multimaster_enable_should_fail = true;

    esp_err_t ret = port_manager_set_mode(0, PM_MODE_PASSIVE);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "an unavailable cache pool must not fail (and roll back) a transport-mode change");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "the port must come up in the requested mode");
    TEST_ASSERT_FALSE_MESSAGE(mock_cache_multimaster_enabled,
        "and it must come up with the cache off, not with a cache it does not have");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_sniffer_enable_called[0],
        "the CACHE reason is armed anyway, so a later successful retry needs no re-arm");
}

/* Same policy at boot: the pool that would not allocate is logged, the port comes up in
 * its stored mode anyway, and the overlay is kept so the next sync retries it.
 *
 * port_manager_init()'s return code is deliberately NOT asserted here. A failed pool
 * never reaches it: port_init_mode() swallows the sync error by design, so it returns
 * ESP_OK in this scenario and the assertion would hold no matter what the caller did with
 * that value — it pinned nothing. The property worth pinning, that a port which really
 * does fail to come up is still not a boot failure, needs a port that really fails, and
 * is pinned by test_init_continues_when_a_port_fails_to_come_up() below. */
void test_init_brings_ports_up_when_the_cache_pool_is_unavailable(void)
{
    mock_cache_en[0] = true;                       /* NVS says port 1 is the cache source */
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    mock_cache_multimaster_enable_should_fail = true;

    (void)port_manager_init();

    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "the port must come up regardless — routing Modbus is what the device is for");
    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(0),
        "the stored overlay is kept, so the next sync retries the allocation");
    TEST_ASSERT_FALSE_MESSAGE(mock_cache_multimaster_enabled,
        "the cache itself is off until that retry succeeds");
}

/* The port loop's log-and-continue policy, with a port that genuinely fails to
 * initialise. Both halves matter: the loop must go on to the next port, and the failure
 * must not leave port_manager_init(). main.c keeps the result in pm_ret, so an error
 * escaping here would be one ESP_ERROR_CHECK away from rebooting a gateway because one
 * RS-485 port could not be opened — taking the other, working port down with it. */
void test_init_continues_when_a_port_fails_to_come_up(void)
{
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    mock_setting_items_set_port_mode(1, PORT_MODE_TCP_BRIDGE_STR);
    /* Fails bridge_port_init_serial_only() only, so port 1 (PASSIVE) dies and port 2
     * (TCP_BRIDGE, a different init path) still comes up. */
    mock_bridge_port_init_serial_only_should_fail = true;

    esp_err_t ret = port_manager_init();

    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(0),
        "the port whose init failed must stay down, not be recorded as running");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_TCP_BRIDGE, port_manager_get_mode(1),
        "the loop must carry on to the next port instead of returning on the first error");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "one dead port must not be reported as a port-manager failure: main.c would "
        "abort the boot on it and take the working port down too");
}

/* The REST layer must not answer 200 with cache_enabled:true for a cache that is not
 * running (C9). It must also not answer 400: the body was fully validated above, so
 * this is the device failing, not the client. 503 specifically, because the allocation
 * is transient — repeating this very request retries it (see the retry test above). */
void test_set_cache_handler_reports_a_failed_pool_allocation_as_503(void)
{
    mock_json_utils_reset();
    mock_cache_multimaster_enable_should_fail = true;

    httpd_req_t req = {0};
    mock_json_utils_inject_enabled(true);

    TEST_ASSERT_EQUAL(ESP_OK, port_set_cache_handler(&req, 0));

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_json_utils_send_response_called,
        "a cache that could not be enabled must not be answered with 200 cache_enabled:true");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_json_utils_send_error_called,
        "the failure must be reported to the client");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("503 Service Unavailable",
        mock_json_utils_send_error_last_status,
        "out of memory is the device failing to serve a valid request, not a bad request");
}

/* The other server-side outcome through the same handler: the overlay is live but could
 * not be persisted (persist-6). Also not the client's fault — 500, and distinct from the
 * 503 above so an integrator can tell "retry" from "this will not fix itself". */
void test_set_cache_handler_reports_a_failed_persist_as_500(void)
{
    mock_json_utils_reset();
    mock_setting_items_save_bool_should_fail = true;

    httpd_req_t req = {0};
    mock_json_utils_inject_enabled(true);

    TEST_ASSERT_EQUAL(ESP_OK, port_set_cache_handler(&req, 0));

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_json_utils_send_response_called,
        "a change that will not survive a reboot must not be reported as a plain success");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_json_utils_send_error_called, "it must be reported");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("500 Internal Server Error",
        mock_json_utils_send_error_last_status,
        "a failed NVS write is a server-side fault, not a bad request");
}

/* The success path of the same handler, so the 503/500 tests above are proved to be
 * measuring the failure rather than a handler that errors on everything. */
void test_set_cache_handler_reports_success(void)
{
    mock_json_utils_reset();

    httpd_req_t req = {0};
    mock_json_utils_inject_enabled(true);

    TEST_ASSERT_EQUAL(ESP_OK, port_set_cache_handler(&req, 0));

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_json_utils_send_response_called,
        "a cache that really came up must be answered with 200");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_json_utils_send_error_called, "and with no error");
    TEST_ASSERT_TRUE(port_manager_get_cache(0));
    TEST_ASSERT_TRUE(mock_cache_multimaster_enabled);
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
     * equals ANY other local listener's port — a bridge gateway, or httpd on web_port —
     * every one of them now binds the same dual-stack address, so lwIP's tcp_listen()
     * really does return ERR_USE and tcp_server_init() returns ESP_FAIL, and a reboot does
     * not clear it (against a bridge it only swaps which of the two loses the port;
     * against httpd the cache server loses again, since httpd is started first). Both
     * ports are configured here, since the abort took every one of them down, not just the
     * port that carries the cache. */
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
 * 232) and passes only because the macro name happens to be followed by ',' or ':' there
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

/* Load a production .c file into the caller's buffer for the source-text checks below, or fail
 * the test trying, and hand it back with comments blanked. `path` is relative to the suite
 * directory and names the file in every failure message: the three below read identically
 * otherwise, and a CI log that does not say which file failed points the reader at the wrong one.
 *
 * The buffer belongs to the caller, one per file, so two files can be held at once: one shared
 * buffer would hand a test that read both the second file's text for BOTH pointers, and check
 * one file's anchors against the other with nothing to notice. */
static const char *read_c_source(const char *path, char *buf, size_t buf_size)
{
    char msg[256];
    FILE *f = fopen(path, "rb");
    snprintf(msg, sizeof(msg), "%s not readable — run this test from unittests/port_manager", path);
    TEST_ASSERT_NOT_NULL_MESSAGE(f, msg);
    size_t n = fread(buf, 1, buf_size - 1, f);
    fclose(f);
    buf[n] = '\0';
    snprintf(msg, sizeof(msg), "%s is empty", path);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, n, msg);
    /* A short read is the only proof the whole file arrived: fread() filling the buffer
     * to the brim looks exactly like a file that is one byte too long, and the anchors
     * in the callers would then be searched in a silently truncated copy — a missing
     * statement would read as "the call is gone" and a missing closing brace as "the call
     * is outside app_main()". Both are wrong answers, so refuse to answer at all. */
    snprintf(msg, sizeof(msg), "%s no longer fits in its buffer — grow it, do not trust a truncated read", path);
    TEST_ASSERT_LESS_THAN_MESSAGE(buf_size - 1, n, msg);
    blank_out_comments(buf);
    return buf;
}

static const char *read_main_c_source(void)
{
    static char src[64 * 1024];
    return read_c_source("../../main/main.c", src, sizeof(src));
}

static const char *read_port_manager_c_source(void)
{
    static char src[192 * 1024];
    return read_c_source("../../main/bridge/port_manager.c", src, sizeof(src));
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

/* Whitespace between an identifier and its parens is invisible to the compiler, so a guard matching
 * only the tight form is one space from being walked past; skip_ws() steps over it. find_no_arg_call()
 * returns the next `name()` at or after `from` in `src`, NULL if none, checking both ends of
 * the identifier: empty parens only, so a definition `void name(void)` is not a call to itself (a
 * `void name();` declaration in the .c would be), and no identifier char before, else `pm_name()`. */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static const char *find_no_arg_call(const char *src, const char *from, const char *name)
{
    const size_t name_len = strlen(name);

    for (const char *p = strstr(from, name); p != NULL; p = strstr(p + 1, name)) {
        if (p > src && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) continue;
        const char *q = skip_ws(p + name_len);
        if (*q == '(' && *skip_ws(q + 1) == ')') return p;
    }
    return NULL;
}

/* Is there an ESP_ERROR_CHECK in src whose argument text starts with `name`?
 *
 * The regression class this closes: matching the literal "ESP_ERROR_CHECK(port_manager_init())"
 * alone is trivial to walk past — `esp_err_t pm_ret = port_manager_init(); ESP_ERROR_CHECK(pm_ret);`
 * and `ESP_ERROR_CHECK( port_manager_init() )` abort the boot without ever containing it.
 *
 * What it still cannot prove: this is strstr(), not a parser. Comments no longer count —
 * read_main_c_source() blanks them — but a hit inside a string literal still does, and the same
 * abort reached through another macro, a helper, or `if (pm_ret != ESP_OK) abort();` does not.
 * The prefix match also fires on ESP_ERROR_CHECK(port_manager_init_subsystems()) — which main.c
 * avoids for the same reason, so that is a wanted hit rather than a false one. */
static bool esp_error_check_wraps(const char *src, const char *name)
{
    const char *macro = "ESP_ERROR_CHECK";
    const size_t name_len = strlen(name);

    for (const char *p = strstr(src, macro); p != NULL; p = strstr(p + 1, macro)) {
        const char *arg = skip_ws(p + strlen(macro));
        if (*arg != '(') continue;   /* ESP_ERROR_CHECK_WITHOUT_ABORT and friends */
        if (strncmp(skip_ws(arg + 1), name, name_len) == 0) return true;
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
 * 11. Lock creation — port_manager_locks_init()  (C4)
 *
 * Both the per-port init mutexes and the global cache-decision mutex used to be
 * created lazily inside the lock paths, under `if (h == NULL) h = create();`. That
 * is not atomic: two tasks that both read NULL each create a mutex, the second
 * assignment overwrites the first, and each task then holds a lock the other does
 * not respect — no mutual exclusion, plus a leaked semaphore. The window was a
 * boot-order property, not a theoretical one: main.c starts httpd, and arms the
 * config-button long-press callback, long before port_manager_init() leaves the
 * wait-for-network loop, and both routes reach these locks.
 *
 * The fix is to create every lock once, up front, in port_manager_locks_init() —
 * called from the top of port_manager_init_subsystems(), above its one-shot guard —
 * and to make the lock paths assert instead of create. The mutexes live in static
 * buffers, so creation cannot fail and there is no failure path left to test: the
 * asserts in the lock paths can only ever catch "locks_init() has not run".
 *
 * port_manager_reset_for_test() calls locks_init() so the other ~110 tests can take a
 * lock without booting the module. That convenience is NOT the coverage of this fix —
 * the tests below go through port_manager_init_subsystems(), the production entry
 * point, after clearing the handles it is supposed to create.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Every lock port_manager owns: one init mutex per port, then the cache-decision mutex, then the
 * cache-move mutex — created in that order, which the index arithmetic below relies on. */
#define PM_LOCK_COUNT (BRIDGES_COUNT + 2)

/* Sized by MOCK_STATIC_MUTEX_MAX, indexed below by PM_LOCK_COUNT: over-capacity calls are
 * counted but not stored, so a third port would read past the end with the count still passing. */
_Static_assert(PM_LOCK_COUNT <= MOCK_STATIC_MUTEX_MAX,
    "mock_xSemaphoreCreateMutexStatic_buffers[] needs one slot per port_manager lock — raise "
    "MOCK_STATIC_MUTEX_MAX in unittests/mocks/freertos/semphr.h");

void test_locks_init_creates_every_lock_up_front(void)
{
    port_manager_clear_locks_for_test();
    mock_freertos_semaphore_reset();

    port_manager_locks_init();

    TEST_ASSERT_EQUAL_MESSAGE(PM_LOCK_COUNT, mock_xSemaphoreCreateMutexStatic_called,
        "port_manager_locks_init() must create one init mutex per port plus the cache-decision "
        "and cache-move mutexes — every lock the module owns, before anything can take one");
    /* Static storage, so this is the only creation call the module may use: a dynamic
     * xSemaphoreCreateMutex() would be a creation that can fail, and a boot-path failure is
     * precisely what the static buffers exist to remove. */
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreCreateMutex_called,
        "the locks must be statically allocated — a heap mutex reintroduces a failure mode "
        "on the boot path that has no correct handling");

    /* One buffer per lock, checked pairwise: the mock returns the buffer as the handle, as the
     * real one does, so two locks on one buffer are one lock under two names. */
    for (int i = 0; i < PM_LOCK_COUNT; i++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(mock_xSemaphoreCreateMutexStatic_buffers[i],
            "every lock must be created from a static buffer of its own");
        for (int j = i + 1; j < PM_LOCK_COUNT; j++) {
            TEST_ASSERT_TRUE_MESSAGE(mock_xSemaphoreCreateMutexStatic_buffers[i] !=
                                     mock_xSemaphoreCreateMutexStatic_buffers[j],
                "each lock needs its own static buffer — sharing one makes two ports (or a port "
                "and the cache decision) serialise against the same mutex");
        }
    }

    /* Idempotent, and it has to be: port_manager_init_subsystems() calls it a second time
     * with httpd already up, where re-running xSemaphoreCreateMutexStatic() on a live
     * mutex's buffer would reinitialise a lock another task may be holding. */
    port_manager_locks_init();
    TEST_ASSERT_EQUAL_MESSAGE(PM_LOCK_COUNT, mock_xSemaphoreCreateMutexStatic_called,
        "a second port_manager_locks_init() must create nothing — handles that are already "
        "set must be left alone, not rebuilt on top of a possibly-held mutex");
}

/* The production wiring, which is the thing that actually has to hold: main.c never calls
 * port_manager_locks_init() itself, it calls port_manager_init_subsystems(). Deleting the
 * locks_init() call from that function must fail a test — before this one, it did not,
 * because every test reached locks_init() through port_manager_reset_for_test().
 *
 * Second half pins the placement ABOVE the one-shot guard, the other thing nothing
 * distinguished: with s_subsystems_ready already set, everything below the guard is skipped,
 * so only a call above it can still create anything. */
void test_init_subsystems_creates_the_locks_above_the_one_shot_guard(void)
{
    /* A device that has booted no further than app_main()'s first lines. */
    port_manager_clear_locks_for_test();
    mock_freertos_semaphore_reset();

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init_subsystems());

    TEST_ASSERT_EQUAL_MESSAGE(PM_LOCK_COUNT, mock_xSemaphoreCreateMutexStatic_called,
        "port_manager_init_subsystems() must create every port_manager lock — it is what "
        "main.c calls, and http_server_init() registers handlers that take them right after");

    /* The call above consumed the one-shot guard. Take the handles away again — nothing in
     * production can do this, which is exactly why the helper is test-only — and repeat. */
    port_manager_clear_locks_for_test();
    mock_freertos_semaphore_reset();

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init_subsystems());

    TEST_ASSERT_EQUAL_MESSAGE(PM_LOCK_COUNT, mock_xSemaphoreCreateMutexStatic_called,
        "port_manager_locks_init() must sit ABOVE the s_subsystems_ready guard: a call below "
        "it is skipped on every entry after the first, and port_manager_init() is one such entry");
}

/* Structural half of the C4 regression guard, and the half that actually discriminates.
 * The behavioural test below runs with every handle already set, so a reintroduced
 * `if (h == NULL) h = create();` in a lock path would not fire there and the suite would stay
 * green. Anchoring on the source text does not depend on the state a test happens to be in:
 * the creation call must appear inside port_manager_locks_init() and nowhere else.
 *
 * Same source-text exception, and the same limits, as the main.c checks above: comments are
 * blanked, string literals are not, and this is strstr() rather than a parser. What that leaves
 * uncovered is indirection: `static void (*f)(void) = port_manager_locks_init;` then `f();` in a
 * lock path is neither a create nor a call by that name, so neither loop sees it. The
 * "xSemaphoreCreateMutex" prefix covers the dynamic form and the Static one at once. */
void test_only_locks_init_creates_a_mutex(void)
{
    const char *src = read_port_manager_c_source();

    const char *fn = strstr(src, "\nvoid port_manager_locks_init(void)\n{");
    TEST_ASSERT_NOT_NULL_MESSAGE(fn,
        "port_manager.c must still define port_manager_locks_init() at file scope — the check "
        "below is bounded by that definition and means nothing without it");
    const char *fn_end = strstr(fn + 1, "\n}");
    TEST_ASSERT_NOT_NULL_MESSAGE(fn_end,
        "port_manager_locks_init() must still be a brace-terminated function at file scope");

    int inside = 0;
    for (const char *p = strstr(src, "xSemaphoreCreateMutex");
         p != NULL;
         p = strstr(p + 1, "xSemaphoreCreateMutex")) {
        TEST_ASSERT_TRUE_MESSAGE(p > fn && p < fn_end,
            "every mutex creation in port_manager.c must live inside port_manager_locks_init(): "
            "a create anywhere else — a lock path above all — is a create a second task can race, "
            "which is the C4 defect this replaced");
        inside++;
    }
    /* Otherwise a rename of the creation API would silently empty the loop and pass. */
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, inside,
        "port_manager_locks_init() must still create the mutexes — no creation call found at all");

    /* And exactly one call site, the hole the loop above leaves open: a lock path that runs
     * `if (h == NULL) port_manager_locks_init();` keeps every create inside locks_init() and still
     * races — both re-enter, the second re-initialising a live mutex as free. Only
     * port_manager_reset_for_test() is exempt, and only its body: the scan resumes past it to EOF. */
    const char *sub = strstr(src, "\nesp_err_t port_manager_init_subsystems(void)\n{");
    TEST_ASSERT_NOT_NULL_MESSAGE(sub,
        "port_manager.c must still define port_manager_init_subsystems() at file scope");
    const char *sub_end = strstr(sub + 1, "\n}");
    TEST_ASSERT_NOT_NULL_MESSAGE(sub_end,
        "port_manager_init_subsystems() must still be a brace-terminated function at file scope");
    const char *reset_fn = strstr(src, "\nvoid port_manager_reset_for_test(void)\n{");
    TEST_ASSERT_NOT_NULL_MESSAGE(reset_fn,
        "port_manager_reset_for_test() must still be defined — it bounds the exclusion below");
    const char *reset_fn_end = strstr(reset_fn + 1, "\n}");
    TEST_ASSERT_NOT_NULL_MESSAGE(reset_fn_end,
        "port_manager_reset_for_test() must still be a brace-terminated function at file scope");

    int calls = 0;
    for (const char *p = find_no_arg_call(src, src, "port_manager_locks_init");
         p != NULL;
         p = find_no_arg_call(src, p + 1, "port_manager_locks_init")) {
        if (p > reset_fn && p < reset_fn_end) continue;
        TEST_ASSERT_TRUE_MESSAGE(p > sub && p < sub_end,
            "port_manager_locks_init() must be called only from port_manager_init_subsystems(): "
            "a call from a lock path re-creates a mutex another task may be holding");
        calls++;
    }
    TEST_ASSERT_EQUAL_MESSAGE(1, calls,
        "port_manager_init_subsystems() must carry exactly one call to port_manager_locks_init()");
}

/* Behavioural companion to the structural check above. It cannot prove the absence of a lazy
 * create (its handles are already set, so a `if (h == NULL) h = create()` would not run); what
 * it does prove is that the locked paths are reachable with the locks merely pre-created — i.e.
 * that they take what locks_init() made and do not need anything else built for them. */
void test_locked_paths_run_on_pre_created_locks(void)
{
    /* The locks are up: setUp() -> port_manager_reset_for_test() -> locks_init(). */
    mock_freertos_semaphore_reset();

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_tx_disabled(0, true));
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_tx_disabled(1, true));
    port_manager_set_cache(0, true);
    port_manager_set_cache(1, true);
    port_manager_set_cache(1, false);

    /* Without this the test would also pass if the calls above stopped locking at all. */
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, mock_xSemaphoreTake_called,
        "the calls above must actually take the locks, or the assertion below proves nothing");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreCreateMutexStatic_called,
        "a locked path found every mutex missing and built one — with the locks already created "
        "there is nothing left for it to create");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreCreateMutex_called,
        "and no heap mutex either");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 12. The boot loop runs under pm_lock (C10)
 *
 * port_manager_init() sits behind main.c's wait-for-network loop, which http_server_init()
 * runs ABOVE: httpd answers POST /ports/N/mode and POST /settings for the whole of it. The
 * boot loop used to be the one caller of port_init_mode() that took no pm_lock.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* The mock keeps only the LAST handle taken, and this needs every one of them plus the
 * nesting depth each was taken at, so the hook logs them as they happen. */
#define TAKE_LOG_MAX 16
typedef struct {
    SemaphoreHandle_t handle;
    int               held_before;   /* locks already held when this take ran */
} take_record_t;
static take_record_t s_take_log[TAKE_LOG_MAX];
static int s_take_log_len;

/* Runs inside xSemaphoreTake(), after it has recorded the handle and BEFORE it bumps
 * mock_xSemaphore_held_count — so held_before is the depth this take nests at. */
static void log_lock_take(void)
{
    if (s_take_log_len < TAKE_LOG_MAX) {
        s_take_log[s_take_log_len].handle      = mock_xSemaphoreTake_Handle;
        s_take_log[s_take_log_len].held_before = mock_xSemaphore_held_count;
    }
    s_take_log_len++;
}

/* Structural half: the boot loop must take each port's own lock, and HOLD it across
 * port_init_mode().
 *
 * The depth assertion on the port locks is what pins "one at a time". pm_lock ->
 * cache_decision_mutex is the documented order and port_init_mode() -> cache_sync_global()
 * uses it, so a nested take is expected — but never a PORT lock nested inside anything, which
 * would be either two pm_locks held at once (the nesting port_manager_set_cache() documents as
 * deliberately avoided) or a pm_lock taken under the cache-decision mutex, i.e. that order
 * backwards.
 *
 * The cache-decision assertion is what pins the SPAN, and it is the one that discriminates.
 * "Each port's lock was taken at depth 0, and nothing stayed held" is satisfied just as well by
 * a loop that unlocks immediately after the two skip checks and runs port_init_mode() outside
 * the lock — which is exactly the defect this change closes (the duplicated lazy serial_lock in
 * transparent_tcp.c, and the second bridge_port_init_serial_only() for PM_MODE_PASSIVE, since
 * that one has no double-init guard). cache_sync_global() takes the cache-decision mutex
 * unconditionally at its top and is reached from inside port_init_mode(), so its take is a
 * probe placed in the middle of the critical section: held_before == 1 means the port lock was
 * still held there, held_before == 0 means it had already been given back. */
void test_init_brings_each_port_up_under_its_own_lock(void)
{
    /* Captured before any counter reset: the mock hands back the static buffer it was given,
     * in creation order — one per port, then the cache-decision mutex. */
    SemaphoreHandle_t port_lock[BRIDGES_COUNT];
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        port_lock[i] = (SemaphoreHandle_t)mock_xSemaphoreCreateMutexStatic_buffers[i];
        TEST_ASSERT_NOT_NULL_MESSAGE(port_lock[i],
            "setUp() must have created the per-port locks, or this test compares against NULL");
    }
    SemaphoreHandle_t cache_lock =
        (SemaphoreHandle_t)mock_xSemaphoreCreateMutexStatic_buffers[BRIDGES_COUNT];
    TEST_ASSERT_NOT_NULL_MESSAGE(cache_lock,
        "setUp() must have created the cache-decision mutex — it is created last, right after "
        "the per-port locks, and it is what the span assertion below probes with");

    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    mock_setting_items_set_port_mode(1, PORT_MODE_REPEATER_STR);

    s_take_log_len = 0;
    mock_xSemaphoreTake_hook = log_lock_take;
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());
    mock_xSemaphoreTake_hook = NULL;

    /* Both ports really came up, or "no unlocked init" would hold trivially. */
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "port 1 must have been brought up, or the lock assertions below cover nothing");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_REPEATER, port_manager_get_mode(1),
        "port 2 must have been brought up, or the lock assertions below cover nothing");

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        bool taken = false;
        for (int n = 0; n < s_take_log_len && n < TAKE_LOG_MAX; n++) {
            if (s_take_log[n].handle != port_lock[i]) {
                continue;
            }
            taken = true;
            TEST_ASSERT_EQUAL_MESSAGE(0, s_take_log[n].held_before,
                "a port lock must be taken with nothing else held: the two pm_locks are never "
                "nested, and pm_lock under the cache-decision mutex is the documented order "
                "backwards");
        }
        TEST_ASSERT_TRUE_MESSAGE(taken,
            "the boot loop must bring each port up under that port's own pm_lock — httpd is "
            "already answering POST /ports/N/mode by the time this runs");
    }

    /* One cache-decision take per port brought up, each from inside port_init_mode(), each
     * with that port's pm_lock still held. Both halves are asserted: the count, so a loop that
     * stopped calling port_init_mode() altogether cannot pass by taking the mutex zero times,
     * and the depth, so a loop that calls it outside the lock cannot pass either. */
    int cache_takes = 0;
    for (int n = 0; n < s_take_log_len && n < TAKE_LOG_MAX; n++) {
        if (s_take_log[n].handle != cache_lock) {
            continue;
        }
        cache_takes++;
        TEST_ASSERT_EQUAL_MESSAGE(1, s_take_log[n].held_before,
            "pm_lock must be held ACROSS port_init_mode(), not just around the skip checks: "
            "cache_sync_global() is called from inside it, so its take must nest one deep. At "
            "depth 0 the port comes up unlocked — two concurrent entries create two lazy "
            "serial_locks in transparent_tcp.c, and PM_MODE_PASSIVE gets a second "
            "bridge_port_init_serial_only() with no double-init guard to stop it");
    }
    TEST_ASSERT_EQUAL_MESSAGE((int)BRIDGES_COUNT, cache_takes,
        "port_init_mode() must have run once per port, inside the lock — no take of the "
        "cache-decision mutex means no port was actually brought up in the loop");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphore_held_count,
        "every lock the boot loop took must be released again — a pm_lock left held deadlocks "
        "the first REST request that touches that port");
}

/* Assert that everything s_take_log recorded happened INSIDE one critical section of
 * outer_lock: it is the first take, at depth 0, and every later take nests below it. */
static void assert_move_is_covered_by(SemaphoreHandle_t outer_lock, const char *what)
{
    char msg[160];

    snprintf(msg, sizeof(msg), "%s must take at least the outer lock and one pm_lock", what);
    TEST_ASSERT_GREATER_THAN_MESSAGE(1, s_take_log_len, msg);
    TEST_ASSERT_TRUE_MESSAGE(s_take_log_len <= TAKE_LOG_MAX,
        "the take log overflowed — raise TAKE_LOG_MAX or the assertions below miss takes");

    snprintf(msg, sizeof(msg),
             "%s must take the cache-move mutex FIRST, before any pm_lock: it is the outermost "
             "of the three, and taking it under a pm_lock inverts the documented order", what);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(outer_lock, s_take_log[0].handle, msg);
    TEST_ASSERT_EQUAL_MESSAGE(0, s_take_log[0].held_before,
        "and it must be taken with nothing else held");

    for (int n = 1; n < s_take_log_len; n++) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(outer_lock, s_take_log[n].handle,
            "the outer lock must be taken exactly once per operation — a second take means it "
            "was released mid-move, and the release-then-enable window is unguarded again");
        snprintf(msg, sizeof(msg),
                 "every lock %s takes must nest INSIDE the cache-move mutex — a take at depth 0 "
                 "means the outer lock was already given back, so the move is not atomic", what);
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, s_take_log[n].held_before, msg);
    }
}

/* Both cache entry points must serialise on ONE outer lock, and that is a structural property
 * a single-threaded test can still pin.
 *
 * Why it needs a lock at all: a move releases the old holder under one pm_lock and enables the
 * new one under a different pm_lock, taken afterwards, so its check-then-act is not atomic. Two
 * concurrent movers can both find no holder and both enable — two buses feeding one port-blind
 * pool. That used to be excluded by a premise instead of a lock ("the only caller is the REST
 * handler, on the single esp_http_server request task"), and POST /settings retired it:
 * settings_update() reaches port_manager_apply_cache_settings() from the httpd task AND from the
 * config-button task (factory reset on a long press, main.c).
 *
 * What is asserted: the FIRST lock each entry point takes is the same handle, taken at depth 0,
 * and every lock either of them takes afterwards nests inside it. A second lock at depth 0 would
 * mean the outer one had already been given back — i.e. the move is not covered end to end. */
void test_both_cache_entry_points_serialise_on_one_outer_lock(void)
{
    /* Created last by port_manager_locks_init(), right after the cache-decision mutex. */
    SemaphoreHandle_t move_lock =
        (SemaphoreHandle_t)mock_xSemaphoreCreateMutexStatic_buffers[BRIDGES_COUNT + 1];
    TEST_ASSERT_NOT_NULL_MESSAGE(move_lock,
        "setUp() must have created the cache-move mutex — it is the last lock locks_init() makes");

    port_manager_set_mode(0, PM_MODE_PASSIVE);
    port_manager_set_mode(1, PM_MODE_PASSIVE);

    /* Entry point 1: POST /ports/N/cache. */
    s_take_log_len = 0;
    mock_xSemaphoreTake_hook = log_lock_take;
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_cache(0, true));
    mock_xSemaphoreTake_hook = NULL;
    assert_move_is_covered_by(move_lock, "port_manager_set_cache");

    /* Entry point 2: the runtime apply of a POST /settings, moving the overlay 1 -> 2 so the
     * whole release-then-enable sequence runs and every lock it needs is in the log. */
    write_cache_en_nvs(false, true);
    s_take_log_len = 0;
    mock_xSemaphoreTake_hook = log_lock_take;
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_cache_settings());
    mock_xSemaphoreTake_hook = NULL;
    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(1),
        "the move must really have happened, or the lock log covers nothing");
    assert_move_is_covered_by(move_lock, "port_manager_apply_cache_settings");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphore_held_count,
        "both entry points must give every lock back — the cache-move mutex above all, since "
        "leaving it held deadlocks every later cache change on the device");
}

/* Behavioural half, and the one that discriminates: a port a request already brought up
 * inside the window must not be initialised a second time. bridge_port_init() and
 * repeater_init_port() refuse a doubled bring-up on their own, but PASSIVE goes through
 * bridge_port_init_serial_only(), which does not — a second call means a second
 * serial_init()/uart_driver_install() for one port.
 *
 * The second half is what keeps the skip honest: the port that is NOT up must still be
 * brought up in the same pass. */
void test_init_does_not_reinit_a_port_a_request_already_brought_up(void)
{
    /* POST /ports/1/mode landed while the boot task was still waiting for the network. It
     * persists the mode, which is exactly what makes the boot loop read it back and re-init. */
    mock_setting_items_set_port_mode(1, PORT_MODE_PASSIVE_STR);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode(0, PM_MODE_PASSIVE));
    TEST_ASSERT_EQUAL(PM_MODE_PASSIVE, port_manager_get_mode(0));
    TEST_ASSERT_EQUAL_STRING(PORT_MODE_PASSIVE_STR, mock_setting_items_get_port_mode(0));

    mock_bridge_reset();
    mock_sniffer_reset();
    mock_serial_reset();

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_bridge_calls[0].bridge_port_init_serial_only_called,
        "a port that is already running must not be opened a second time: bridge_port_init_"
        "serial_only() has no double-init guard, so this is two uart_driver_install() calls "
        "on one port");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_sniffer_attach_called[0],
        "and nothing else of the bring-up may be repeated on it either");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "the running port is left running, not torn down");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_bridge_calls[1].bridge_port_init_serial_only_called,
        "the port that is NOT up must still be brought up in the same pass — the skip is per "
        "port, not a bail-out");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(1),
        "and must end up in its stored mode");
}

/* The other side of that skip: pm_ctx[].mode is PM_MODE_DISABLED both for "never tried" and
 * for "tried and failed" — port_init_mode() sets the mode only on success — so a port whose
 * init failed inside the window looks exactly like an untouched one and must be retried. */
void test_init_retries_a_port_whose_earlier_init_failed(void)
{
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    mock_bridge_port_init_serial_only_should_fail = true;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, port_manager_set_mode(0, PM_MODE_PASSIVE));
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(0),
        "a failed init must leave the port DISABLED, or this test is not set up as intended");

    mock_bridge_port_init_serial_only_should_fail = false;
    mock_bridge_reset();

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_bridge_calls[0].bridge_port_init_serial_only_called,
        "a port that only FAILED to come up must still be tried by the boot loop");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "and must come up this time");
}

/* The factory clock_out test owns the TX/DE pins while the flag is set, so the boot loop must
 * not bring the ports up under it. Skipping is only safe because unfreezing has a path that
 * brings them up: wb_test.c clears the flag and then calls port_manager_apply_settings() for
 * both ports, on the exit path and on the aborted-entry path alike. The second half of this
 * test is that path — without it the skip would leave the ports down for good. */
void test_init_skips_ports_frozen_by_the_factory_test(void)
{
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    mock_setting_items_set_port_mode(1, PORT_MODE_PASSIVE_STR);
    port_manager_set_ports_frozen(true);

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_bridge_calls[i].bridge_port_init_serial_only_called,
            "the boot loop must not hand the TX/DE pins back to the UART while the factory "
            "test is driving them");
        TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(i),
            "and must leave the ports as the test put them");
    }

    /* wb_test.c's exit path. */
    port_manager_set_ports_frozen(false);
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(0));
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_apply_settings(1));

    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(0),
        "the test's exit path is what brings a skipped port up — skipping would otherwise "
        "leave it down until a reboot");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_PASSIVE, port_manager_get_mode(1),
        "both ports, since the test disables both");
}

/* The freeze must be read INSIDE the lock, for the same reason as in apply_settings():
 * read outside it, the boot task could see false, be preempted, and resume after the test
 * has frozen the ports and started the LEDC. */
void test_freeze_landing_during_lock_wait_stops_the_boot_loop(void)
{
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    mock_setting_items_set_port_mode(1, PORT_MODE_PASSIVE_STR);

    /* One-shot (it clears itself), so it lands on the first lock the loop waits for. Nothing
     * above the loop in port_manager_init() takes a mutex: the subsystem stage (which is where
     * the cache-overlay normalisation lives, and it takes no lock) and the cache-server stage
     * are mocked or lock-free here. */
    mock_xSemaphoreTake_hook = freeze_ports_while_waiting_for_lock;
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());
    mock_xSemaphoreTake_hook = NULL;

    TEST_ASSERT_TRUE(port_manager_ports_frozen());
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_bridge_calls[0].bridge_port_init_serial_only_called,
        "a freeze that lands while the boot loop waits for pm_lock must still stop it");
    TEST_ASSERT_EQUAL_MESSAGE(PM_MODE_DISABLED, port_manager_get_mode(0),
        "the port must stay as the factory test left it");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 13. The cache overlay is loaded before httpd starts (C10 follow-up)
 *
 * The "already up" skip in the boot loop above is only safe if a request answered inside the
 * boot window brought the port up with the STORED overlay in hand. That means loading it in
 * port_manager_init_subsystems(), above http_server_init(), not above the port loop.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* The cache overlay is normalised by port_manager_init_subsystems(), which main.c runs BEFORE
 * http_server_init() — not above the port loop in port_manager_init(), which sits behind the
 * wait-for-network loop and therefore runs after httpd has been answering for a while.
 *
 * This test pins the location. test_init_normalises_dual_cache_nvs() above pins the same
 * single-port invariant through port_manager_init(), but that one passes with the loop in
 * either place, because port_manager_init() calls port_manager_init_subsystems() itself. */
void test_cache_overlay_is_normalised_before_httpd_starts(void)
{
    mock_cache_en[0] = true;
    mock_cache_en[1] = true;

    /* main.c's call, and nothing else — no port_manager_init(). */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init_subsystems());

    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(0),
        "the stored overlay must be in memory before http_server_init() registers a handler: "
        "a POST /ports/N/mode answered while it is still false brings the port up uncached");
    TEST_ASSERT_FALSE_MESSAGE(port_manager_get_cache(1),
        "and the single-port invariant (review #51) must be applied in the same pass");
    TEST_ASSERT_FALSE_MESSAGE(mock_cache_en[1],
        "including the corrective NVS write — setting_items is up by then, main.c "
        "ESP_ERROR_CHECKs nvs_init() and setting_items_init() above this call");
}

/* The placement of that block inside port_manager_init_subsystems() has two halves, and this
 * is the half that carries the risk: it sits BELOW the s_subsystems_ready guard, so it runs
 * exactly once per boot. Above the guard it would run on every entry — and port_manager_init()
 * is a second entry, made from behind main.c's wait-for-network loop, i.e. with httpd answering
 * for the whole of that window. A second pass re-reads the cache_en_N keys and re-decides the
 * single-port owner behind a live client's back, and rewrites the loser's key while holding no
 * pm_lock, racing the POST /ports/N/cache that is the other writer of both.
 *
 * The state set up below is the one persist-6 leaves behind: POST /ports/2/cache moved the
 * overlay to port 2 in memory and wrote cache_en_2, but the release write of cache_en_1 failed,
 * so NVS carries BOTH keys. (test_set_cache_move_surfaces_a_failed_release_write drives that
 * path for real; here the end state is set up directly, because what is under test is only that
 * the second call does not look at NVS again.) Re-normalising it hands the overlay back to
 * port 1 — while SNIFF_REASON_CACHE stays on port 2, so /info would name port 1 as the cache
 * source while port 2 is the one feeding the pool: the single-port invariant (review #51)
 * inverted at runtime.
 *
 * Mirror image of test_init_subsystems_creates_the_locks_above_the_one_shot_guard() in section
 * 11, which pins the opposite direction for the port_manager_locks_init() call above the guard. */
void test_init_subsystems_normalises_the_overlay_below_the_one_shot_guard(void)
{
    /* NVS as stored at boot: port 2 is the cache source. */
    mock_cache_en[0] = false;
    mock_cache_en[1] = true;

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init_subsystems());   /* main.c, before httpd */
    TEST_ASSERT_FALSE(port_manager_get_cache(0));
    TEST_ASSERT_TRUE(port_manager_get_cache(1));

    /* NVS changes under the module afterwards — every POST /ports/N/cache writes these keys —
     * and this particular stale cache_en_1 is what one of them left behind when its release
     * write failed. Set here by hand rather than driven through port_manager_set_cache(), the
     * same shortcut the lock test above takes when it clears the handles: what is under test is
     * only what the SECOND call reads. */
    mock_cache_en[0] = true;
    int save_bool_before = mock_setting_items_save_bool_called;

    /* port_manager_init()'s own call to this function, from behind the wait-for-network loop. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init_subsystems());

    TEST_ASSERT_FALSE_MESSAGE(port_manager_get_cache(0),
        "the normalisation must sit BELOW the s_subsystems_ready guard: a second pass re-reads "
        "the cache_en_N keys and re-decides the cache owner while a client holds the other port");
    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(1),
        "the port the client moved the overlay to must keep it — SNIFF_REASON_CACHE is armed "
        "there, so a re-decision leaves /info naming one port while the other feeds the pool");
    TEST_ASSERT_TRUE_MESSAGE(mock_cache_en[1],
        "and the second pass must not rewrite NVS either — it would do so without pm_lock, "
        "racing the POST /ports/N/cache that is the only other writer of these keys");
    TEST_ASSERT_EQUAL_MESSAGE(save_bool_before, mock_setting_items_save_bool_called,
        "no corrective write at all on the second entry: the one-shot guard is what keeps the "
        "normalisation — and its unlocked NVS writes — to the single pass that runs before httpd");
}

/* The other half of the same placement, and the cheaper one: the block sits ABOVE the subsystem
 * inits, so a boot on which sniffer_init() or cache_multimaster_init() runs out of memory still
 * loads and normalises the overlay. Below the `return first_err` that ends the function it would
 * be skipped entirely on such a boot — and the one-shot guard means nothing loads it later, so
 * pm_ctx[].cache_overlay would stay false (BSS) for the rest of the boot while cache_en_1 reads
 * true: /info would report cache_enabled false against GET /settings' cache_en_1 true, and the
 * dual-key NVS state would never be normalised. */
void test_init_subsystems_normalises_the_overlay_above_the_subsystem_inits(void)
{
    /* Legacy dual-key NVS, so both halves of the block are visible: the load and the fix-up. */
    mock_cache_en[0] = true;
    mock_cache_en[1] = true;

    mock_sniffer_init_should_fail = true;
    esp_err_t ret = port_manager_init_subsystems();
    mock_sniffer_init_should_fail = false;

    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, ret,
        "the subsystem failure is still what the caller is told about");
    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(0),
        "the overlay must be loaded even when a subsystem below it failed — the flag is what "
        "/info reports as cache_enabled, and the one-shot guard means nothing loads it later");
    TEST_ASSERT_FALSE_MESSAGE(port_manager_get_cache(1),
        "and the single-port invariant must be applied in that same pass");
    TEST_ASSERT_FALSE_MESSAGE(mock_cache_en[1],
        "including the corrective NVS write, or GET /settings keeps reporting cache_en_2 true "
        "against an /info that reports port 2 uncached");
}

/* End-to-end version of the same thing, and the regression this closes: with the overlay
 * loaded above the port loop instead, a port a request brought up inside the boot window
 * came up with SNIFF_REASON_CACHE unarmed — port_init_mode() read cache_overlay while it was
 * still false (BSS) — and the boot loop's "already up" skip then left it that way for good.
 * cache_sync_global() and sniffer_enable(.., SNIFF_REASON_CACHE) have exactly two call sites
 * between them (port_init_mode() and cache_overlay_apply_locked()), so nothing re-armed it
 * short of a fresh POST /ports/N/cache.
 *
 * Both ports are brought up in the window on purpose: that is the case where the boot loop
 * calls port_init_mode() for neither port, so a sync that only happens there never runs at
 * all and the 32 KB pool is never even allocated. */
void test_cache_is_armed_for_a_port_brought_up_in_the_boot_window(void)
{
    mock_cache_en[0] = true;                       /* NVS: port 1 is the cache source */
    mock_setting_items_set_port_mode(0, PORT_MODE_PASSIVE_STR);
    mock_setting_items_set_port_mode(1, PORT_MODE_PASSIVE_STR);

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init_subsystems());   /* main.c, before httpd */

    /* POST /ports/1/mode and POST /ports/2/mode land inside the window: main.c starts httpd
     * above the wait-for-network loop that port_manager_init() sits behind. */
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode(0, PM_MODE_PASSIVE));
    TEST_ASSERT_EQUAL(ESP_OK, port_manager_set_mode(1, PM_MODE_PASSIVE));

    TEST_ASSERT_EQUAL(ESP_OK, port_manager_init());

    TEST_ASSERT_TRUE_MESSAGE(port_manager_get_cache(0),
        "the overlay flag is what /info reports as cache_enabled");
    TEST_ASSERT_TRUE_MESSAGE((mock_sniffer_reasons[0] & SNIFF_REASON_CACHE) != 0,
        "and SNIFF_REASON_CACHE must actually be armed on that port — without it /info says "
        "cache_enabled true while not one packet reaches the cache, and nothing re-arms it");
    TEST_ASSERT_TRUE_MESSAGE(mock_cache_multimaster_enabled,
        "and the pool must exist, or /cache/status reports it disabled with the flag set");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_sniffer_reasons[1] & SNIFF_REASON_CACHE,
        "the other port must NOT be armed — the cache is single-port (review #51)");
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
    RUN_TEST(test_set_cache_enable_moves_overlay_from_the_other_port);
    RUN_TEST(test_set_cache_move_never_leaves_both_ports_enabled);
    RUN_TEST(test_set_cache_move_back_to_port1_in_port_order);
    RUN_TEST(test_set_cache_enable_on_current_holder_does_not_cycle_the_pool);
    RUN_TEST(test_set_cache_move_surfaces_a_failed_release_write);
    RUN_TEST(test_set_cache_disable_on_non_holder_leaves_the_holder_alone);
    RUN_TEST(test_set_cache_second_port_allowed_after_first_disabled);

    // The runtime apply of a POST /settings (port_manager_apply_cache_settings)
    RUN_TEST(test_apply_cache_settings_moves_the_overlay_to_the_port_nvs_names);
    RUN_TEST(test_apply_cache_settings_drops_the_overlay_when_no_port_asks_for_it);
    RUN_TEST(test_apply_cache_settings_enables_the_overlay_from_no_holder);
    RUN_TEST(test_apply_cache_settings_leaves_a_matching_overlay_completely_alone);
    RUN_TEST(test_apply_cache_settings_normalises_a_request_that_enabled_both_ports);
    RUN_TEST(test_apply_cache_settings_clears_a_surplus_key_without_moving_anything);
    RUN_TEST(test_apply_cache_settings_surfaces_a_failed_pool_allocation);
    RUN_TEST(test_apply_cache_settings_surfaces_a_failed_surplus_clear);
    RUN_TEST(test_apply_cache_settings_pool_failure_outranks_a_failed_surplus_clear);
    RUN_TEST(test_apply_cache_settings_records_the_overlay_on_a_disabled_port);

    RUN_TEST(test_init_normalises_dual_cache_nvs);
    RUN_TEST(test_cache_overlay_survives_transport_change);
    RUN_TEST(test_set_cache_invalid_port);
    RUN_TEST(test_set_cache_disable_clears_reason_and_disables_cache);
    RUN_TEST(test_get_cache_invalid_port);
    RUN_TEST(test_set_cache_persist_failure_surfaced);
    /* C9 — a failed pool allocation must not be reported as success */
    RUN_TEST(test_set_cache_surfaces_a_failed_pool_allocation);
    RUN_TEST(test_set_cache_retries_the_pool_allocation_after_a_failure);
    RUN_TEST(test_set_cache_pool_failure_outranks_a_persist_failure);
    RUN_TEST(test_set_cache_pool_failure_outranks_a_failed_release_write);
    RUN_TEST(test_set_mode_is_not_failed_by_an_unavailable_cache_pool);
    RUN_TEST(test_init_brings_ports_up_when_the_cache_pool_is_unavailable);
    RUN_TEST(test_init_continues_when_a_port_fails_to_come_up);
    RUN_TEST(test_set_cache_handler_reports_a_failed_pool_allocation_as_503);
    RUN_TEST(test_set_cache_handler_reports_a_failed_persist_as_500);
    RUN_TEST(test_set_cache_handler_reports_success);
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

    /* 11 – lock creation: every lock up front, none created in a lock path (C4) */
    RUN_TEST(test_locks_init_creates_every_lock_up_front);
    RUN_TEST(test_init_subsystems_creates_the_locks_above_the_one_shot_guard);
    RUN_TEST(test_only_locks_init_creates_a_mutex);
    RUN_TEST(test_locked_paths_run_on_pre_created_locks);

    /* 12 – the boot loop runs under pm_lock and skips ports that are already up (C10) */
    RUN_TEST(test_init_brings_each_port_up_under_its_own_lock);
    RUN_TEST(test_both_cache_entry_points_serialise_on_one_outer_lock);
    RUN_TEST(test_init_does_not_reinit_a_port_a_request_already_brought_up);
    RUN_TEST(test_init_retries_a_port_whose_earlier_init_failed);
    RUN_TEST(test_init_skips_ports_frozen_by_the_factory_test);
    RUN_TEST(test_freeze_landing_during_lock_wait_stops_the_boot_loop);

    /* 13 – the cache overlay is loaded before httpd starts, so the skip above is safe */
    RUN_TEST(test_cache_overlay_is_normalised_before_httpd_starts);
    RUN_TEST(test_init_subsystems_normalises_the_overlay_below_the_one_shot_guard);
    RUN_TEST(test_init_subsystems_normalises_the_overlay_above_the_subsystem_inits);
    RUN_TEST(test_cache_is_armed_for_a_port_brought_up_in_the_boot_window);

    return UNITY_END();
}

int main(void)
{
    return port_manager_test();
}
