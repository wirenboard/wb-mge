#include "wb_app_desc.h"
#include "config.h"

#ifdef __unittest_env__
    #define WB_APP_DESC_ATTR
#else
    #define WB_APP_DESC_ATTR __attribute__((section(".rodata_custom_desc")))
#endif

const WB_APP_DESC_ATTR wb_app_desc_t wb_app_desc = {
    .magic_word = WB_APP_DESC_MAGIC_WORD,
    .signature = DEVICE_SIGNATURE,
    .device_model = DEVICE_MODEL,
    .fw_version = FIRMWARE_VERSION,
    .fw_git_info = FIRMWARE_GIT_INFO,
    .reserved = {0}
};

size_t wb_app_desc_get_str_field(const char* field, size_t field_len, char* dest_str_buf)
{
    size_t len = 0;
    for (len = 0; (len < field_len) && field[len]; len++) {
        dest_str_buf[len] = field[len];
    }
    dest_str_buf[len] = 0;
    return len;
}
