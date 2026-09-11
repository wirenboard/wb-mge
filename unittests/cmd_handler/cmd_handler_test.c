#include "unity.h"
#include "console_log.h"

#include "cmd_handler.h"
#include "cJSON.h"
#include "task.h"
#include "setting_items.h"

#include <string.h>

// -------------------------------------------------------------------
// Symbols exported by mocks
// -------------------------------------------------------------------
void   mock_json_utils_set_request(cJSON *request);
cJSON *mock_json_utils_take_response(void);
void   mock_json_utils_reset(void);
extern int         mock_json_utils_send_error_called;
extern const char *mock_json_utils_last_error;

extern int mock_airzone_inclusion_counter;
extern int mock_airzone_settings_counter;
extern int mock_airzone_reload_called;
extern int mock_airzone_reloads_at_settings_bump;
void       mock_airzone_gw_reset(void);

extern int       mock_setting_items_set_defaults_called;
extern esp_err_t mock_setting_items_set_defaults_result;
void             mock_setting_items_reset(void);
void             mock_setting_items_set_int(const char *key, int value);

extern int mock_settings_update_call_count;
void       mock_settings_update_reset(void);

void mock_rs485_control_reset(void);
void mock_esp_system_reset(void);
void mock_freertos_task_reset(void);

// -------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------

// Run POST /cmd with the given command name and return the emitted response, or NULL when the
// handler answered with an error instead. The caller deletes a non-NULL response.
static cJSON *run_cmd(const char *cmd_name)
{
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "cmd", cmd_name);
    mock_json_utils_set_request(request);

    httpd_req_t req = {0};
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, cmd_post_handler(&req),
        "cmd_post_handler must return ESP_OK — the answer travels in the response JSON");

    return mock_json_utils_take_response();
}

static bool response_success(const cJSON *resp)
{
    cJSON *item = cJSON_GetObjectItem(resp, "success");
    return (item != NULL) && cJSON_IsTrue(item);
}

// -------------------------------------------------------------------
// setUp / tearDown
// -------------------------------------------------------------------

void setUp(void)
{
    mock_json_utils_reset();
    mock_airzone_gw_reset();
    mock_setting_items_reset();
    mock_settings_update_reset();
    mock_rs485_control_reset();
    mock_esp_system_reset();
    mock_freertos_task_reset();
}

void tearDown(void)
{
    /* Frees a request or response a failing assertion left behind. */
    mock_json_utils_reset();
}

// ===================================================================
// zwave_include
// ===================================================================

// The whole command is one increment of the inclusion counter: the channel to the Z-Wave board is
// read-only, so the board can never report "seen" and a flag would have nobody to clear it. The
// board treats any difference from the value it last read as exactly one request.
void test_zwave_include_increments_inclusion_counter(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cmd: zwave_include -> inclusion counter +1, success:true");
    LOG_MESSAGE();

    cJSON *resp = run_cmd("zwave_include");

    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "zwave_include must be answered with a response object");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp), "zwave_include must report success");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_airzone_inclusion_counter,
        "zwave_include must increment the inclusion counter exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_json_utils_send_error_called,
        "a known command must not be answered with an error");

    cJSON *command = cJSON_GetObjectItem(resp, "command");
    TEST_ASSERT_NOT_NULL_MESSAGE(command, "the response must echo the command name");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("zwave_include", command->valuestring,
        "the response must echo the command that was executed");

    cJSON_Delete(resp);
}

// Each call is one request. The board compares against the value it last read, so a second press
// of the button has to move the counter again or it would be ignored.
void test_zwave_include_twice_increments_twice(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cmd: zwave_include twice -> inclusion counter +2");
    LOG_MESSAGE();

    cJSON *first = run_cmd("zwave_include");
    cJSON_Delete(first);
    cJSON *second = run_cmd("zwave_include");
    cJSON_Delete(second);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_airzone_inclusion_counter,
        "every zwave_include must count as its own request");
}

// zwave_include touches the Z-Wave board and nothing else: it must not reset settings, must not
// re-apply them and must not reboot the device.
void test_zwave_include_does_nothing_else(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cmd: zwave_include -> no settings reset, no settings update, no reboot");
    LOG_MESSAGE();

    cJSON *resp = run_cmd("zwave_include");
    cJSON_Delete(resp);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_setting_items_set_defaults_called,
        "zwave_include must not touch the stored settings");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_settings_update_call_count,
        "zwave_include must not re-apply the settings");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xTaskCreate_data.called,
        "zwave_include must not schedule a reboot");
}

// An unknown command must still be refused now that the table has grown a third entry.
void test_unknown_command_is_refused_and_moves_no_counter(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cmd: unknown command -> error, inclusion counter untouched");
    LOG_MESSAGE();

    cJSON *resp = run_cmd("zwave_exclude");

    TEST_ASSERT_NULL_MESSAGE(resp, "an unknown command must not be answered with a success object");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_json_utils_send_error_called,
        "an unknown command must be answered with an error");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_airzone_inclusion_counter,
        "an unknown command must not increment the inclusion counter");
}

// The command sits next to the ones that were already there; adding it must not have shadowed
// set_default_settings, whose name shares no prefix but whose table slot moved.
void test_set_default_settings_still_dispatches(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cmd: set_default_settings -> defaults applied, inclusion counter untouched");
    LOG_MESSAGE();

    cJSON *resp = run_cmd("set_default_settings");

    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "set_default_settings must be answered with a response object");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp), "set_default_settings must report success");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_setting_items_set_defaults_called,
        "set_default_settings must reset the stored settings");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_airzone_inclusion_counter,
        "set_default_settings must not increment the inclusion counter");

    cJSON_Delete(resp);
}

// ===================================================================
// set_default_settings and the Airzone counters
// ===================================================================

// setting_items_set_defaults() rewrites every key, so it puts both Airzone counters back to 0 on
// its own. They must come out of the reset exactly as they went in: the Z-Wave board treats any
// difference from the value it last read as one event, so a counter that came back as 0 would,
// after the next reboot, read as an inclusion request nobody made.
void test_set_default_settings_preserves_airzone_counters(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cmd: set_default_settings -> both Airzone counters keep their stored values");
    LOG_MESSAGE();

    mock_setting_items_set_int(KEY_AIRZONE_INCL_CNT, 7);
    mock_setting_items_set_int(KEY_AIRZONE_SET_CNT, 3);

    cJSON *resp = run_cmd("set_default_settings");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "set_default_settings must be answered with a response object");
    cJSON_Delete(resp);

    TEST_ASSERT_EQUAL_INT_MESSAGE(7, setting_items_read_int(KEY_AIRZONE_INCL_CNT),
        "a factory reset must leave the stored inclusion counter where it was");
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, setting_items_read_int(KEY_AIRZONE_SET_CNT),
        "a factory reset must leave the stored settings counter where it was");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_airzone_inclusion_counter,
        "a factory reset must never ask the Z-Wave board to enter inclusion");
}

// The four Airzone settings really did change, so the board has to be told — once. The values are
// reloaded into the register block's cache first and the counter moves after them, because the
// board reads the four registers only in the instant the counter changes.
void test_set_default_settings_bumps_settings_counter_after_the_values(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cmd: set_default_settings -> settings counter +1, bumped after the values are reloaded");
    LOG_MESSAGE();

    cJSON *resp = run_cmd("set_default_settings");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "set_default_settings must be answered with a response object");
    cJSON_Delete(resp);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_airzone_settings_counter,
        "a factory reset must move the settings counter exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_airzone_reload_called,
        "a factory reset must refresh the cached Airzone settings");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_airzone_reloads_at_settings_bump,
        "the defaults must already be in the cache when the counter moves");
}

// A reset that failed changed nothing, so there is nothing to tell the board about — and the
// counters must not be rewritten on the way out either.
void test_failed_set_default_settings_moves_no_counter(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cmd: set_default_settings fails -> no counter moves, stored counters untouched");
    LOG_MESSAGE();

    mock_setting_items_set_int(KEY_AIRZONE_INCL_CNT, 7);
    mock_setting_items_set_int(KEY_AIRZONE_SET_CNT, 3);
    mock_setting_items_set_defaults_result = ESP_FAIL;

    cJSON *resp = run_cmd("set_default_settings");
    if (resp != NULL) {
        TEST_ASSERT_FALSE_MESSAGE(response_success(resp), "a failed reset must not report success");
        cJSON_Delete(resp);
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_airzone_settings_counter,
        "a reset that changed nothing must not move the settings counter");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_airzone_inclusion_counter,
        "a failed reset must not move the inclusion counter");
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, setting_items_read_int(KEY_AIRZONE_INCL_CNT),
        "a failed reset must leave the stored inclusion counter alone");
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, setting_items_read_int(KEY_AIRZONE_SET_CNT),
        "a failed reset must leave the stored settings counter alone");
}

// -------------------------------------------------------------------
// Test runner
// -------------------------------------------------------------------

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_zwave_include_increments_inclusion_counter);
    RUN_TEST(test_zwave_include_twice_increments_twice);
    RUN_TEST(test_zwave_include_does_nothing_else);
    RUN_TEST(test_unknown_command_is_refused_and_moves_no_counter);
    RUN_TEST(test_set_default_settings_still_dispatches);
    RUN_TEST(test_set_default_settings_preserves_airzone_counters);
    RUN_TEST(test_set_default_settings_bumps_settings_counter_after_the_values);
    RUN_TEST(test_failed_set_default_settings_moves_no_counter);

    return UNITY_END();
}
