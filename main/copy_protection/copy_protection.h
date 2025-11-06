#pragma once

#include "esp_err.h"
#include "esp_io_expander.h"
#include "sys_info.h"

typedef enum {
    COPY_PROT_STATE_UNKNOWN = 0,
    COPY_PROT_STATE_OK = 1,
    COPY_PROT_STATE_FAIL = -1
} copy_protection_state_t;

// Must be called before copy_protection_init()
void copy_protection_init_keys(void);

// Must be called after copy_protection_init_keys() and gpio_expander_init()
esp_err_t copy_protection_init(esp_io_expander_handle_t io_expander_handle);

// Must be called after copy_protection_init() and sys_info_init()
// Called from network_init()
// It is better to call after WiFi initialization to get real random delay time
esp_err_t copy_protection_start_protection_code_task(void);

// Must be called after copy_protection_init()
// Called from indication_init()
// It is better to call after WiFi initialization to get real random delay time
esp_err_t copy_protection_start_port_expander_task(void);

// Must be called after copy_protection_init(), copy_protection_start_protection_code_task() and copy_protection_start_port_expander_task()
// Called from config_button_init()
// It is better to call after WiFi initialization to get real random delay time
esp_err_t copy_protection_start_sys_monitor_task(void);

// Get copy protection system status
copy_protection_state_t copy_protection_get_state(void);

#if CONFIG_EFUSE_VIRTUAL
    esp_err_t copy_protection_get_protection_code(uint8_t out_buf[PROTECTION_CODE_LEN]);
#endif
