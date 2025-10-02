#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "array_size.h"


#define DEBUG_LOG_ENABLE        0   // Global debug logs enable


#if (DEBUG_LOG_ENABLE)

    typedef struct {
        const char* tag;
        bool enabled;
    } cfg_elem_t;

    static const cfg_elem_t debug_log_config[] = {
        {.tag = "main",                 .enabled = true},
        {.tag = "auth",                 .enabled = false},
        {.tag = "gpio_expander",        .enabled = false},
        {.tag = "nv_storage",           .enabled = false},
        {.tag = "setting_items",        .enabled = false},
        {.tag = "settings_save_timer",  .enabled = true},
        {.tag = "settings_update",      .enabled = true},
        {.tag = "sys_info",             .enabled = true},
        {.tag = "bridge",               .enabled = false},
        {.tag = "transparent_tcp",      .enabled = false},
        {.tag = "modbus_tcp",           .enabled = false},
        {.tag = "modbus_helpers",       .enabled = false},
        {.tag = "serial",               .enabled = false},
        {.tag = "tcp_client",           .enabled = false},
        {.tag = "tcp_server",           .enabled = false},
        {.tag = "indication",           .enabled = true},
        {.tag = "network",              .enabled = true},
        {.tag = "wifi_apsta",           .enabled = true},
        {.tag = "mio_control",          .enabled = false}
    };

    void debug_log_init(void)
    {
        for (unsigned i = 0; i < ARRAY_SIZE(debug_log_config); i++) {
            if (debug_log_config[i].enabled) {
                esp_log_level_set(debug_log_config[i].tag, ESP_LOG_DEBUG);
            }
        }
    }

    bool debug_log_is_enabled(const char* tag)
    {
        if (tag == NULL) {
            return false;
        }

        for (unsigned i = 0; i < ARRAY_SIZE(debug_log_config); i++) {
            if (strcmp(debug_log_config[i].tag, tag) == 0) {
                return debug_log_config[i].enabled;
            }
        }

        return false;
    }

#else

    void debug_log_init(void)
    {
    }

    bool debug_log_is_enabled(const char* tag)
    {
        (void)tag;
        return false;
    }

#endif
