#include "cmd_handler.h"
#include "json_utils.h"
#include "auth.h"
#include "setting_items.h"
#include "sys_info.h"

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char *TAG = "cmd_handler";

#define CMD_NAME_MAX_LEN        32
#define REBOOT_DELAY_MS         1000

#define REBOOT_TASK_STACK_SIZE  2048
#define REBOOT_TASK_PRIORITY    5

typedef enum {
    CMD_REBOOT,
    CMD_SET_DEFAULT_SETTINGS,
    CMD_WRITE_FACTORY_DATA,
} cmd_code_t;

typedef struct {
    int cmd_code;
    const char *cmd_name;
    const char *description;
} cmd_t;

static const cmd_t available_commands[] = {
    {CMD_REBOOT, "reboot", "Restart the device"},
    {CMD_SET_DEFAULT_SETTINGS, "set_default_settings", "Reset all settings to factory defaults"},
    {CMD_WRITE_FACTORY_DATA, "write_factory_data", "Write factory calibration data to storage"},
    {-1, NULL, NULL},  // Terminator
};

static void reboot_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Executing reboot command");

    vTaskDelay(pdMS_TO_TICKS(REBOOT_DELAY_MS));
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
        return -1;
    }

    for (int i = 0; available_commands[i].cmd_name != NULL; i++) {
        if (strncmp(cmd_str, available_commands[i].cmd_name, CMD_NAME_MAX_LEN) == 0) {
            return available_commands[i].cmd_code;
        }
    }

    return -1;
}

static esp_err_t cmd_execute(int cmd_code)
{
    ESP_LOGI(TAG, "Executing command with code: %d", cmd_code);

    esp_err_t result = ESP_OK;

    switch (cmd_code) {
        case CMD_REBOOT:
            cmd_reboot_device();
            break;

        case CMD_SET_DEFAULT_SETTINGS:
            if (setting_items_set_defaults() != 0) {
                ESP_LOGE(TAG, "Failed to set default settings");
                result = ESP_FAIL;
            } else {
                ESP_LOGI(TAG, "Default settings applied successfully");
            }
            break;

        case CMD_WRITE_FACTORY_DATA:
            if (sys_info_write_factory_data() != ESP_OK) {
                ESP_LOGE(TAG, "Failed to write factory data");
                result = ESP_FAIL;
            } else {
                ESP_LOGI(TAG, "Factory data written successfully");
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
        return ESP_OK;
    }

    cJSON *request_json = json_utils_receive_json(req);
    if (request_json == NULL) {
        return ESP_FAIL;
    }

    // Validate request and extract command
    char cmd_str[CMD_NAME_MAX_LEN] = {0};
    if (cmd_validate_request(request_json, cmd_str, sizeof(cmd_str)) != ESP_OK) {
        json_utils_cleanup(request_json, NULL);
        return json_utils_send_error(req, "Invalid command request");
    }

    int cmd_code = cmd_get_code(cmd_str);
    if (cmd_code == -1) {
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
