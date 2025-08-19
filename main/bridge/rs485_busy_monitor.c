#include "sys_info.h"
#include "freertos/FreeRTOS.h"
#include "array_size.h"

//------------------------------------------------------------------------------

#define RS485_BUSY_MONITOR_TIMEOUT_MS           5000

#define RS485_BUSY_MONITOR_TASK_STACK_SIZE      1024
#define RS485_BUSY_MONITOR_TASK_PRIORITY        1

#define MAX(a, b)                               ((a) > (b) ? (a) : (b))

//------------------------------------------------------------------------------

static TickType_t last_activity_tick[2] = {0, 0};   // FreeRTOS ticks

//------------------------------------------------------------------------------

static void rs485_busy_monitor_task(void *arg)
{
    static const TickType_t timeout = pdMS_TO_TICKS(RS485_BUSY_MONITOR_TIMEOUT_MS);
    static const TickType_t delay = MAX(10, timeout / 10);

    while (1) {
        TickType_t now_tick = xTaskGetTickCount();
        // Port 1
        if (sys_info.rs485_1_is_busy && (now_tick - last_activity_tick[0]) > timeout) {
            sys_info.rs485_1_is_busy = false;
        }
        // Port 2
        if (sys_info.rs485_2_is_busy && (now_tick - last_activity_tick[1]) > timeout) {
            sys_info.rs485_2_is_busy = false;
        }
        vTaskDelay(delay);
    }
}

//------------------------------------------------------------------------------

void rs485_busy_monitor_init(void)
{
    sys_info.rs485_1_is_busy = false;
    sys_info.rs485_2_is_busy = false;

    xTaskCreate(rs485_busy_monitor_task, "rs485_busy_monitor_task", RS485_BUSY_MONITOR_TASK_STACK_SIZE,
                NULL, RS485_BUSY_MONITOR_TASK_PRIORITY, NULL);
}

//------------------------------------------------------------------------------

void rs485_busy_monitor_update_activity(int index)
{
    if (index >= ARRAY_SIZE(last_activity_tick)) {
        return;
    }

    last_activity_tick[index] = xTaskGetTickCount();
    if (index == 0) {
        sys_info.rs485_1_is_busy = true;
    } else if (index == 1) {
        sys_info.rs485_2_is_busy = true;
    }
}

//------------------------------------------------------------------------------
