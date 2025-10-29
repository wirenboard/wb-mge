#pragma once

#include "FreeRTOS.h"

#define MOCK_TASK_HANDLE_T              ((TaskHandle_t)0xDEADBEEF)

typedef void (*TaskFunction_t)(void *);

extern int mock_xTaskCreate_called;
extern TaskFunction_t mock_xTaskCreate_pvTaskCode;
extern const char *mock_xTaskCreate_pcName;
extern uint32_t mock_xTaskCreate_usStackDepth;
extern void *mock_xTaskCreate_pvParameters;
extern UBaseType_t mock_xTaskCreate_uxPriority;
extern TaskHandle_t mock_xTaskCreate_pxCreatedTask;
extern BaseType_t mock_xTaskCreate_return_value;

extern int mock_vTaskDelete_called;
extern TaskHandle_t mock_vTaskDelete_xTaskToDelete;

extern int mock_vTaskDelay_called;
extern TickType_t mock_vTaskDelay_xTicksToDelay;

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
