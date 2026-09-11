#include "unity.h"
#include "console_log.h"

#include "cmd_handler.h"
#include "cJSON.h"
#include "task.h"

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
void       mock_airzone_gw_reset(void);

extern int       mock_settings_update_call_count;
extern int       mock_settings_factory_reset_call_count;
extern esp_err_t mock_settings_factory_reset_result;
extern int       mock_settings_factory_resets_at_update;
void             mock_settings_update_reset(void);

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

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_settings_factory_reset_call_count,
        "zwave_include must not reset the stored settings");
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

// ===================================================================
// set_default_settings
//
// The reset itself lives in settings_factory_reset() and is tested against the real function in
// the settings_update suite — keeping the Airzone counters across it, republishing the settings
// before the counter moves. Here it is a recording stub, so these tests assert only what
// cmd_execute() still decides: the reset runs once, the new settings are applied after it, and a
// reset that failed is reported instead.
// ===================================================================

// The command sits next to the ones that were already there; adding zwave_include must not have
// shadowed set_default_settings, whose name shares no prefix but whose table slot moved.
void test_set_default_settings_still_dispatches(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cmd: set_default_settings -> factory reset once, inclusion counter untouched");
    LOG_MESSAGE();

    cJSON *resp = run_cmd("set_default_settings");

    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "set_default_settings must be answered with a response object");
    TEST_ASSERT_TRUE_MESSAGE(response_success(resp), "set_default_settings must report success");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_settings_factory_reset_call_count,
        "set_default_settings must perform the factory reset exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_airzone_inclusion_counter,
        "set_default_settings must not increment the inclusion counter");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xTaskCreate_data.called,
        "set_default_settings must not reboot the device");

    cJSON_Delete(resp);
}

// The reset only rewrites NVS; the running system is still on the old settings until
// settings_update() reconciles it — so the command has to call it, and after the reset, or it
// would re-apply the values the reset is about to replace.
void test_set_default_settings_applies_the_new_settings_after_the_reset(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cmd: set_default_settings -> settings_update() once, after the factory reset");
    LOG_MESSAGE();

    cJSON *resp = run_cmd("set_default_settings");
    TEST_ASSERT_NOT_NULL_MESSAGE(resp, "set_default_settings must be answered with a response object");
    cJSON_Delete(resp);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_settings_update_call_count,
        "the defaults have to be applied to the running system exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_settings_factory_resets_at_update,
        "the reset must already have happened when the settings are applied");
}

// A reset that failed left the stored settings as they were, so there is nothing to apply — and
// the caller has to be told, or the UI reports a factory reset that never happened.
void test_failed_set_default_settings_is_reported_and_applies_nothing(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "cmd: set_default_settings fails -> error answer, no settings update, no reboot");
    LOG_MESSAGE();

    mock_settings_factory_reset_result = ESP_FAIL;

    cJSON *resp = run_cmd("set_default_settings");
    if (resp != NULL) {
        TEST_ASSERT_FALSE_MESSAGE(response_success(resp), "a failed reset must not report success");
        cJSON_Delete(resp);
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_json_utils_send_error_called,
        "a failed reset must be answered with an error");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_settings_factory_reset_call_count,
        "a failed reset must not be retried behind the user's back");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_settings_update_call_count,
        "a reset that changed nothing must not re-apply the settings");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_airzone_inclusion_counter,
        "a failed reset must not move the inclusion counter");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xTaskCreate_data.called,
        "a failed reset must not reboot the device");
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
    RUN_TEST(test_set_default_settings_applies_the_new_settings_after_the_reset);
    RUN_TEST(test_failed_set_default_settings_is_reported_and_applies_nothing);

    return UNITY_END();
}
