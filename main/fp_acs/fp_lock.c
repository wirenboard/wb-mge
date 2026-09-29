#include "fp_lock.h"

#include "fp_acs.h"

#if FP_ACS_ENABLED

#include <stdint.h>
#include <string.h>

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
// A pulse waiting to be taken plus the manual commands and settings checks sent while a pulse
// runs, run after it.
#define FP_LOCK_QUEUE_LENGTH        4
#define FP_LOCK_TASK_STACK_SIZE     (4 * 1024)  // as main/mb_master/user_app.c: the same call path
#define FP_LOCK_TASK_PRIORITY       5

typedef enum {
    FP_LOCK_REQUEST_PULSE,                      // fp_lock_open()
    FP_LOCK_REQUEST_ON,                         // fp_lock_set(true)
    FP_LOCK_REQUEST_OFF,                        // fp_lock_set(false)
    FP_LOCK_REQUEST_SYNC,                       // fp_lock_settings_changed()
} fp_lock_request_t;

// The relay the lock uses and the state that opens the door, as the settings give them.
typedef struct {
    uint8_t  unit;
    uint16_t coil;
    bool     open;                              // the coil level that opens the door: ON for a strike
} fp_lock_target_t;

static QueueHandle_t fp_lock_queue;

// True from the moment an open request is taken until its pulse has ended (or was skipped); read and written through
// the atomic builtins, since fp_lock_open() is called from the sensor task and the bot alike.
static bool fp_lock_busy;

// The relay as of the last coil write that succeeded: written by the lock task, read by the web
// server, hence the atomic builtins as well.
static bool fp_lock_relay_state;


// One coil write, retried: a relay module that missed a frame on a noisy bus answers the next.
static esp_err_t fp_lock_write(uint8_t unit, uint16_t coil, bool on)
{
    esp_err_t err = ESP_FAIL;
    for (unsigned attempt = 0; attempt <= FP_LOCK_WRITE_RETRIES; attempt++) {
        err = mb_master_write_coil(FP_LOCK_PORT, unit, coil, on);
        if (err == ESP_OK) {
            __atomic_store_n(&fp_lock_relay_state, on, __ATOMIC_SEQ_CST);
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

// Read before every request (fp_lock_sync()), so a changed setting applies to the next one.
static void fp_lock_read_target(fp_lock_target_t *target)
{
    target->unit = (uint8_t)setting_items_read_int(KEY_LOCK_ADDR);
    target->coil = (uint16_t)(setting_items_read_int(KEY_LOCK_CHANNEL) - FP_LOCK_FIRST_CHANNEL);

    // Anything but "magnet" is the strike, the default: the value is validated on the way in.
    char mode[SETTING_ITEM_MAX_STR_LEN] = {0};
    (void)setting_items_read(KEY_LOCK_MODE, mode);
    target->open = (strcmp(mode, LOCK_MODE_MAGNET_STR) != 0);
}

static const char *fp_lock_mode_str(bool open)
{
    return open ? LOCK_MODE_STRIKE_STR : LOCK_MODE_MAGNET_STR;
}

// The target comes from the last sync, not from the settings, so the closing write goes to the same
// relay, in the same sense, as the opening one even if the settings change during the pulse.
static void fp_lock_pulse(const fp_lock_target_t *target)
{
    int pulse_ms = setting_items_read_int(KEY_LOCK_PULSE_MS);

    ESP_LOGI(TAG, "Opening: unit %u, channel %u, %s, %d ms", (unsigned)target->unit,
             (unsigned)(target->coil + FP_LOCK_FIRST_CHANNEL), fp_lock_mode_str(target->open), pulse_ms);
    (void)fp_lock_write(target->unit, target->coil, target->open);
    vTaskDelay(pdMS_TO_TICKS(pulse_ms));
    // Whatever the opening write returned: one that timed out may still have switched the relay,
    // and a door left open is worse than a closing write the relay did not need.
    (void)fp_lock_write(target->unit, target->coil, !target->open);
}

// on is the coil level itself, as the web switch shows it, whatever the mode.
static void fp_lock_switch(const fp_lock_target_t *target, bool on, bool *held)
{
    ESP_LOGI(TAG, "Manual %s: unit %u, channel %u, %s", on ? "ON" : "OFF", (unsigned)target->unit,
             (unsigned)(target->coil + FP_LOCK_FIRST_CHANNEL), fp_lock_mode_str(target->open));
    if (on == target->open) {
        // Held only once the relay did open: a hold over a relay left closed would skip every
        // opening, while the web UI, showing the relay unchanged, offers nothing but the same
        // command again.
        if (fp_lock_write(target->unit, target->coil, on) == ESP_OK) {
            *held = true;
        }
    } else {
        *held = false;
        (void)fp_lock_write(target->unit, target->coil, on);
    }
}

// Brings the relay in line with the settings before a request is handled. When the unit id, the
// channel or the mode differs from the applied one, a relay the lock no longer uses is switched
// OFF (released), the one it now uses is put in the closed state, and the hold is dropped: it was
// on the old relay, or in the old sense of the coil. The new target counts as applied whatever
// the writes return; the next pulse ends in the closed state anyway.
static void fp_lock_sync(fp_lock_target_t *applied, bool *held)
{
    fp_lock_target_t current;
    fp_lock_read_target(&current);

    bool moved = ((current.unit != applied->unit) || (current.coil != applied->coil));
    if ((!moved) && (current.open == applied->open)) {
        return;
    }

    ESP_LOGI(TAG, "Lock settings changed: unit %u, channel %u, %s", (unsigned)current.unit,
             (unsigned)(current.coil + FP_LOCK_FIRST_CHANNEL), fp_lock_mode_str(current.open));
    if (moved) {
        (void)fp_lock_write(applied->unit, applied->coil, false);
    }
    (void)fp_lock_write(current.unit, current.coil, !current.open);
    *held = false;
    *applied = current;
}

static void fp_lock_task(void *arg)
{
    (void)arg;

    // Only this task uses these. applied is the relay and the mode last applied, at boot or by a
    // sync; held is set by a manual command to the open state and cleared by one to the closed
    // state or by a sync that found the settings changed.
    fp_lock_target_t applied;
    bool held = false;

    // A reboot in the middle of a pulse or a hold leaves the relay module as it was: it has its own
    // power and keeps its coil, so the door would stay open until the next opening.
    fp_lock_read_target(&applied);
    (void)fp_lock_write(applied.unit, applied.coil, !applied.open);

    for (;;) {
        fp_lock_request_t request = FP_LOCK_REQUEST_PULSE;
        if (xQueueReceive(fp_lock_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        fp_lock_sync(&applied, &held);
        switch (request) {
        case FP_LOCK_REQUEST_PULSE:
            // The door is already open, and the pulse would end by closing it.
            if (held) {
                ESP_LOGI(TAG, "Opening skipped: the relay is held open by hand");
            } else {
                fp_lock_pulse(&applied);
            }
            __atomic_store_n(&fp_lock_busy, false, __ATOMIC_SEQ_CST);
            break;
        case FP_LOCK_REQUEST_ON:
            fp_lock_switch(&applied, true, &held);
            break;
        case FP_LOCK_REQUEST_OFF:
            fp_lock_switch(&applied, false, &held);
            break;
        case FP_LOCK_REQUEST_SYNC:
            // The sync above is the whole request.
            break;
        }
    }
}

void fp_lock_start(void)
{
    fp_lock_queue = xQueueCreate(FP_LOCK_QUEUE_LENGTH, sizeof(fp_lock_request_t));
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

    fp_lock_request_t request = FP_LOCK_REQUEST_PULSE;
    if (xQueueSend(fp_lock_queue, &request, 0) != pdTRUE) {
        __atomic_store_n(&fp_lock_busy, false, __ATOMIC_SEQ_CST);
        return false;
    }
    return true;
}

bool fp_lock_set(bool on)
{
    if (fp_lock_queue == NULL) {
        ESP_LOGE(TAG, "Manual %s ignored: the lock task is not running", on ? "ON" : "OFF");
        return false;
    }

    fp_lock_request_t request = on ? FP_LOCK_REQUEST_ON : FP_LOCK_REQUEST_OFF;
    if (xQueueSend(fp_lock_queue, &request, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Manual %s dropped: the request queue is full", on ? "ON" : "OFF");
        return false;
    }
    return true;
}

void fp_lock_settings_changed(void)
{
    // The task was never started, and fp_lock_start() has said why.
    if (fp_lock_queue == NULL) {
        return;
    }

    // A full queue loses nothing: every request in it runs the same sync first.
    fp_lock_request_t request = FP_LOCK_REQUEST_SYNC;
    if (xQueueSend(fp_lock_queue, &request, 0) != pdTRUE) {
        ESP_LOGD(TAG, "Settings check not queued: the queue is full, and each request in it checks first");
    }
}

bool fp_lock_relay_on(void)
{
    return __atomic_load_n(&fp_lock_relay_state, __ATOMIC_SEQ_CST);
}

#endif // FP_ACS_ENABLED
