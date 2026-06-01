#pragma once

/* Minimal mock of esp_system.h for the mb_device unit test.
 * Provides the esp_reset_reason_t enum (names match ESP-IDF; numeric values
 * are not relied upon by mb_device.c, only the enum names) and a controllable
 * esp_reset_reason(). */

typedef enum {
    ESP_RST_UNKNOWN = 0,   /* Reset reason can not be determined */
    ESP_RST_POWERON,       /* Reset due to power-on event */
    ESP_RST_EXT,           /* Reset by external pin (not applicable for ESP32) */
    ESP_RST_SW,            /* Software reset via esp_restart */
    ESP_RST_PANIC,         /* Software reset due to exception/panic */
    ESP_RST_INT_WDT,       /* Reset (software or hardware) due to interrupt watchdog */
    ESP_RST_TASK_WDT,      /* Reset due to task watchdog */
    ESP_RST_WDT,           /* Reset due to other watchdogs */
    ESP_RST_DEEPSLEEP,     /* Reset after exiting deep sleep mode */
    ESP_RST_BROWNOUT,      /* Brownout reset (software or hardware) */
    ESP_RST_SDIO,          /* Reset over SDIO */
} esp_reset_reason_t;

extern esp_reset_reason_t mock_reset_reason;

esp_reset_reason_t esp_reset_reason(void);

void mock_set_reset_reason(esp_reset_reason_t reason);
void mock_reset_reason_reset(void);
