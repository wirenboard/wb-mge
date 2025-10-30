#include "task.h"
#include <stddef.h>

int mock_xTaskCreate_called = 0;
TaskFunction_t mock_xTaskCreate_pvTaskCode = NULL;
const char *mock_xTaskCreate_pcName = NULL;
uint32_t mock_xTaskCreate_usStackDepth = 0;
void *mock_xTaskCreate_pvParameters = NULL;
UBaseType_t mock_xTaskCreate_uxPriority = 0;
TaskHandle_t mock_xTaskCreate_pxCreatedTask = NULL;
BaseType_t mock_xTaskCreate_return_value = pdPASS;

int mock_vTaskDelete_called = 0;
TaskHandle_t mock_vTaskDelete_xTaskToDelete = NULL;

int mock_vTaskDelay_called = 0;
TickType_t mock_vTaskDelay_xTicksToDelay = 0;

BaseType_t xTaskCreate(TaskFunction_t pvTaskCode,
                       const char * const pcName,
                       const uint32_t usStackDepth,
                       void * const pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t * const pxCreatedTask)
{
    mock_xTaskCreate_pvTaskCode = pvTaskCode;
    mock_xTaskCreate_pcName = pcName;
    mock_xTaskCreate_usStackDepth = usStackDepth;
    mock_xTaskCreate_pvParameters = pvParameters;
    mock_xTaskCreate_uxPriority = uxPriority;

    mock_xTaskCreate_called++;

    if (mock_xTaskCreate_return_value == pdPASS && pxCreatedTask != NULL) {
        *pxCreatedTask = MOCK_TASK_HANDLE_T;
        mock_xTaskCreate_pxCreatedTask = *pxCreatedTask;
        pvTaskCode(pvParameters);
    }

    return mock_xTaskCreate_return_value;
}

void vTaskDelete(TaskHandle_t xTaskToDelete)
{
    mock_vTaskDelete_xTaskToDelete = xTaskToDelete;
    mock_vTaskDelete_called++;
}

void vTaskDelay(const TickType_t xTicksToDelay)
{
    mock_vTaskDelay_called++;
    mock_vTaskDelay_xTicksToDelay = xTicksToDelay;
}

TickType_t mock_pdMS_TO_TICKS(TickType_t ms)
{
    return ms;
}

void mock_freertos_task_reset(void)
{
    mock_xTaskCreate_called = 0;
    mock_xTaskCreate_pvTaskCode = NULL;
    mock_xTaskCreate_pcName = NULL;
    mock_xTaskCreate_usStackDepth = 0;
    mock_xTaskCreate_pvParameters = NULL;
    mock_xTaskCreate_uxPriority = 0;
    mock_xTaskCreate_pxCreatedTask = NULL;
    mock_xTaskCreate_return_value = pdPASS;

    mock_vTaskDelete_called = 0;
    mock_vTaskDelete_xTaskToDelete = NULL;

    mock_vTaskDelay_called = 0;
    mock_vTaskDelay_xTicksToDelay = 0;
}
