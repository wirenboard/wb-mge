#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  unsigned task_priority;
  size_t stack_size;
  BaseType_t core_id;
  uint8_t ttl;
  uint16_t port;
  uint32_t interval;
  uint16_t mx_max_delay;
  const char* uuid_root;
  const char* uuid;
  const char* schema_url;
  const char* device_type;
  const char* friendly_name;
  const char* serial_number;
  const char* presentation_url;
  const char* manufacturer_name;
  const char* manufacturer_url;
  const char* model_name;
  const char* model_url;
  const char* model_number;
  const char* model_description;
  const char* server_name;
  const char* services_description;
  const char* icons_description;
} ssdp_config_t;

#define SDDP_DEFAULT_CONFIG()                                               \
  {                                                                         \
    .task_priority = tskIDLE_PRIORITY + 5, .stack_size = 4096,              \
    .core_id = tskNO_AFFINITY, .ttl = 2, .port = 80, .interval = 1200,      \
    .mx_max_delay = 10000, .uuid_root = NULL, .uuid = NULL,                 \
    .schema_url = "description.xml", .device_type = "Basic",                \
    .friendly_name = NULL, .serial_number = NULL,                       \
    .presentation_url = "/", .manufacturer_name = "Wiren Board",      \
    .manufacturer_url = "https://wirenboard.com", .model_name = NULL,   \
    .model_url = NULL, .model_number = NULL,                                \
    .model_description = NULL, .server_name = "SSDPServer/1.0",             \
    .services_description = NULL, .icons_description = NULL                 \
  }

esp_err_t ssdp_init();

esp_err_t ssdp_start(ssdp_config_t* configuration);

esp_err_t ssdp_stop();

const char* get_ssdp_schema_str();

#ifdef __cplusplus
}
#endif
