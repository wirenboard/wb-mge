#include "mb_registers.h"

#include <inttypes.h>
#include <string.h>

#include "array_size.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "voltage_monitor.h"

static const char *TAG = "mb_registers";

#define MB_EVENT_QUEUE_DEPTH            32
// A master that keeps writing into a queue nobody drains would otherwise produce one log
// line per register written. Rate-limited to one line per interval, carrying the running
// total, so the drops are visible without the log becoming the next bottleneck.
#define MB_EVENT_DROP_LOG_INTERVAL_US   (5 * 1000 * 1000)

#define MB_REFRESH_TASK_STACK_SIZE      (3 * 1024)
#define MB_REFRESH_TASK_PRIORITY        4
#define MB_REFRESH_PERIOD_MS            1000

// ── The register map ─────────────────────────────────────────────────────────────────
//
// ADD YOUR OWN REGISTERS HERE. This table is the single place registers are declared:
// append a row, and the register exists on all three transports (RTU port 1, RTU port 2
// and TCP) at once. Nothing else needs changing — the store below sizes itself from this
// table, and the Modbus hook in mb_slave.c answers ILLEGAL DATA ADDRESS for anything not
// listed here.
//
// Rules the rest of the module relies on:
//   - (kind, addr) must be unique; the first match wins, so a duplicate row is dead;
//   - `writable` is meaningful for holding registers only. An input register is read-only
//     by definition (Modbus has no function code that writes one), so its value reaches
//     the bus only through mb_reg_set();
//   - a holding register with writable = false is readable with FC03 and refused with
//     ILLEGAL DATA ADDRESS on FC06/FC16.
//
// The rows below are the demo content and double as the example to copy: input registers
// 0-3 are published by the refresh task at the bottom of this file, holding registers 0-7
// are read-write scratch that starts at 0 and only a master ever changes.
static const mb_reg_desc_t mb_reg_table[] = {
    // Input registers: device -> master, refreshed once a second by mb_refresh_task().
    {0, MB_REG_INPUT,   false, "uptime_hi"},     // uptime in seconds, high word
    {1, MB_REG_INPUT,   false, "uptime_lo"},     // uptime in seconds, low word
    {2, MB_REG_INPUT,   false, "supply_mv"},     // supply voltage, millivolts
    {3, MB_REG_INPUT,   false, "free_heap_kib"}, // free heap, KiB

    // Holding registers: read-write scratch, default 0.
    {0, MB_REG_HOLDING, true,  "scratch_0"},
    {1, MB_REG_HOLDING, true,  "scratch_1"},
    {2, MB_REG_HOLDING, true,  "scratch_2"},
    {3, MB_REG_HOLDING, true,  "scratch_3"},
    {4, MB_REG_HOLDING, true,  "scratch_4"},
    {5, MB_REG_HOLDING, true,  "scratch_5"},
    {6, MB_REG_HOLDING, true,  "scratch_6"},
    {7, MB_REG_HOLDING, true,  "scratch_7"},
};

// The store, one uint16_t per descriptor, indexed the same way as mb_reg_table[].
static uint16_t          mb_reg_values[ARRAY_SIZE(mb_reg_table)];
static SemaphoreHandle_t mb_reg_mutex;
static QueueHandle_t     mb_event_queue;

static uint32_t mb_events_dropped;
static int64_t  mb_last_drop_log_us;

// Descriptor indices of the demo input registers, so the refresh task does not repeat the
// (kind, addr) pairs the table already states.
#define MB_IDX_UPTIME_HI    0
#define MB_IDX_UPTIME_LO    1
#define MB_IDX_SUPPLY_MV    2
#define MB_IDX_FREE_HEAP    3


// Index into mb_reg_table[] / mb_reg_values[], or -1 when there is no such register.
static int mb_reg_index(mb_reg_kind_t kind, uint16_t addr)
{
    for (size_t i = 0; i < ARRAY_SIZE(mb_reg_table); i++) {
        if ((mb_reg_table[i].kind == kind) && (mb_reg_table[i].addr == addr)) {
            return (int)i;
        }
    }
    return -1;
}

const mb_reg_desc_t *mb_reg_find(mb_reg_kind_t kind, uint16_t addr)
{
    int index = mb_reg_index(kind, addr);
    return (index < 0) ? NULL : &mb_reg_table[index];
}

esp_err_t mb_reg_get(mb_reg_kind_t kind, uint16_t addr, uint16_t *out)
{
    if ((out == NULL) || (mb_reg_mutex == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    int index = mb_reg_index(kind, addr);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    xSemaphoreTake(mb_reg_mutex, portMAX_DELAY);
    *out = mb_reg_values[index];
    xSemaphoreGive(mb_reg_mutex);
    return ESP_OK;
}

esp_err_t mb_reg_set(mb_reg_kind_t kind, uint16_t addr, uint16_t value)
{
    if (mb_reg_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int index = mb_reg_index(kind, addr);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    xSemaphoreTake(mb_reg_mutex, portMAX_DELAY);
    mb_reg_values[index] = value;
    xSemaphoreGive(mb_reg_mutex);
    return ESP_OK;
}

esp_err_t mb_reg_write_from_master(mb_event_src_t src, mb_reg_kind_t kind,
                                   uint16_t addr, uint16_t value)
{
    esp_err_t err = mb_reg_set(kind, addr, value);
    if (err != ESP_OK) {
        return err;
    }

    // Posted AFTER the store is updated, so a consumer woken by the event and calling
    // mb_reg_get() straight away reads the value the event describes rather than the one
    // it replaced.
    const mb_write_event_t event = {
        .src          = src,
        .addr         = addr,
        .value        = value,
        .timestamp_us = esp_timer_get_time(),
    };

    // Timeout 0: this runs on the Modbus stack's task, which owes the master a response
    // inside the RTU inter-frame timeout. A slow consumer must cost the bus nothing, so a
    // full queue drops the event instead of blocking here.
    //
    // Dropping is never silent, which is the whole reason this queue exists rather than
    // esp-modbus's own notification queue: that one discards a full queue with an
    // ESP_LOGD nobody sees in a release build, so a consumer that fell behind looked
    // exactly like a master that never wrote.
    if (xQueueSend(mb_event_queue, &event, 0) != pdTRUE) {
        mb_events_dropped++;
        int64_t now_us = esp_timer_get_time();
        if ((now_us - mb_last_drop_log_us) >= MB_EVENT_DROP_LOG_INTERVAL_US) {
            mb_last_drop_log_us = now_us;
            ESP_LOGW(TAG, "Write event queue full, event dropped (%" PRIu32 " total): "
                          "the application is not draining mb_slave_event_queue()",
                     mb_events_dropped);
        }
    }
    return ESP_OK;
}

QueueHandle_t mb_slave_event_queue(void)
{
    return mb_event_queue;
}


// Publishes the demo input registers. A plain task, at a plain priority, deliberately
// outside the Modbus stack: it calls mb_reg_set() exactly the way application code would.
static void mb_refresh_task(void *arg)
{
    (void)arg;

    while (1) {
        uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
        // 32-bit value spread over two registers, high word first — the order almost every
        // Modbus master defaults to for a 32-bit big-endian quantity.
        mb_reg_set(MB_REG_INPUT, mb_reg_table[MB_IDX_UPTIME_HI].addr, (uint16_t)(uptime_s >> 16));
        mb_reg_set(MB_REG_INPUT, mb_reg_table[MB_IDX_UPTIME_LO].addr, (uint16_t)(uptime_s & 0xFFFF));

        // voltage_monitor_get_sys_voltage() returns volts; the register carries millivolts
        // so the master needs no scaling factor for a sane supply range. Under QEMU this
        // resolves to the mock in qemu/hardware_mocks_qemu.c, which returns a fixed 12.3 V.
        float voltage_v = voltage_monitor_get_sys_voltage();
        if (voltage_v < 0.0f) {
            voltage_v = 0.0f;
        }
        uint32_t voltage_mv = (uint32_t)(voltage_v * 1000.0f);
        if (voltage_mv > UINT16_MAX) {
            voltage_mv = UINT16_MAX;
        }
        mb_reg_set(MB_REG_INPUT, mb_reg_table[MB_IDX_SUPPLY_MV].addr, (uint16_t)voltage_mv);

        uint32_t heap_kib = esp_get_free_heap_size() / 1024;
        if (heap_kib > UINT16_MAX) {
            heap_kib = UINT16_MAX;
        }
        mb_reg_set(MB_REG_INPUT, mb_reg_table[MB_IDX_FREE_HEAP].addr, (uint16_t)heap_kib);

        vTaskDelay(pdMS_TO_TICKS(MB_REFRESH_PERIOD_MS));
    }
}

esp_err_t mb_registers_init(void)
{
    // Idempotent: mb_slave_start() calls this on every restart, and the store, the queue
    // and the refresh task all outlive a stop/start of the Modbus transports. Keeping the
    // store across a restart is the point — a register the application published does not
    // revert to 0 because the serial parameters changed.
    if (mb_reg_mutex != NULL) {
        return ESP_OK;
    }

    mb_reg_mutex = xSemaphoreCreateMutex();
    if (mb_reg_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create the register mutex");
        return ESP_ERR_NO_MEM;
    }

    mb_event_queue = xQueueCreate(MB_EVENT_QUEUE_DEPTH, sizeof(mb_write_event_t));
    if (mb_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create the write event queue");
        vSemaphoreDelete(mb_reg_mutex);
        mb_reg_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    memset(mb_reg_values, 0, sizeof(mb_reg_values));

    BaseType_t ret = xTaskCreate(mb_refresh_task, "mb_refresh_task", MB_REFRESH_TASK_STACK_SIZE,
                                 NULL, MB_REFRESH_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the register refresh task");
        vQueueDelete(mb_event_queue);
        mb_event_queue = NULL;
        vSemaphoreDelete(mb_reg_mutex);
        mb_reg_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Register framework ready: %u registers", (unsigned)ARRAY_SIZE(mb_reg_table));
    return ESP_OK;
}
