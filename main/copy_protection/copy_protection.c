#include "copy_protection.h"
#include "mbedtls/md.h"
#include "esp_err.h"
#include "sys_info.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "port_expander_tests.h"


#define MAC_ADDR_LEN                        6
#define KEY_LEN                             32
#define HMAC_LEN                            32

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
} prot_ctx_t;

static prot_ctx_t prot_ctx = {0};


static const uint8_t key[KEY_LEN] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
};

static const uint8_t swap_table[SECURITY_CODE_LEN] = {
    1, 3, 5, 7, 9, 11, 13, 15, 19, 21, 23, 25
};


// static const char* TAG = "copy_protection";


static void calc_hmac(const uint8_t mac_addr[MAC_ADDR_LEN], const uint8_t key[KEY_LEN], uint8_t out_hmac[HMAC_LEN])
{
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1); // 1 — enable HMAC mode
    mbedtls_md_hmac_starts(&ctx, key, KEY_LEN);
    mbedtls_md_hmac_update(&ctx, mac_addr, MAC_ADDR_LEN);
    mbedtls_md_hmac_finish(&ctx, out_hmac);
    mbedtls_md_free(&ctx);
}


static void truncate_hmac(const uint8_t hmac[HMAC_LEN], const uint8_t swap_table[SECURITY_CODE_LEN], uint8_t out_sec_code[SECURITY_CODE_LEN])
{
    for (unsigned index = 0; index < SECURITY_CODE_LEN; index++) {
        unsigned pos = swap_table[index];
        if (pos >= HMAC_LEN) {
            pos = HMAC_LEN;
        }
        out_sec_code[index] = hmac[pos];
    }
}


static TickType_t get_random_time(unsigned min_ms, unsigned max_ms)
{
    unsigned range = max_ms - min_ms;
    unsigned value = min_ms + esp_random() % (range + 1);

    return pdMS_TO_TICKS(value);
}


static bool consttime_memeq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    uint8_t r = 0;
    for (size_t i = 0; i < n; ++i) {
        r |= x[i] ^ y[i];
    }
    return (r == 0);
}


// static bool port_expander_check(void)
// {
//     esp_io_expander_set_dir(prot_ctx.port_expander, PORT_EXPANDER_GROUNDED_PIN, IO_EXPANDER_INPUT);
//     esp_io_expander_set_dir(prot_ctx.port_expander, PORT_EXPANDER_SHORTED_PIN_1, IO_EXPANDER_INPUT);
//     esp_io_expander_set_dir(prot_ctx.port_expander, PORT_EXPANDER_SHORTED_PIN_2, IO_EXPANDER_OUTPUT);
//     esp_io_expander_set_level(prot_ctx.port_expander, PORT_EXPANDER_SHORTED_PIN_2, 0);

//     uint32_t pin_levels = 0;
//     esp_io_expander_get_level(prot_ctx.port_expander, PORT_EXPANDER_GROUNDED_PIN, &pin_levels);
//     bool ok = !pin_levels;

//     pin_levels = 0;
//     esp_io_expander_get_level(prot_ctx.port_expander, PORT_EXPANDER_SHORTED_PIN_1, &pin_levels);
//     ok = !pin_levels && ok;



// }


static void security_code_task(void *arg)
{
    vTaskDelay(prot_ctx.sec_code_task_delay);

    uint8_t sec_code[SECURITY_CODE_LEN];
    truncate_hmac(prot_ctx.hmac, swap_table, sec_code);
    bool eq = consttime_memeq(sec_code, sys_info.security_code, SECURITY_CODE_LEN);

    EventBits_t event;
    if (eq) {
        event = EVENT_SEC_CODE_OK;
    } else {
        event = EVENT_SEC_CODE_FAIL;
    }

    xEventGroupSetBits(prot_ctx.event_group, event);

    vTaskDelete(NULL);
}


static void port_expander_task(void *arg)
{
    vTaskDelay(prot_ctx.port_exp_task_delay);

    esp_err_t ret = port_expander_run_tests(prot_ctx.port_expander);
    bool ok = (ret == ESP_OK) && prot_ctx.port_expander_check_ok;

    if (ok) {
        xEventGroupSetBits(prot_ctx.event_group, EVENT_PORT_EXPANDER_OK);
    } else {
        xEventGroupSetBits(prot_ctx.event_group, EVENT_PORT_EXPANDER_FAIL);
    }

    vTaskDelete(NULL);
}


static void sys_monitor_task(void *arg)
{
    vTaskDelay(prot_ctx.sys_monitor_task_delay);

    EventBits_t bits_to_wait = EVENT_SEC_CODE_OK | EVENT_SEC_CODE_FAIL;
    EventBits_t bits = xEventGroupWaitBits(prot_ctx.event_group, bits_to_wait, pdFALSE, pdFALSE, 0);
    bool ok = (bits & EVENT_SEC_CODE_OK);

    bits_to_wait = EVENT_PORT_EXPANDER_OK | EVENT_PORT_EXPANDER_FAIL;
    bits = xEventGroupWaitBits(prot_ctx.event_group, bits_to_wait, pdFALSE, pdFALSE, 0);
    bool fail = (bits & EVENT_PORT_EXPANDER_FAIL) || !bits;

    if (!ok || fail) {
        prot_ctx.prot_state = COPY_PROT_STATE_FAIL;
        //TODO: do something bad
    } else {
        prot_ctx.prot_state = COPY_PROT_STATE_OK;
    }

    vTaskDelete(NULL);
}


esp_err_t copy_protection_init(esp_io_expander_handle_t io_expander_handle)
{
    prot_ctx.prot_state = COPY_PROT_STATE_UNKNOWN;

    if (io_expander_handle == NULL) {
        prot_ctx.prot_state = COPY_PROT_STATE_FAIL;
        return ESP_FAIL;
    }
    prot_ctx.port_expander = io_expander_handle;

    uint8_t mac_addr[MAC_ADDR_LEN] = {0};
    esp_err_t ret = esp_efuse_mac_get_default(mac_addr);
    if (ret != ESP_OK) {
        prot_ctx.prot_state = COPY_PROT_STATE_FAIL;
        return ESP_FAIL;
    }

    calc_hmac(mac_addr, key, prot_ctx.hmac);

    prot_ctx.event_group = xEventGroupCreate();
    if (prot_ctx.event_group == NULL) {
        prot_ctx.prot_state = COPY_PROT_STATE_FAIL;
        return ESP_FAIL;
    }

    prot_ctx.sec_code_task_delay = get_random_time(SEC_CODE_TASK_DELAY_MS_MIN, SEC_CODE_TASK_DELAY_MS_MAX);
    prot_ctx.port_exp_task_delay = get_random_time(PORT_EXPANDER_TASK_DELAY_MS_MIN, PORT_EXPANDER_TASK_DELAY_MS_MAX);
    prot_ctx.sys_monitor_task_delay = get_random_time(SYSTEM_MONITOR_TASK_DELAY_MS_MIN, SYSTEM_MONITOR_TASK_DELAY_MS_MAX);

    prot_ctx.port_expander_check_ok = (port_expander_run_tests(io_expander_handle) == ESP_OK);

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
        calc_hmac(mac_addr, key, hmac);
        truncate_hmac(hmac, swap_table, out_buf);

        return ESP_OK;
    }
#endif
