#pragma once

#include "FreeRTOS.h"
#include <stdbool.h>

typedef void *TaskHandle_t;

typedef void (*TaskFunction_t)(void *);

typedef struct {
    bool should_fail;
    int called;
    bool self_execution;
    TaskFunction_t pvTaskCode;
    const char *pcName;
    uint32_t usStackDepth;
    void *pvParameters;
    UBaseType_t uxPriority;
    TaskHandle_t *pxCreatedTask;
} mock_xTaskCreate_t;

typedef struct {
    int called;
    TaskHandle_t xTaskToDelete;
} mock_vTaskDelete_t;

typedef struct {
    int called;
    TickType_t xTicksToDelay;
    int counter;
    int task_handle_reset_on_count;
} mock_vTaskDelay_t;

extern mock_xTaskCreate_t mock_xTaskCreate_data;
extern mock_vTaskDelete_t mock_vTaskDelete_data;
extern mock_vTaskDelay_t mock_vTaskDelay_data;

void mock_freertos_task_reset(void);

BaseType_t xTaskCreate(TaskFunction_t pvTaskCode,
                       const char * const pcName,
                       const uint32_t usStackDepth,
                       void * const pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t * const pxCreatedTask);

void vTaskDelete(TaskHandle_t xTaskToDelete);
void vTaskDelay(const TickType_t xTicksToDelay);

#define pdMS_TO_TICKS(ms)   mock_pdMS_TO_TICKS(ms)
TickType_t mock_pdMS_TO_TICKS(TickType_t ms);
