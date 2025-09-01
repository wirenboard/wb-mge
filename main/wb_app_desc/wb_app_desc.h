#pragma once

#include <stdint.h>
#include <stddef.h>

// Don't change these definitions

#define WB_APP_DESC_MAGIC_WORD          0xDACBBCAB

#define WB_APP_DESC_SIZE                192

#define DEVICE_SIGNATURE_LEN            12
#define DEVICE_MODEL_LEN                20
#define FIRMWARE_VERSION_LEN            16
#define FIRMWARE_GIT_INFO_LEN           50

#define WB_APP_DESC_RESERVED_LEN        (WB_APP_DESC_SIZE \
                                        - sizeof(uint32_t) \
                                        - DEVICE_SIGNATURE_LEN \
                                        - DEVICE_MODEL_LEN \
                                        - FIRMWARE_VERSION_LEN \
                                        - FIRMWARE_GIT_INFO_LEN)


typedef struct __attribute__((packed)) {
    uint32_t magic_word;
    char signature[DEVICE_SIGNATURE_LEN];
    char device_model[DEVICE_MODEL_LEN];
    char fw_version[FIRMWARE_VERSION_LEN];
    char fw_git_info[FIRMWARE_GIT_INFO_LEN];
    uint8_t reserved[WB_APP_DESC_RESERVED_LEN];
} wb_app_desc_t;


_Static_assert(sizeof(wb_app_desc_t) == WB_APP_DESC_SIZE, "sizeof(wb_app_desc_t) should be equal WB_APP_DESC_SIZE");


extern const wb_app_desc_t wb_app_desc;

// Get string field value from wb_app_desc_t
// sizeof(dest_str_buf) must be at least (field_len + 1) bytes
size_t wb_app_desc_get_str_field(const char* field, size_t field_len, char* dest_str_buf);

// Get string field value from wb_app_desc_t macro
// sizeof(dest_str_buf) must be at least (sizeof(field) + 1) bytes
#define GET_APP_DESC_STR_FIELD(field_name, dest_str_buf)    wb_app_desc_get_str_field(wb_app_desc.field_name, sizeof(wb_app_desc.field_name), dest_str_buf)
