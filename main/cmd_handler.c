#include "cmd_handler.h"
#include "json_utils.h"
#include "auth.h"
#include "setting_items.h"
#include "sys_info.h"
#include "array_size.h"
#include "settings_update.h"
#include "settings_save_timer.h"
#include "rs485_control.h"

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#if (QEMU_BUILD)
    #include <soc/timer_group_reg.h>   /* TIMG_LACTCONFIG_REG etc. — LACT shutdown */
    #include <esp_intr_alloc.h>         /* esp_intr_dump() — interrupt map debug */
    #include <driver/uart.h>            /* uart_is_driver_installed(), uart_write_bytes() */
#endif

static const char *TAG = "cmd_handler";

#define CMD_NAME_MAX_LEN        32
#define REBOOT_DELAY_MS         1000

#define REBOOT_TASK_STACK_SIZE  2048
#define REBOOT_TASK_PRIORITY    5

#define CMD_UNKNOWN             -1

typedef enum {
    CMD_REBOOT,
    CMD_SET_DEFAULT_SETTINGS,
#if (QEMU_BUILD)
    CMD_REBOOT_RAW, /* DEBUG/QEMU: esp_restart() without LACT silence — bug05 repro */
    CMD_INTR_DUMP,  /* DEBUG/QEMU: dump CPU interrupt allocation table via esp_intr_dump() */
    CMD_UART1_TX,   /* DEBUG/QEMU: flood UART1 TX to provoke UART1 interrupt (bug01 repro) */
#endif
} cmd_code_t;

typedef struct {
    int cmd_code;
    const char *cmd_name;
    const char *description;
} cmd_t;

static const cmd_t available_commands[] = {
    {CMD_REBOOT, "reboot", "Restart the device"},
    {CMD_SET_DEFAULT_SETTINGS, "set_default_settings", "Reset all settings to factory defaults"},
#if (QEMU_BUILD)
    {CMD_REBOOT_RAW, "reboot_raw", "DEBUG: esp_restart() without LACT shutdown (bug05 repro)"},
    {CMD_INTR_DUMP,  "intr_dump",  "DEBUG: dump CPU interrupt allocation table"},
    {CMD_UART1_TX,   "uart1_tx",   "DEBUG: flood UART1 TX to make UART1 interrupt pending (bug01 repro)"},
#endif
};

#if (QEMU_BUILD)
static void reboot_raw_task(void *pvParameters)
{
    /* Use 100 ms instead of REBOOT_DELAY_MS (1 s): under QEMU starvation,
     * vTaskDelay(1 s) may hang indefinitely (bug 08). 100 ms is enough for
     * httpd to send the HTTP response before esp_restart() tears down the chip. */
    vTaskDelay(pdMS_TO_TICKS(100));
    /* Intentionally do NOT silence LACT — same as panic path. Bug05 repro tool.
     * With the IDF-level NULL guard in timer_alarm_isr() (bug05 patch),
     * this should now result in one clean reboot instead of a boot-loop. */
    esp_restart();
}
#endif

static void reboot_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Executing reboot command");

    #if (!QEMU_BUILD)
        rs485_bus_vout_set_allowed(false);
        rs485_bus_vout_on_off(false);
    #endif

    /* Bug 08 fix: under QEMU host starvation, vTaskDelay(1 s) can hang forever.
     * Use 100 ms in QEMU — enough for httpd to send the HTTP response before
     * esp_restart() tears down the chip, small enough to avoid the starvation window. */
    #if (QEMU_BUILD)
        vTaskDelay(pdMS_TO_TICKS(100));
    #else
        vTaskDelay(pdMS_TO_TICKS(REBOOT_DELAY_MS));
    #endif

    #if (QEMU_BUILD)
        /* Silence the LACT timer before rebooting. In QEMU, SW_CPU_RESET does not
         * reinitialize the .data section (unlike real hardware), so s_alarm_handler
         * in BSS becomes NULL. If the LACT interrupt fires between the reset and
         * esp_timer_impl_init(), it calls through a NULL pointer -> panic -> boot-loop.
         * Stopping LACT here prevents this (bug 05 mitigation). */
        REG_WRITE(TIMG_LACTCONFIG_REG(0), 0);
        REG_CLR_BIT(TIMG_INT_ENA_TIMERS_REG(0), TIMG_LACT_INT_ENA);
        REG_WRITE(TIMG_INT_CLR_TIMERS_REG(0), TIMG_LACT_INT_CLR);
    #endif

    esp_restart();
}

void cmd_reboot_device(void)
{
    ESP_LOGI(TAG, "Scheduling device reboot");
    xTaskCreate(reboot_task, "reboot_task", REBOOT_TASK_STACK_SIZE, NULL, REBOOT_TASK_PRIORITY, NULL);
}

static int cmd_get_code(const char *cmd_str)
{
    if (cmd_str == NULL) {
        return CMD_UNKNOWN;
    }

    for (size_t i = 0; i < ARRAY_SIZE(available_commands); i++) {
        if (strncmp(cmd_str, available_commands[i].cmd_name, CMD_NAME_MAX_LEN) == 0) {
            return available_commands[i].cmd_code;
        }
    }

    return CMD_UNKNOWN;
}

static esp_err_t cmd_execute(int cmd_code)
{
    ESP_LOGI(TAG, "Executing command with code: %d", cmd_code);

    esp_err_t result = ESP_OK;

    switch (cmd_code) {
    case CMD_REBOOT:
        cmd_reboot_device();
        break;

#if (QEMU_BUILD)
    case CMD_REBOOT_RAW:
        xTaskCreate(reboot_raw_task, "reboot_raw", REBOOT_TASK_STACK_SIZE, NULL, REBOOT_TASK_PRIORITY, NULL);
        break;

    case CMD_INTR_DUMP:
        /* Dump the CPU interrupt allocation table to stdout (QEMU serial).
         * Useful for identifying which CPU slot a given peripheral uses (e.g. bug 01). */
        esp_intr_dump(NULL);
        break;

    case CMD_UART1_TX:
        /* Flood UART1 TX to provoke a pending UART1 TX interrupt.
         * If port teardown happens immediately after (uart_driver_delete),
         * code WITHOUT fix 01 would land in xt_unhandled_interrupt. */
        if (uart_is_driver_installed(UART_NUM_1)) {
            static const uint8_t buf[256] = {0xAA};
            for (int i = 0; i < 8; i++) {
                uart_write_bytes(UART_NUM_1, (const char *)buf, sizeof(buf));
            }
        }
        break;
#endif

    case CMD_SET_DEFAULT_SETTINGS:
        settings_save_timer_auto_init();
        settings_save_timer_wait();
        if (setting_items_set_defaults(false) != ESP_OK) {
            result = ESP_FAIL;
        } else {
            settings_update();
            ESP_LOGI(TAG, "All default settings applied successfully");
        }
        break;

    default:
        ESP_LOGW(TAG, "Unknown command code: %d", cmd_code);
        result = ESP_FAIL;
        break;
    }

    return result;
}

static esp_err_t cmd_validate_request(cJSON *request_json, char *cmd_str, size_t cmd_str_size)
{
    if ((request_json == NULL) || (cmd_str == NULL)) {
        return ESP_FAIL;
    }

    // Check if command field exists
    if (!cJSON_HasObjectItem(request_json, "cmd")) {
        ESP_LOGW(TAG, "No command field in request");
        return ESP_FAIL;
    }

    cJSON *cmd_item = cJSON_GetObjectItem(request_json, "cmd");
    if (!cJSON_IsString(cmd_item)) {
        ESP_LOGW(TAG, "Command field is not a string");
        return ESP_FAIL;
    }

    // Copy command string
    strncpy(cmd_str, cmd_item->valuestring, cmd_str_size - 1);
    cmd_str[cmd_str_size - 1] = '\0';

    return ESP_OK;
}

esp_err_t cmd_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Command POST request received");

    if (!auth_middleware_check(req)) {
        // Func will send 401 Unauthorized if auth fails
        return ESP_OK;
    }

    cJSON *request_json = json_utils_receive_json(req);
    if (request_json == NULL) {
        return json_utils_send_error(req, "Invalid command request JSON");
    }

    // Validate request and extract command
    char cmd_str[CMD_NAME_MAX_LEN] = {0};
    if (cmd_validate_request(request_json, cmd_str, sizeof(cmd_str)) != ESP_OK) {
        json_utils_cleanup(request_json, NULL);
        return json_utils_send_error(req, "Invalid command request");
    }

    int cmd_code = cmd_get_code(cmd_str);
    if (cmd_code == CMD_UNKNOWN) {
        ESP_LOGW(TAG, "Unknown command: %s", cmd_str);
        json_utils_cleanup(request_json, NULL);
        return json_utils_send_error(req, "Unknown command");
    }

    esp_err_t exec_result = cmd_execute(cmd_code);
    if (exec_result != ESP_OK) {
        ESP_LOGE(TAG, "Command execution failed: %s", cmd_str);
        json_utils_cleanup(request_json, NULL);
        return json_utils_send_error(req, "Command execution failed");
    }

    // Create success response
    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        json_utils_cleanup(request_json, NULL);
        return json_utils_send_error(req, "Failed to create response");
    }

    cJSON_AddBoolToObject(response_json, "success", true);
    cJSON_AddStringToObject(response_json, "command", cmd_str);

    json_utils_send_response(req, request_json, response_json);

    return ESP_OK;
}
