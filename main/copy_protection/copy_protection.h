#pragma once

#include "esp_err.h"
#include "esp_io_expander.h"
#include "sys_info.h"

typedef enum {
    COPY_PROT_STATE_UNKNOWN = 0,
    COPY_PROT_STATE_OK = 1,
    COPY_PROT_STATE_FAIL = -1
} copy_protection_state_t;

void copy_protection_init_keys(void);
esp_err_t copy_protection_init(esp_io_expander_handle_t io_expander_handle);
copy_protection_state_t copy_protection_get_state(void);

#if CONFIG_EFUSE_VIRTUAL
    esp_err_t copy_protection_get_security_code(uint8_t out_buf[SECURITY_CODE_LEN]);
#endif
