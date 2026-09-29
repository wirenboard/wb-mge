#include "fp_lock.h"

#include "fp_acs.h"

#if FP_ACS_ENABLED

#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "mb_master.h"
#include "setting_items.h"

static const char *TAG = "fp_lock";

#define FP_LOCK_PORT                0           // RS-485 port 1 in mb_master's numbering
#define FP_LOCK_FIRST_CHANNEL       1           // lock_channel counts relays from 1, coils from 0
#define FP_LOCK_WRITE_RETRIES       3
// A port being restarted by a settings apply refuses at once; the pause lets it come back up.
#define FP_LOCK_RETRY_DELAY_MS      200
#define FP_LOCK_QUEUE_LENGTH        1
#define FP_LOCK_TASK_STACK_SIZE     (4 * 1024)  // as main/mb_master/user_app.c: the same call path
#define FP_LOCK_TASK_PRIORITY       5

static QueueHandle_t fp_lock_queue;

// True from the moment a request is taken until its pulse has ended; read and written through
// the atomic builtins, since fp_lock_open() is called from the sensor task and the bot alike.
static bool fp_lock_busy;


// One coil write, retried: a relay module that missed a frame on a noisy bus answers the next.
static esp_err_t fp_lock_write(uint8_t unit, uint16_t coil, bool on)
{
    esp_err_t err = ESP_FAIL;
    for (unsigned attempt = 0; attempt <= FP_LOCK_WRITE_RETRIES; attempt++) {
        err = mb_master_write_coil(FP_LOCK_PORT, unit, coil, on);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Unit %u coil %u %s: attempt %u failed: %s", (unsigned)unit, (unsigned)coil,
                 on ? "ON" : "OFF", attempt + 1, esp_err_to_name(err));
        if (attempt < FP_LOCK_WRITE_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(FP_LOCK_RETRY_DELAY_MS));
        }
    }
    ESP_LOGE(TAG, "Unit %u coil %u %s: giving up after %u attempts", (unsigned)unit, (unsigned)coil,
             on ? "ON" : "OFF", FP_LOCK_WRITE_RETRIES + 1);
    return err;
}

// Read at every use, so a changed setting applies to the next opening.
static void fp_lock_read_target(uint8_t *unit, uint16_t *coil)
{
    *unit = (uint8_t)setting_items_read_int(KEY_LOCK_ADDR);
    *coil = (uint16_t)(setting_items_read_int(KEY_LOCK_CHANNEL) - FP_LOCK_FIRST_CHANNEL);
}

static void fp_lock_pulse(void)
{
    uint8_t unit = 0;
    uint16_t coil = 0;
    fp_lock_read_target(&unit, &coil);
    int pulse_ms = setting_items_read_int(KEY_LOCK_PULSE_MS);

    ESP_LOGI(TAG, "Opening: unit %u, channel %u, %d ms", (unsigned)unit, (unsigned)(coil + FP_LOCK_FIRST_CHANNEL),
             pulse_ms);
    (void)fp_lock_write(unit, coil, true);
    vTaskDelay(pdMS_TO_TICKS(pulse_ms));
    // Whatever the ON write returned: one that timed out may still have switched the relay, and a
    // door left open is worse than an OFF the relay did not need.
    (void)fp_lock_write(unit, coil, false);
}

static void fp_lock_task(void *arg)
{
    (void)arg;

    // A reboot in the middle of a pulse leaves the relay module switched on: it has its own
    // power and keeps its coil, so the door would stay open until the next opening.
    uint8_t unit = 0;
    uint16_t coil = 0;
    fp_lock_read_target(&unit, &coil);
    (void)fp_lock_write(unit, coil, false);

    for (;;) {
        uint8_t request = 0;
        if (xQueueReceive(fp_lock_queue, &request, portMAX_DELAY) == pdTRUE) {
            fp_lock_pulse();
            __atomic_store_n(&fp_lock_busy, false, __ATOMIC_SEQ_CST);
        }
    }
}

void fp_lock_start(void)
{
    fp_lock_queue = xQueueCreate(FP_LOCK_QUEUE_LENGTH, sizeof(uint8_t));
    if (fp_lock_queue == NULL) {
        ESP_LOGE(TAG, "No memory for the request queue: the lock will not open");
        return;
    }
    if (xTaskCreate(fp_lock_task, "fp_lock", FP_LOCK_TASK_STACK_SIZE, NULL, FP_LOCK_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the lock task: the lock will not open");
        vQueueDelete(fp_lock_queue);
        fp_lock_queue = NULL;
    }
}

bool fp_lock_open(void)
{
    if (fp_lock_queue == NULL) {
        ESP_LOGE(TAG, "Open request ignored: the lock task is not running");
        return false;
    }

    bool idle = false;
    if (!__atomic_compare_exchange_n(&fp_lock_busy, &idle, true, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        ESP_LOGI(TAG, "Open request dropped: a pulse is already under way");
        return false;
    }

    uint8_t request = 0;
    if (xQueueSend(fp_lock_queue, &request, 0) != pdTRUE) {
        __atomic_store_n(&fp_lock_busy, false, __ATOMIC_SEQ_CST);
        return false;
    }
    return true;
}

#endif // FP_ACS_ENABLED
