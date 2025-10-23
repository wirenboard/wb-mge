#include "copy_protection.h"
#include "esp_err.h"
#include "sys_info.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "esp_mac.h"
#include "port_expander_tests.h"
#include "debug_log.h"
#include "esp_log.h"
#include "copy_protection_helpers.h"
#include "keys.h"
#include "serial.h"
#include "tcp_client.h"
#include "tcp_server.h"
#include "settings_manager.h"


#define SEC_CODE_TASK_STACK_SIZE            4096
#define SEC_CODE_TASK_PRIORITY              14
#define SEC_CODE_TASK_DELAY_MS_MIN          5312
#define SEC_CODE_TASK_DELAY_MS_MAX          7957

#define PORT_EXPANDER_TASK_STACK_SIZE       4096
#define PORT_EXPANDER_TASK_PRIORITY         14
#define PORT_EXPANDER_TASK_DELAY_MS_MIN     4183
#define PORT_EXPANDER_TASK_DELAY_MS_MAX     7123

#define SYSTEM_MONITOR_TASK_STACK_SIZE      4096
#define SYSTEM_MONITOR_TASK_PRIORITY        15
#define SYSTEM_MONITOR_TASK_DELAY_MS_MIN    9568    // Must be > MAX(SEC_CODE_TASK_DELAY_MS_MAX, PORT_EXPANDER_TASK_DELAY_MS_MAX)
#define SYSTEM_MONITOR_TASK_DELAY_MS_MAX    11231

#define EVENT_SEC_CODE_OK                   BIT10
#define EVENT_SEC_CODE_FAIL                 BIT2
#define EVENT_PORT_EXPANDER_OK              BIT3
#define EVENT_PORT_EXPANDER_FAIL            BIT11


typedef struct {
    esp_io_expander_handle_t port_expander;
    uint8_t hmac[HMAC_LEN];
    bool port_expander_check_ok;
    TickType_t sec_code_task_delay;
    TickType_t port_exp_task_delay;
    TickType_t sys_monitor_task_delay;
    EventGroupHandle_t event_group;
    copy_protection_state_t prot_state;
    uint8_t key[KEY_LEN];
    uint8_t swap_table[SECURITY_CODE_LEN];
    bool keys_initialized;
} prot_ctx_t;

static prot_ctx_t prot_ctx = {0};

#pragma pack(push, 1)
    typedef struct {
        uint8_t hmac_key[KEY_LEN];
        uint8_t dummy_1[35];
        uint8_t hmac_key_table[KEY_LEN];
        uint8_t dummy_2[14];
        uint8_t prot_code_swap[SECURITY_CODE_LEN];
        uint8_t dummy_3[27];
        uint8_t prot_code_swap_table[SECURITY_CODE_LEN];
    } stored_keys_t;
#pragma pack(pop)

static stored_keys_t stored_keys = {
    .hmac_key = HMAC_KEY,
    .hmac_key_table = HMAC_KEY_TABLE,
    .prot_code_swap = PROT_CODE_SWAP,
    .prot_code_swap_table = PROT_CODE_SWAP_TABLE,
    // Random data
    .dummy_1 = {
        0x3A, 0xD1, 0x8F, 0x25, 0x4B, 0x9C, 0x7E, 0x02,
        0x6D, 0xE8, 0x43, 0x11, 0xBA, 0x74, 0x5F, 0xC3,
        0xA9, 0x01, 0xD6, 0x2E, 0x80, 0x47, 0xB3, 0xF9,
        0x0C, 0x68, 0x3E, 0x95, 0xD2, 0x7B, 0x44, 0x28,
        0xAF, 0x1D, 0x63
    },
    .dummy_2 = {
        0x7E, 0x3B, 0x90, 0x45, 0x1C, 0xD8, 0x52, 0xFA,
        0x06, 0xB7, 0xE1, 0x34, 0x8A, 0xCB
    },
    .dummy_3 = {
        0xF4, 0x69, 0x22, 0xA0, 0x17, 0xCC, 0x5D, 0x83,
        0x1B, 0x9F, 0x40, 0xE7, 0x36, 0x52, 0xAD, 0x04,
        0xB8, 0xC1, 0x6F, 0x93, 0x28, 0xD5, 0x79, 0x03,
        0xEE, 0x61, 0x14
    }
};

#if DEBUG_LOG_ENABLE
    static const char* TAG = "copy_protection";
#endif


static void activate_copy_protection(void)
{
    prot_ctx.prot_state = COPY_PROT_STATE_FAIL;

    serial_activate_copy_protection();
    tcp_client_activate_copy_protection();
    tcp_server_activate_copy_protection();
    settings_activate_copy_protection();

    #if DEBUG_LOG_ENABLE
        ESP_LOGE(TAG, "Copy protection activated!");
    #endif
}


static void security_code_task(void *arg)
{
    #if DEBUG_LOG_ENABLE
        ESP_LOGD(TAG, "security_code_task() started");
    #endif
    vTaskDelay(prot_ctx.sec_code_task_delay);

    uint8_t sec_code[SECURITY_CODE_LEN];
    truncate_hmac(prot_ctx.hmac, prot_ctx.swap_table, sec_code);
    bool eq = consttime_memeq(sec_code, sys_info.security_code, SECURITY_CODE_LEN);

    #if DEBUG_LOG_ENABLE
        if (!eq) {
            ESP_LOGE(TAG, "Security code check failed");
        }
    #endif

    EventBits_t event;
    if (eq) {
        event = EVENT_SEC_CODE_OK;
    } else {
        event = EVENT_SEC_CODE_FAIL;
    }

    xEventGroupSetBits(prot_ctx.event_group, event);

    #if DEBUG_LOG_ENABLE
        ESP_LOGD(TAG, "security_code_task() finished");
    #endif
    vTaskDelete(NULL);
}


static void port_expander_task(void *arg)
{
    #if DEBUG_LOG_ENABLE
        ESP_LOGD(TAG, "port_expander_task() started");
    #endif
    vTaskDelay(prot_ctx.port_exp_task_delay);

    esp_err_t ret = port_expander_run_tests(prot_ctx.port_expander);
    bool ok = (ret == ESP_OK) && prot_ctx.port_expander_check_ok;

    #if DEBUG_LOG_ENABLE
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Port expander tests failed");
        }
    #endif

    if (ok) {
        xEventGroupSetBits(prot_ctx.event_group, EVENT_PORT_EXPANDER_OK);
    } else {
        xEventGroupSetBits(prot_ctx.event_group, EVENT_PORT_EXPANDER_FAIL);
    }

    #if DEBUG_LOG_ENABLE
        ESP_LOGD(TAG, "port_expander_task() finished");
    #endif
    vTaskDelete(NULL);
}


static void sys_monitor_task(void *arg)
{
    #if DEBUG_LOG_ENABLE
        ESP_LOGD(TAG, "sys_monitor_task() started");
    #endif
    vTaskDelay(prot_ctx.sys_monitor_task_delay);

    EventBits_t bits_to_wait = EVENT_SEC_CODE_OK | EVENT_SEC_CODE_FAIL;
    EventBits_t bits = xEventGroupWaitBits(prot_ctx.event_group, bits_to_wait, pdFALSE, pdFALSE, 0);
    bool ok = (bits & EVENT_SEC_CODE_OK);

    bits_to_wait = EVENT_PORT_EXPANDER_OK | EVENT_PORT_EXPANDER_FAIL;
    bits = xEventGroupWaitBits(prot_ctx.event_group, bits_to_wait, pdFALSE, pdFALSE, 0);
    bool fail = (bits & EVENT_PORT_EXPANDER_FAIL) || !bits;

    if (!ok || fail) {
        activate_copy_protection();
    } else {
        prot_ctx.prot_state = COPY_PROT_STATE_OK;
        #if DEBUG_LOG_ENABLE
            ESP_LOGD(TAG, "Copy protection checks passed!");
        #endif
    }

    #if DEBUG_LOG_ENABLE
        ESP_LOGD(TAG, "sys_monitor_task() finished");
    #endif
    vTaskDelete(NULL);
}


void copy_protection_init_keys(void)
{
    unswap_array_values(stored_keys.hmac_key, stored_keys.hmac_key_table, KEY_LEN, prot_ctx.key);
    unswap_array_values(stored_keys.prot_code_swap, stored_keys.prot_code_swap_table, SECURITY_CODE_LEN, prot_ctx.swap_table);
    prot_ctx.keys_initialized = true;
}


esp_err_t copy_protection_init(esp_io_expander_handle_t io_expander_handle)
{
    prot_ctx.prot_state = COPY_PROT_STATE_UNKNOWN;

    if (!prot_ctx.keys_initialized) {
        copy_protection_init_keys();
    }

    if (io_expander_handle == NULL) {
        activate_copy_protection();
        #if DEBUG_LOG_ENABLE
            ESP_LOGE(TAG, "GPIO expander handle is NULL");
        #endif
        return ESP_FAIL;
    }
    prot_ctx.port_expander = io_expander_handle;

    uint8_t mac_addr[MAC_ADDR_LEN] = {0};
    esp_err_t ret = esp_efuse_mac_get_default(mac_addr);
    if (ret != ESP_OK) {
        activate_copy_protection();
        #if DEBUG_LOG_ENABLE
            ESP_LOGE(TAG, "Unable to get MAC address");
        #endif
        return ESP_FAIL;
    }

    calc_hmac(mac_addr, prot_ctx.key, prot_ctx.hmac);

    prot_ctx.event_group = xEventGroupCreate();
    if (prot_ctx.event_group == NULL) {
        activate_copy_protection();
        #if DEBUG_LOG_ENABLE
            ESP_LOGE(TAG, "Unable to create Event Group");
        #endif
        return ESP_FAIL;
    }

    prot_ctx.sec_code_task_delay = get_random_time(SEC_CODE_TASK_DELAY_MS_MIN, SEC_CODE_TASK_DELAY_MS_MAX);
    prot_ctx.port_exp_task_delay = get_random_time(PORT_EXPANDER_TASK_DELAY_MS_MIN, PORT_EXPANDER_TASK_DELAY_MS_MAX);
    prot_ctx.sys_monitor_task_delay = get_random_time(SYSTEM_MONITOR_TASK_DELAY_MS_MIN, SYSTEM_MONITOR_TASK_DELAY_MS_MAX);

    prot_ctx.port_expander_check_ok = (port_expander_run_tests(io_expander_handle) == ESP_OK);
    #if DEBUG_LOG_ENABLE
        if (!prot_ctx.port_expander_check_ok) {
            ESP_LOGE(TAG, "Port expander tests failed");
        }
    #endif

    xTaskCreate(sys_monitor_task, "", SYSTEM_MONITOR_TASK_STACK_SIZE, NULL, SYSTEM_MONITOR_TASK_PRIORITY, NULL);
    xTaskCreate(security_code_task, "", SEC_CODE_TASK_STACK_SIZE, NULL, SEC_CODE_TASK_PRIORITY, NULL);
    xTaskCreate(port_expander_task, "", PORT_EXPANDER_TASK_STACK_SIZE, NULL, PORT_EXPANDER_TASK_PRIORITY, NULL);

    // Don't fail, always return ESP_OK
    return ESP_OK;
}


copy_protection_state_t copy_protection_get_state(void)
{
    return prot_ctx.prot_state;
}


#if CONFIG_EFUSE_VIRTUAL
    esp_err_t copy_protection_get_security_code(uint8_t out_buf[SECURITY_CODE_LEN])
    {
        uint8_t mac_addr[MAC_ADDR_LEN] = {0};
        esp_err_t ret = esp_efuse_mac_get_default(mac_addr);
        if (ret != ESP_OK) {
            return ret;
        }

        uint8_t hmac[HMAC_LEN] = {0};
        calc_hmac(mac_addr, prot_ctx.key, hmac);
        truncate_hmac(hmac, prot_ctx.swap_table, out_buf);

        return ESP_OK;
    }
#endif
