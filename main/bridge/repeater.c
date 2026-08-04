#include "repeater.h"
#include "bridge.h"            // BRIDGES_COUNT
#include "rs485_stats.h"       // rs485_busy_monitor_update_activity
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"         // esp_timer_get_time (64-bit monotonic microseconds since boot)
#include "freertos/semphr.h"
#include "freertos/task.h"     // vTaskDelay (teardown drain wait)
#include "esp_log.h"
#include <string.h>

static const char *TAG = "repeater";

typedef struct { serial_desc_t *serial_desc; } repeater_ctx_t;

static repeater_ctx_t s_ctx[BRIDGES_COUNT] = {0};
static uint64_t s_bytes[BRIDGES_COUNT] = {0};   // s_bytes[i] = bytes forwarded FROM port i to its peer
static uint64_t s_dropped[BRIDGES_COUNT] = {0}; // s_dropped[i] = bytes received on port i that were lost: forward failures plus RX-stage drops (receive-buffer / ring overflow)
static unsigned s_active_count = 0;             // number of ports currently in repeater mode
static int64_t  s_active_since_us = 0;          // esp_timer_get_time() snapshot when forwarding became active (s_active_count 0->1)

// s_inflight[i] = forwards currently inside serial_send() writing INTO port i. Indexed by
// the DESTINATION port, not the source: it is what repeater_deinit_port(i) waits on before
// freeing port i's descriptor, and the thing it has to wait for is a sender that is writing
// into i (which is the peer port's UART task, not i's own).
static unsigned s_inflight[BRIDGES_COUNT] = {0};

// Repeater-global mutex guarding the per-port serial_desc pointers, the shared
// counters/active accounting and s_inflight. Created once by repeater_init() (called from
// port_manager_init_subsystems(), which main.c runs before the HTTP server starts), with a
// lazy fallback so single-threaded unit tests that never call an init still work.
//
// Note what it does NOT do: it is not held across serial_send(), so lock possession is not
// what keeps a peer descriptor alive while a forward is on the wire — s_inflight is. See
// repeater_rx_handler() and repeater_deinit_port().
static SemaphoreHandle_t s_lock = NULL;

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

#define REPEATER_US_PER_MS      1000U

// Poll interval of the teardown drain wait in repeater_deinit_port(). Same value and same
// polling shape as tcp_server_deinit()'s wait for active_connections (tcp_server.c).
#define REPEATER_DRAIN_POLL_MS  10U

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

// Drop handler installed on every repeater port's serial descriptor: bytes lost at the RX
// stage (receive-buffer / driver-ring overflow inside serial.c) are attributed to the port
// that received them, matching the dropped_i semantics ("received on port i, not forwarded").
// Runs in the port's UART task — the same task as repeater_rx_handler. The two are mutually
// exclusive per UART event (a single handle_uart_event invocation takes exactly one branch),
// so they are never nested. find_index_by_serial_desc() is read WITHOUT s_lock (same as
// repeater_rx_handler does); only the s_dropped[] update is taken under s_lock.
// A drop that arrives during the teardown window — after repeater_deinit_port() has NULLed
// s_ctx[index].serial_desc under the lock but before the UART task has fully exited — resolves
// to index < 0 and is intentionally NOT counted (the port is already deregistered). This is
// acceptable: such bytes are a negligible tail at shutdown.
static void repeater_drop_handler(serial_desc_t *desc, size_t dropped_len)
{
    int index = find_index_by_serial_desc(desc);
    if (index < 0) {
        return;
    }
    repeater_lock();
    s_dropped[index] += (uint64_t)dropped_len;
    repeater_unlock();
}

// Receive handler installed on every repeater port: forward received bytes to the peer port,
// unless the peer cannot actually transmit them (not in repeater mode, or its TX is disabled).
//
// serial_send() runs with s_lock RELEASED. It ends in uart_write_bytes(), which blocks until
// the bytes fit in the destination port's TX ring (SERIAL_BUF_SIZE bytes, serial.c) — at a low
// baud rate that is hundreds of milliseconds, and holding s_lock across it froze every other
// user of the lock for exactly as long. What that fixes is repeater_get_stats(), on every
// GET /info (the web UI polls it continuously). Measured on the bench with a sustained stream,
// 115200 in / 1200 out: holding the lock across the send took GET /info from a median of 31 ms
// to 969 ms (max 44 ms -> 2009 ms); with the lock released the median is back to 33 ms under
// the same load.
//
// What it does NOT fix is the other number from that bench, POST /ports/1/mode at 879 ms:
// re-measured after this change it is still ~1.1 s. A repeater port teardown waits out the same
// TX drain either way — releasing the lock only moves the wait from the lock into two explicit
// places: a forward already inside uart_write_bytes() writing INTO this port (the drain loop in
// repeater_deinit_port() below) and this port's OWN UART task leaving uart_write_bytes() on the
// peer (the EVENT_TASK_FINISHED join inside serial_deinit()). Together those are
// exactly what the old repeater_lock() at the top of repeater_deinit_port() waited for. That
// latency belongs to the bytes sitting in the TX rings, not to the lock.
//
// What keeps the peer descriptor alive across the unlocked send is s_inflight[peer], not lock
// possession. The peer-pointer read and the increment happen in ONE critical section, so
// against a concurrent repeater_deinit_port(peer) there are only two outcomes: either it has
// not NULLed the pointer yet, and we register as in-flight before it can get the lock, so its
// drain wait sees us; or it has already NULLed it, and we read NULL and never touch the
// descriptor at all. Splitting the read and the increment into two critical sections would
// create the third, broken case — a pointer read before the NULL and an increment after the
// drain wait finished — so they must stay together.
//
// What the guard does NOT buy: it does not make the send shorter, and it does not stop this
// task — the port's UART event task — from being blocked inside uart_write_bytes() for just as
// long as before. serial.c warns above uart_event_task() that a long callback overflows the
// UART event queue and merges/drops packets; that needs the forward moved off the callback,
// which this change does not do.
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

    // A peer with "Disable transmission (TX)" turned on makes serial_send() a silent no-op that
    // still returns ESP_OK (documented in port_manager.h): the bytes never reach the wire. Test
    // the flag BEFORE the send. Letting serial_send() swallow them would count them as forwarded
    // and blink the peer's TX indicator, so the UI would report "Forwarded: N, Dropped: 0" for a
    // repeater that is relaying nothing at all.
    //
    // The read is an acquire load (see the note on tx_disabled in serial.h), not mutual exclusion: the writer
    // holds the port's pm_lock, this path the repeater's disjoint s_lock, and serial_send() — reached after
    // repeater_unlock() — re-reads the flag, so the two can disagree. Three timings, all accepted: set in the
    // gap, so serial_send() swallows the frame yet it is booked forwarded (the bus does stay silent — dir_pin
    // parks LOW before the store); re-enabled in the gap, so it is booked dropped; or flipped while
    // uart_write_bytes() runs or the ring drains, parking dir_pin mid-frame, so the peer gets a truncated frame
    // still booked forwarded — inherent to cutting TX mid-transmission, not to this check. One frame either way;
    // closing the window would change serial_send()'s return contract, deliberately not done here.
    bool peer_tx_disabled = (peer_desc != NULL) && serial_tx_disabled(peer_desc);

    if ((peer_desc == NULL) || peer_tx_disabled) {
        // Peer not in repeater mode (NULL) or peer TX disabled: nothing is sent, so no
        // in-flight registration is needed and the bytes are lost here.
        s_dropped[index] += (uint64_t)len;
        repeater_unlock();
        return;
    }
    s_inflight[peer]++;
    repeater_unlock();

    esp_err_t err = serial_send(peer_desc, data, len);

    // Still inside the in-flight window on purpose. port_manager's port_deinit_mode() calls
    // rs485_busy_monitor_reset(peer) once repeater_deinit_port(peer) has returned, and the
    // drain wait below is what keeps that return behind this line — reporting the activity
    // after the decrement could re-raise the peer's "busy" indicator just after the teardown
    // cleared it, leaving it stuck on for the monitor's 5 s timeout.
    if (err == ESP_OK) {
        rs485_busy_monitor_update_activity(peer);          // TX forwarded to peer
    }

    repeater_lock();
    s_inflight[peer]--;
    if (err == ESP_OK) {
        s_bytes[index] += (uint64_t)len;
    } else {
        // The send failed: the bytes did not reach the wire.
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
    desc->drop_handler = repeater_drop_handler;  // count RX-stage drops into s_dropped[index]

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

// Read one port's in-flight forward count under the lock. A plain read would race the
// senders' ++/-- (both taken under s_lock, see repeater_rx_handler); the file uses no atomics
// anywhere else, so the drain wait re-takes the lock per poll instead of introducing one.
// Taking and releasing it per poll is what makes the wait work at all: the sender needs the
// same lock to decrement, so a drain wait that held it across the polls would never see zero.
static unsigned repeater_inflight_count(unsigned index)
{
    repeater_lock();
    unsigned count = s_inflight[index];
    repeater_unlock();
    return count;
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

    // Wait out any forward that is ALREADY inside serial_send() writing into this port before
    // handing the descriptor to serial_deinit(), which frees it. NULLing the pointer above
    // stops new forwards (the peer's handler then reads NULL and counts a drop), but a sender
    // that read the pointer an instant earlier registered itself in s_inflight[index] under the
    // same lock acquisition and is still using the descriptor. Lock possession no longer proves
    // otherwise — the send happens with the lock released. Same shape and same poll interval as
    // tcp_server_deinit()'s wait for active_connections.
    //
    // This always terminates. At most one forward can be in flight into a port: with
    // BRIDGES_COUNT == 2 the only source is the peer's UART event task, which is
    // single-threaded and is currently blocked in this very send, and it cannot start another
    // forward because the pointer it would read is already NULL. uart_write_bytes() returns
    // once its bytes fit in the destination port's TX ring, and that ring keeps draining at
    // line rate because the UART driver is still installed — serial_deinit() runs only after
    // this loop. The worst case is the ring drain time (SERIAL_BUF_SIZE = 1000 bytes, ~8 s at
    // 1200 baud), and that is not new latency: the old code waited for exactly the same event
    // right here, on a lock the sender held across the same uart_write_bytes().
    //
    // That argument leans on something this file does not enforce: the pointer has to STAY
    // NULL for the whole wait, and what guarantees it is pm_lock(index) in port_manager.c,
    // held across the entire port_deinit_mode() / port_init_mode() pair — so no
    // repeater_init_port(index) can run while this loop does. It is load-bearing precisely
    // because the wait is unbounded: a re-init landing inside the drain would republish the
    // descriptor, the peer's task could keep feeding s_inflight[index], and the wait could
    // starve (it would still free the correct old descriptor, so that is starvation, not a
    // use-after-free). The one call site that does NOT take pm_lock is the boot loop in
    // port_manager_init(); reaching it during a drain takes HTTP mode changes racing a
    // once-per-boot loop that is already unsafe against them for the whole reinit (httpd is
    // started before it, main.c), so that is a boot-window hazard of the loop rather than one
    // this wait adds. A new unlocked repeater_init_port() call site would break the argument.
    //
    // Deliberately unbounded, unlike the bounded conn_lock waits in tcp_server.c. Timing out
    // would mean calling serial_deinit() on a descriptor a sender is still writing into, i.e.
    // the use-after-free this guard exists to prevent; a bound could only choose between that
    // and the wait itself.
    while (repeater_inflight_count(index) > 0) {
        vTaskDelay(pdMS_TO_TICKS(REPEATER_DRAIN_POLL_MS));
    }

    // serial_deinit() is called OUTSIDE the lock on purpose: it blocks waiting for this
    // port's own UART task to finish, and that task may itself be waiting on s_lock inside
    // repeater_rx_handler(). Calling serial_deinit() while holding s_lock would deadlock.
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
    // uptime_ms is derived from a single microsecond snapshot.
    int64_t active_us = (s_active_count > 0) ? (esp_timer_get_time() - s_active_since_us) : 0;
    out->uptime_ms  = (uint64_t)(active_us / REPEATER_US_PER_MS);
    repeater_unlock();
}

#ifdef __unittest_env__
void repeater_reset_for_test(void)
{
    memset(s_ctx, 0, sizeof(s_ctx));
    memset(s_bytes, 0, sizeof(s_bytes));
    memset(s_dropped, 0, sizeof(s_dropped));
    memset(s_inflight, 0, sizeof(s_inflight));
    s_active_count = 0;
    s_active_since_us = 0;
    s_lock = NULL;   // R1: reset so each test starts with no global lock (deterministic mutex-create count)
}

unsigned repeater_get_inflight_for_test(unsigned index)
{
    // Read without the lock: the unit tests are single-threaded, and this is also called
    // from inside the mock serial_send(), i.e. from within the window being observed.
    return (index < BRIDGES_COUNT) ? s_inflight[index] : 0;
}

void repeater_set_inflight_for_test(unsigned index, unsigned count)
{
    // Stage the state a real sender would have left behind: "another port's UART task is
    // inside serial_send() writing into `index`". A single-threaded test cannot park a
    // sender in the mock serial_send() and run repeater_deinit_port() at the same time, so
    // the drain-wait test sets the counter directly and clears it from inside the wait loop.
    if (index < BRIDGES_COUNT) {
        s_inflight[index] = count;
    }
}
#endif
