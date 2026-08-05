#include "ota_test_env.h"

#include <string.h>

#include "cJSON.h"
#include "esp_ota_ops.h"
#include "sys_info.h"
#include "wb_app_desc.h"

static uint8_t body[OTA_TEST_BODY_LEN];

const uint8_t *ota_test_body(void)
{
    return body;
}

static void build_body(void)
{
    memset(body, 0, sizeof(body));

    wb_app_desc_t desc = {0};
    desc.magic_word = WB_APP_DESC_MAGIC_WORD;
    strncpy(desc.signature, OTA_TEST_SIGNATURE, sizeof(desc.signature));
    strncpy(desc.device_model, "WB-MGE v.3", sizeof(desc.device_model));
    strncpy(desc.fw_version, "1.2.3", sizeof(desc.fw_version));
    strncpy(desc.fw_git_info, "abcdef1_test", sizeof(desc.fw_git_info));

    memcpy(&body[WB_APP_DESC_OFFSET], &desc, sizeof(desc));
}

void ota_test_env_reset(void)
{
    build_body();

    memset(&sys_info, 0, sizeof(sys_info));
    strncpy(sys_info.device_signature, OTA_TEST_SIGNATURE, sizeof(sys_info.device_signature) - 1);

    mock_esp_ota_reset();
    mock_http_reset();
    mock_http_set_body(body, sizeof(body));
    mock_http_set_content_type("application/octet-stream");
    mock_json_utils_reset();
    mock_cjson_reset();
    mock_cmd_handler_reset();
    mock_auth_reset();
}

void ota_test_make_request(httpd_req_t *req)
{
    memset(req, 0, sizeof(*req));
    req->method = HTTP_POST;
    strncpy(req->uri, "/update", sizeof(req->uri) - 1);
    req->content_len = OTA_TEST_BODY_LEN;
}
