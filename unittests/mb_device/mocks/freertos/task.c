#include "freertos/task.h"

/* Current high-water-mark return value (in StackType_t words). */
UBaseType_t mock_stack_high_water_mark = 0;

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask)
{
    (void)xTask;
    return mock_stack_high_water_mark;
}

void mock_set_stack_high_water_mark(UBaseType_t words)
{
    mock_stack_high_water_mark = words;
}

void mock_freertos_task_reset(void)
{
    mock_stack_high_water_mark = 0;
}
