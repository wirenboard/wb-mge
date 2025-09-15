#ifndef ESP_LOG_H_
#define ESP_LOG_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_LOG_NONE,       /*!< No log output */
    ESP_LOG_ERROR,      /*!< Critical errors, software module can not recover on its own */
    ESP_LOG_WARN,       /*!< Error conditions from which recovery measures have been taken */
    ESP_LOG_INFO,       /*!< Information messages which describe normal flow of events */
    ESP_LOG_DEBUG,      /*!< Extra information which is not necessary for normal use (values, pointers, sizes, etc). */
    ESP_LOG_VERBOSE     /*!< Bigger chunks of debugging information, or frequent messages which can potentially flood the output. */
} esp_log_level_t;

#define ESP_LOGE(tag, format, ...) printf("[ERROR] %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) printf("[WARN] %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) printf("[INFO] %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) printf("[DEBUG] %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) printf("[VERBOSE] %s: " format "\n", tag, ##__VA_ARGS__)

static inline void esp_log_level_set(const char* tag, esp_log_level_t level)
{
    (void)tag;
    (void)level;
}

#ifdef __cplusplus
}
#endif

#endif /* ESP_LOG_H_ */