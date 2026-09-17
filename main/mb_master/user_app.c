/*
 * THIS FILE IS WHERE A DIY USER PUTS THEIR OWN LOGIC.
 *
 * This is the master-role twin of main/mb_slave/user_app.c: the firmware was built with
 * MB_ROLE=master, so the device drives both RS-485 buses instead of answering them.
 * mb_master.c is plumbing - the two RTU master instances, the UART pins, the RS-485
 * direction control. This file is the example consumer, and it is meant to be replaced.
 *
 * The pattern it demonstrates is the whole contract between the Modbus master and the
 * application, and it is the reason the API is blocking:
 *
 *   - poll from a task of YOUR OWN, as below. mb_master_read_holding() and its two
 *     siblings block until the slave answers or the request times out, so the waiting
 *     happens on your task and on nobody else's;
 *   - a device that is not there comes back as ESP_ERR_TIMEOUT, distinct from every other
 *     failure. Poll loops live or die on telling "silent" apart from "broken", so check
 *     for it rather than for "not ESP_OK";
 *   - nothing stops two tasks polling the same bus: mb_master.c holds one mutex per port,
 *     so concurrent requests queue instead of interleaving.
 *
 * The three calls available are declared in mb_master.h:
 *     mb_master_read_holding (port, slave_id, addr, count, out)
 *     mb_master_read_input   (port, slave_id, addr, count, out)
 *     mb_master_write_holding(port, slave_id, addr, value)
 * with port 0 = RS-485 port 1 and port 1 = RS-485 port 2.
 */

#include "user_app.h"

#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mb_master.h"

static const char *TAG = "user_app";

#define USER_APP_TASK_STACK_SIZE    (4 * 1024)
#define USER_APP_TASK_PRIORITY      4

// What the example polls, once a second, on every RS-485 port: three holding registers
// starting at 0 from unit id 1. Change these - or the whole task - to whatever the device
// on your bus actually is.
#define POLL_SLAVE_ID               1
#define POLL_REG_ADDR               0
#define POLL_REG_COUNT              3
#define POLL_PERIOD_MS              1000


static void user_app_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Polling %u holding registers at %u from unit %u on both RS-485 ports, "
                  "every %u ms",
             (unsigned)POLL_REG_COUNT, (unsigned)POLL_REG_ADDR, (unsigned)POLL_SLAVE_ID,
             (unsigned)POLL_PERIOD_MS);

    while (1) {
        for (unsigned port = 0; port < RS485_PORTS_COUNT; port++) {
            uint16_t values[POLL_REG_COUNT] = {0};
            esp_err_t err = mb_master_read_holding((uint8_t)port, POLL_SLAVE_ID,
                                                   POLL_REG_ADDR, POLL_REG_COUNT, values);
            if (err == ESP_OK) {
                // Replace this line with whatever the values should actually do.
                ESP_LOGI(TAG, "port %u unit %u holding[%u..%u] = 0x%04X 0x%04X 0x%04X",
                         port + 1, (unsigned)POLL_SLAVE_ID, (unsigned)POLL_REG_ADDR,
                         (unsigned)(POLL_REG_ADDR + POLL_REG_COUNT - 1),
                         values[0], values[1], values[2]);
            } else if (err == ESP_ERR_TIMEOUT) {
                // The ordinary "nobody answered" case: no device at that unit id on this
                // bus, or it is powered down. Worth its own branch precisely because it is
                // the expected failure rather than a broken one.
                ESP_LOGW(TAG, "port %u unit %u: no reply (timeout)",
                         port + 1, (unsigned)POLL_SLAVE_ID);
            } else {
                ESP_LOGE(TAG, "port %u unit %u: read failed: %s",
                         port + 1, (unsigned)POLL_SLAVE_ID, esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
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
