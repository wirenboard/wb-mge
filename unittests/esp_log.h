#pragma once

#include <stdio.h>

typedef enum {
    ESP_LOG_NONE    = 0,    /*!< No log output */
    ESP_LOG_ERROR   = 1,    /*!< Critical errors, software module can not recover on its own */
    ESP_LOG_WARN    = 2,    /*!< Error conditions from which recovery measures have been taken */
    ESP_LOG_INFO    = 3,    /*!< Information messages which describe normal flow of events */
    ESP_LOG_DEBUG   = 4,    /*!< Extra information which is not necessary for normal use (values, pointers, sizes, etc). */
    ESP_LOG_VERBOSE = 5,    /*!< Bigger chunks of debugging information, or frequent messages which can potentially flood the output. */
    ESP_LOG_MAX     = 6,    /*!< Number of levels supported */
} esp_log_level_t;

#define ESP_LOGE(tag, format, ...) printf("[ERROR] %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) printf("[WARN] %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) printf("[INFO] %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) printf("[DEBUG] %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) printf("[VERBOSE] %s: " format "\n", tag, ##__VA_ARGS__)

void esp_log_level_set(const char *tag, esp_log_level_t level);
