#ifndef CMD_HANDLER_H
#define CMD_HANDLER_H

#include <esp_http_server.h>
#include "cJSON.h"

// Command codes
typedef enum {
    CMD_REBOOT,
    CMD_SET_DEFAULT_SETTINGS,
    CMD_WRITE_FACTORY_DATA,
} cmd_code_t;

// Command structure
typedef struct {
    int cmd_code;
    const char *cmd_name;
    const char *description;
} cmd_t;

/**
 * @brief Initialize command handler module
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t cmd_handler_init(void);

/**
 * @brief HTTP handler for command POST endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t cmd_post_handler(httpd_req_t *req);

/**
 * @brief Get command code from command string
 * @param cmd_str Command string
 * @return Command code or -1 if not found
 */
int cmd_get_code(const char *cmd_str);

/**
 * @brief Execute command by code
 * @param cmd_code Command code to execute
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t cmd_execute(int cmd_code);

/**
 * @brief Get available commands list
 * @return Array of available commands
 */
const cmd_t *cmd_get_available_commands(void);

/**
 * @brief Get number of available commands
 * @return Number of available commands
 */
size_t cmd_get_command_count(void);

/**
 * @brief Reboot device (async)
 */
void cmd_reboot_device(void);

#endif // CMD_HANDLER_H