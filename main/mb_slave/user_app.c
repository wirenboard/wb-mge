/*
 * THIS FILE IS WHERE A DIY USER PUTS THEIR OWN LOGIC.
 *
 * Everything else in main/mb_slave/ is plumbing: mb_slave.c owns the three Modbus
 * transports, mb_registers.c owns the register map and the store. This file is the
 * example consumer, and it is meant to be replaced.
 *
 * The pattern it demonstrates is the whole contract between the Modbus stack and the
 * application:
 *
 *   - to REACT to a master write, block on mb_slave_event_queue() from a task of your
 *     own, as below. The Modbus stack never calls into application code, so whatever you
 *     do here — a slow I2C transaction, an HTTP request, a long computation — cannot
 *     delay the answer the slave owes the bus;
 *   - to PUBLISH a value, call mb_reg_set(MB_REG_INPUT, <addr>, <value>) from wherever
 *     that value is produced. mb_registers.c has an example: a 1 Hz task that publishes
 *     uptime, supply voltage and free heap.
 *
 * To add a register of your own, append a row to mb_reg_table[] in mb_registers.c.
 */

#include "user_app.h"

#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mb_registers.h"

static const char *TAG = "user_app";

#define USER_APP_TASK_STACK_SIZE    (3 * 1024)
#define USER_APP_TASK_PRIORITY      4


static const char *src_to_string(mb_event_src_t src)
{
    switch (src) {
    case MB_SRC_RTU_PORT1: return "RTU port 1";
    case MB_SRC_RTU_PORT2: return "RTU port 2";
    case MB_SRC_TCP:       return "TCP";
    default:               return "unknown";
    }
}

static void user_app_task(void *arg)
{
    (void)arg;

    QueueHandle_t queue = mb_slave_event_queue();
    if (queue == NULL) {
        ESP_LOGE(TAG, "The Modbus write event queue does not exist, the example task exits");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Waiting for Modbus register writes");

    while (1) {
        mb_write_event_t event;
        if (xQueueReceive(queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // Replace this line with whatever the write should actually do.
        ESP_LOGI(TAG, "holding %u <- 0x%04X from %s (t=%" PRId64 " us)",
                 event.addr, event.value, src_to_string(event.src), event.timestamp_us);
    }
}

void user_app_start(void)
{
    BaseType_t ret = xTaskCreate(user_app_task, "user_app_task", USER_APP_TASK_STACK_SIZE,
                                 NULL, USER_APP_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the example application task");
    }
}
