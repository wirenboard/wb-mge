#include "sys_info.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

//------------------------------------------------------------------------------

#define RS485_PORT_COUNT                        2

#define RS485_BUSY_MONITOR_TIMEOUT_MS           5000

#define RS485_BUSY_MONITOR_TASK_STACK_SIZE      1024
#define RS485_BUSY_MONITOR_TASK_PRIORITY        1

#define MAX(a, b)                               ((a) > (b) ? (a) : (b))

//------------------------------------------------------------------------------

static TickType_t last_activity_tick[RS485_PORT_COUNT] = {0, 0};    // FreeRTOS ticks

static unsigned stats_total[RS485_PORT_COUNT] = {0, 0};
static unsigned stats_success[RS485_PORT_COUNT] = {0, 0};

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
    if (index >= RS485_PORT_COUNT) {
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

void rs485_stats_init(void)
{
    memset(stats_total, 0, sizeof(stats_total));
    memset(stats_success, 0, sizeof(stats_success));
    sys_info.rs485_1_error_percentage = 0;
    sys_info.rs485_2_error_percentage = 0;
}

//------------------------------------------------------------------------------

void rs485_stats_update(int index, unsigned count, unsigned success)
{
    if (index >= RS485_PORT_COUNT) {
        return;
    }

    stats_total[index] += count;
    stats_success[index] += success;

    uint8_t err_percent = 0;
    if (stats_total[index]) {
        unsigned errors = stats_total[index] - stats_success[index];
        err_percent = ((100UL * errors) + (stats_total[index] / 2)) / stats_total[index];
    }

    if (index == 0) {
        sys_info.rs485_1_error_percentage = err_percent;
    } else if (index == 1) {
        sys_info.rs485_2_error_percentage = err_percent;
    }
}

//------------------------------------------------------------------------------