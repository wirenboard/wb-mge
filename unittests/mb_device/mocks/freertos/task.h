#pragma once

#include "freertos/FreeRTOS.h"

/* TaskHandle_t is referenced by serial.h's serial_desc_t and by
 * uxTaskGetStackHighWaterMark(). */
typedef void *TaskHandle_t;

/*
 * Controllable mock for uxTaskGetStackHighWaterMark().
 * Returns the configured high-water-mark value (in StackType_t words). The
 * argument is ignored. Set mock_stack_high_water_mark before calling the code
 * under test; reset via mock_freertos_task_reset().
 */
extern UBaseType_t mock_stack_high_water_mark;

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask);

void mock_set_stack_high_water_mark(UBaseType_t words);
void mock_freertos_task_reset(void);
