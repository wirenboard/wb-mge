#pragma once

/* Shared setup for the ota_handler unit tests: one valid firmware body and one reset that puts
 * every mock back to its default. The state guard in ota_handler.c is a file-static that no test
 * can reach directly, which is why the tests are split across separate binaries — inside one
 * binary the flag would survive from one test into the next. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "ota_mocks.h"

/* Body of the synthetic firmware upload. Must be at least WB_APP_DESC_OFFSET + sizeof(wb_app_desc_t)
 * bytes, since the handler validates the descriptor out of the first received chunk, and no larger
 * than JSON_UTILS_REQ_RECV_BUF_SIZE so it arrives in a single recv. */
#define OTA_TEST_BODY_LEN   1024

/* Device signature the mocked sys_info reports and the synthetic firmware carries. */
#define OTA_TEST_SIGNATURE  "mge_v3"

/* Reset every mock, install the valid firmware body, an octet-stream Content-Type and a matching
 * device signature. Call it from setUp(). */
void ota_test_env_reset(void);

/* Fill req with a well-formed POST /update whose body is the one above. */
void ota_test_make_request(httpd_req_t *req);

const uint8_t *ota_test_body(void);
