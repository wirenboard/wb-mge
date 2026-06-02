#include "repeater.h"
#include "bridge.h"            // BRIDGES_COUNT
#include "rs485_stats.h"       // rs485_busy_monitor_update_activity
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"         // esp_timer_get_time (64-bit monotonic microseconds since boot)
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "repeater";

typedef struct { serial_desc_t *serial_desc; } repeater_ctx_t;

static repeater_ctx_t s_ctx[BRIDGES_COUNT];
static uint64_t s_bytes[BRIDGES_COUNT];   // s_bytes[i] = bytes forwarded FROM port i to its peer
static uint64_t s_dropped[BRIDGES_COUNT]; // s_dropped[i] = bytes received on port i that could not be forwarded
static unsigned s_active_count;           // number of ports currently in repeater mode
static int64_t  s_active_since_us;         // esp_timer_get_time() snapshot when forwarding became active (s_active_count 0->1)

// Repeater-global mutex guarding the per-port serial_desc pointers and the shared
// counters/active accounting. It exists so a port's UART task can read its peer's
// descriptor and forward into it without the peer being torn down (freed) underneath
// it from another context (HTTP set_mode handler or settings_update_task). Created
// once by repeater_init() (called from port_manager_init), with a lazy fallback so
// single-threaded unit tests that never call an init still work.
static SemaphoreHandle_t s_lock;

static void repeater_lock(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void repeater_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

#define REPEATER_US_PER_SEC     1000000U

void repeater_init(void)
{
    // Idempotent: create the global lock once, before any port concurrency starts.
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
}

// Find the port index for a serial descriptor; returns -1 if unknown.
static int find_index_by_serial_desc(const serial_desc_t *desc)
{
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        if (s_ctx[i].serial_desc == desc) {
            return (int)i;
        }
    }
    return -1;
}

// Receive handler installed on every repeater port: forward received bytes to the peer port.
// The peer-pointer read, serial_send and the counter update run under s_lock so a concurrent
// repeater_deinit_port() (running in another context) cannot free the peer descriptor while
// this task is mid-send. serial_send() is intentionally called UNDER the lock: the only other
// lock taken downstream is the sniffer's own mutex (never the repeater lock), so there is no
// lock inversion. repeater_deinit_port() NULLs the pointer under the lock and only calls
// serial_deinit() after releasing it, so any in-flight serial_send here has already completed.
static void repeater_rx_handler(serial_desc_t *desc, uint8_t *data, size_t len)
{
    int index = find_index_by_serial_desc(desc);
    if (index < 0) {
        ESP_LOGE(TAG, "Unknown serial_desc in repeater_rx_handler()");
        return;
    }

    unsigned peer = (index == 0) ? 1 : 0;
    rs485_busy_monitor_update_activity((unsigned)index);   // RX arrived on this port

    repeater_lock();
    serial_desc_t *peer_desc = s_ctx[peer].serial_desc;
    if (peer_desc != NULL && serial_send(peer_desc, data, len) == ESP_OK) {
        s_bytes[index] += (uint64_t)len;
        rs485_busy_monitor_update_activity(peer);          // TX forwarded to peer
    } else {
        // Peer not in repeater mode (NULL) or the send failed: bytes cannot be forwarded.
        s_dropped[index] += (uint64_t)len;
    }
    repeater_unlock();
}

esp_err_t repeater_init_port(unsigned index, serial_config_t *config, serial_desc_t **serial_desc_out)
{
    if (index >= BRIDGES_COUNT || serial_desc_out == NULL) {
        ESP_LOGE(TAG, "Port[%u]: invalid arguments", index + 1);
        return ESP_ERR_INVALID_ARG;
    }

    // Double-init guard: if this port already has a live descriptor, hand it back
    // instead of opening a second serial and leaking the previous descriptor /
    // double-incrementing s_active_count (mirrors transparent_tcp's behavior).
    if (s_ctx[index].serial_desc != NULL) {
        ESP_LOGW(TAG, "Port[%u]: repeater port already initialized", index + 1);
        *serial_desc_out = s_ctx[index].serial_desc;
        return ESP_OK;
    }

    serial_desc_t *desc = serial_init(config, repeater_rx_handler);
    if (desc == NULL) {
        ESP_LOGE(TAG, "Port[%u]: serial_init failed", index + 1);
        return ESP_FAIL;
    }

    repeater_lock();
    // Fresh forwarding session: reset counters and start the uptime clock.
    if (s_active_count == 0) {
        memset(s_bytes, 0, sizeof(s_bytes));
        memset(s_dropped, 0, sizeof(s_dropped));
        s_active_since_us = esp_timer_get_time();
    }
    s_ctx[index].serial_desc = desc;
    *serial_desc_out = desc;
    s_active_count++;
    repeater_unlock();

    ESP_LOGD(TAG, "Port[%u]: repeater port initialized (active_count=%u)", index + 1, s_active_count);
    return ESP_OK;
}

esp_err_t repeater_deinit_port(unsigned index)
{
    if (index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    repeater_lock();
    serial_desc_t *desc = s_ctx[index].serial_desc;
    if (desc == NULL) {
        repeater_unlock();
        return ESP_OK;
    }
    s_ctx[index].serial_desc = NULL;          // no data path will pick it up after this
    if (s_active_count > 0) {
        s_active_count--;
    }
    repeater_unlock();

    // serial_deinit() is called OUTSIDE the lock on purpose: it blocks waiting for this
    // port's own UART task to finish, and that task may itself be waiting on s_lock inside
    // repeater_rx_handler(). Calling serial_deinit() while holding s_lock would deadlock.
    // Safe here because the pointer was already NULLed under the lock, so any in-flight
    // serial_send() on this descriptor completed before we released the lock.
    serial_deinit(desc);

    ESP_LOGD(TAG, "Port[%u]: repeater port deinitialized (active_count=%u)", index + 1, s_active_count);
    return ESP_OK;
}

void repeater_get_stats(repeater_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    // Take the lock so callers get a consistent snapshot of the counters/active/uptime.
    repeater_lock();
    out->bytes_1to2 = s_bytes[0];
    out->bytes_2to1 = s_bytes[1];
    out->dropped_1  = s_dropped[0];
    out->dropped_2  = s_dropped[1];
    out->active     = (s_active_count >= BRIDGES_COUNT);
    out->uptime_s   = (s_active_count > 0)
                      ? (uint64_t)((esp_timer_get_time() - s_active_since_us) / REPEATER_US_PER_SEC)
                      : 0;
    repeater_unlock();
}

#ifdef __unittest_env__
void repeater_reset_for_test(void)
{
    memset(s_ctx, 0, sizeof(s_ctx));
    memset(s_bytes, 0, sizeof(s_bytes));
    memset(s_dropped, 0, sizeof(s_dropped));
    s_active_count = 0;
    s_active_since_us = 0;
    s_lock = NULL;   // R1: reset so each test starts with no global lock (deterministic mutex-create count)
}
#endif
