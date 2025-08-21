#include "sys_info.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

//------------------------------------------------------------------------------

#define RS485_PORT_COUNT                        2       // Количество портов

#define RS485_BUSY_MONITOR_TIMEOUT_MS           5000    // Тайм-аут определения наличия обмена в порте RS-485

#define RS485_BUSY_MONITOR_TASK_STACK_SIZE      1024    // Размер стека задачи мониторинга
#define RS485_BUSY_MONITOR_TASK_PRIORITY        1       // Приоритет задачи мониторинга

#define RS485_STATS_WINDOW_SIZE                 100     // Размер окна (кол-во запросов) для расчета статистики

#define MAX(a, b)                               ((a) > (b) ? (a) : (b))

//------------------------------------------------------------------------------

static TickType_t last_activity_tick[RS485_PORT_COUNT] = {0, 0};    // FreeRTOS ticks

static bool stats_errors[RS485_PORT_COUNT][RS485_STATS_WINDOW_SIZE] = {0};
static unsigned stats_count[RS485_PORT_COUNT] = {0, 0};
static unsigned stats_ptr[RS485_PORT_COUNT] = {0, 0};

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
    memset(stats_count, 0, sizeof(stats_count));
    memset(stats_ptr, 0, sizeof(stats_ptr));
    sys_info.rs485_1_error_percentage = 0;
    sys_info.rs485_2_error_percentage = 0;
}

//------------------------------------------------------------------------------

static void stats_push_result(bool success, bool errors[RS485_STATS_WINDOW_SIZE], unsigned* count, unsigned* ptr)
{
    errors[*ptr] = !success;

    (*ptr)++;
    if (*ptr >= RS485_STATS_WINDOW_SIZE) {
        *ptr = 0;
    }

    if (*count < RS485_STATS_WINDOW_SIZE) {
        (*count)++;
    }
}


static unsigned stats_get_errors_count(bool errors[RS485_STATS_WINDOW_SIZE], unsigned count)
{
    unsigned result = 0;

    for (unsigned i = 0; i < count; i++) {
        if (errors[i]) {
            result++;
        }
    }

    return result;
}

//------------------------------------------------------------------------------

void rs485_stats_update(int index, bool success)
{
    if (index >= RS485_PORT_COUNT) {
        return;
    }

    stats_push_result(success, stats_errors[index], &stats_count[index], &stats_ptr[index]);
    unsigned errors = stats_get_errors_count(stats_errors[index], stats_count[index]);

    uint8_t err_percent = 0;
    if (stats_count[index]) {
        err_percent = ((100UL * errors) + (stats_count[index] / 2)) / stats_count[index];
    }

    if (index == 0) {
        sys_info.rs485_1_error_percentage = err_percent;
    } else if (index == 1) {
        sys_info.rs485_2_error_percentage = err_percent;
    }
}

//------------------------------------------------------------------------------
