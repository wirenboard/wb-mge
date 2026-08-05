#pragma once

/* Mock of the ESP-IDF reset-reason API. auth_init() restores the saved sessions only after a
 * software restart (esp_restart()), so the tests need to choose the reason. */

typedef enum {
    ESP_RST_UNKNOWN,
    ESP_RST_POWERON,
    ESP_RST_EXT,
    ESP_RST_SW,             /* Software reset via esp_restart() */
    ESP_RST_PANIC,
    ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT,
    ESP_RST_WDT,
    ESP_RST_DEEPSLEEP,
    ESP_RST_BROWNOUT,
    ESP_RST_SDIO,
} esp_reset_reason_t;

esp_reset_reason_t esp_reset_reason(void);

extern int mock_esp_reset_reason_called;

void mock_esp_system_reset(void);
void mock_esp_system_set_reset_reason(esp_reset_reason_t reason);
