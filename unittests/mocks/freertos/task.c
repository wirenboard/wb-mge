#include "unity.h"

#include "task.h"
#include <stddef.h>
#include <string.h>

#define MOCK_TASK_HANDLE                ((TaskHandle_t)0xCCCCCCCC)
#define CONFIG_FREERTOS_HZ              500

mock_xTaskCreate_t mock_xTaskCreate_data = {0};
mock_vTaskDelete_t mock_vTaskDelete_data = {0};
mock_vTaskDelay_t mock_vTaskDelay_data = {0};

BaseType_t xTaskCreate(TaskFunction_t pvTaskCode,
                       const char * const pcName,
                       const uint32_t usStackDepth,
                       void * const pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t * const pxCreatedTask)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(pvTaskCode, "Task function should not be NULL");

    mock_xTaskCreate_data.pvTaskCode = pvTaskCode;
    mock_xTaskCreate_data.pcName = pcName;
    mock_xTaskCreate_data.usStackDepth = usStackDepth;
    mock_xTaskCreate_data.pvParameters = pvParameters;
    mock_xTaskCreate_data.uxPriority = uxPriority;
    mock_xTaskCreate_data.pxCreatedTask = pxCreatedTask;

    mock_xTaskCreate_data.called++;

    if (mock_xTaskCreate_data.should_fail) {
        return pdFAIL;
    }

    if (pxCreatedTask != NULL) {
        *pxCreatedTask = MOCK_TASK_HANDLE;
    }

    if (mock_xTaskCreate_data.self_execution) {
        pvTaskCode(pvParameters);
    }

    return pdPASS;
}

void vTaskDelete(TaskHandle_t xTaskToDelete)
{
    mock_vTaskDelete_data.xTaskToDelete = xTaskToDelete;
    mock_vTaskDelete_data.called++;
}

void vTaskDelay(const TickType_t xTicksToDelay)
{
    mock_vTaskDelay_data.called++;
    mock_vTaskDelay_data.xTicksToDelay = xTicksToDelay;

    if (mock_vTaskDelay_data.task_handle_reset_on_count > 0) {
        if (mock_vTaskDelay_data.called >= mock_vTaskDelay_data.task_handle_reset_on_count) {
            *mock_xTaskCreate_data.pxCreatedTask = NULL;
        }
    }
}

TickType_t mock_pdMS_TO_TICKS(TickType_t ms)
{
    return (ms * CONFIG_FREERTOS_HZ) / 1000;
}

void mock_freertos_task_reset(void)
{
    memset(&mock_xTaskCreate_data, 0, sizeof(mock_xTaskCreate_data));
    memset(&mock_vTaskDelete_data, 0, sizeof(mock_vTaskDelete_data));
    memset(&mock_vTaskDelay_data, 0, sizeof(mock_vTaskDelay_data));
}
