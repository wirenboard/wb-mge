#pragma once

/* Control and observation for the ota_handler mocks that stand in for project modules and therefore
 * have no header of their own to declare their knobs in (they are compiled against the real
 * json_utils.h, cmd_handler.h and auth.h). The mocks of ESP-IDF APIs declare theirs in the stub
 * headers that replace the IDF ones: esp_http_server.h, esp_ota_ops.h, cJSON.h. */

#include <stdbool.h>

/* --- json_utils ----------------------------------------------------------- */

extern int         mock_json_utils_send_response_called;
extern int         mock_json_utils_send_error_called;
extern const char *mock_json_utils_last_error;
extern const char *mock_json_utils_last_error_status;

void mock_json_utils_reset(void);

/* --- cmd_handler ---------------------------------------------------------- */

extern int mock_cmd_reboot_device_call_count;

void mock_cmd_handler_reset(void);

/* --- auth ----------------------------------------------------------------- */

extern int mock_auth_middleware_check_call_count;

/* false makes auth_middleware_check() answer the request with 401 and reject it. */
void mock_auth_set_result(bool result);

void mock_auth_reset(void);
