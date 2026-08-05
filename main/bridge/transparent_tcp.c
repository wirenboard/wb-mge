#include "transparent_tcp.h"

#include <string.h>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "serial.h"
#include "tcp_server.h"
#include "tcp_client.h"
#include "rs485_stats.h"
#include "bridge.h"


#define TRANSPARENT_TCP_MAX_TASK_COUNT      BRIDGES_COUNT   // Maximum number of tasks (ports)


/* "Send to whichever peer this port is currently connected to" — tcp_server and tcp_client
 * both provide it, and both resolve the target and send under the descriptor's connection
 * lock. Deliberately NOT a (desc, sock, data, len) signature: a socket passed by value is
 * a socket that can be closed and recycled between the read and the send, which is the
 * race this indirection removes. */
typedef esp_err_t (*tcp_send_func_t)(tcp_desc_t *desc, uint8_t *data, size_t len);
typedef esp_err_t (*tcp_connected_func_t)(tcp_desc_t *desc);
typedef esp_err_t (*tcp_deinit_func_t)(tcp_desc_t *desc);

typedef struct {
    bool initialized;
    unsigned index;
    serial_desc_t* serial_desc;
    tcp_desc_t* tcp_desc;
    bridge_mode_t mode;
    tcp_send_func_t tcp_send_func;
    tcp_connected_func_t tcp_connected_func;
    tcp_deinit_func_t tcp_deinit_func;
    /* Guards serial_desc against the TCP->serial producer: held by the TCP receiver task
     * across "read the descriptor and send through it", and by deinit across "clear the
     * descriptor" — see process_data_from_tcp() and transparent_tcp_deinit_port().
     *
     * One mutex PER PORT, not one per module: a receiver task holds it across
     * serial_send() -> uart_write_bytes(), which blocks until the bytes fit in the TX
     * ring (hundreds of ms for a full packet at a low baud rate), and a module-wide lock
     * would make the two ports take turns at that.
     *
     * Created on the port's first init and deliberately NEVER deleted, not even by
     * deinit: a receiver task can be waiting on this mutex at exactly the moment the port
     * is torn down, and deleting a mutex out from under a waiter is the class of bug this
     * lock exists to prevent. The context array is static, so the handle is simply reused
     * by the next init of the same port. */
    SemaphoreHandle_t serial_lock;
    /* Consecutive serial->TCP send failures on this port, used to throttle the error log.
     * Written only by this port's UART event task (single writer), so no atomics. */
    uint32_t tcp_send_failures;
} transp_tcp_task_ctx_t;


static const char *TAG = "transparent_tcp";

static transp_tcp_task_ctx_t transp_tcp_task_ctx[TRANSPARENT_TCP_MAX_TASK_COUNT] = {0};


/* Take/release the port's serial-path mutex.
 *
 * A NULL handle is treated as "no contention possible": that state cannot come out of
 * transparent_tcp_init_port() (it fails instead), it only exists for a context built by
 * hand in a unit test, which is single-threaded. Same convention as
 * tcp_desc_conn_lock_acquire().
 *
 * The wait is unbounded, unlike the producer-side wait on tcp_desc's conn_lock, and can be —
 * but the two callers pay for it very differently:
 *   - process_data_from_tcp(), the RECEIVER side, waits for deinit, which does nothing under
 *     the lock but swap two pointers. Cheap, and the waiter is a TCP receiver task, not the
 *     UART event task whose stalling would overflow the UART event queue;
 *   - transparent_tcp_deinit_port(), the DEINIT side, waits for a receiver task parked inside
 *     serial_send() -> uart_write_bytes(), which blocks until the payload fits in the TX ring.
 *     That is the expensive direction: at BAUDRATE_MIN (1200, main/setting_validators.c) a
 *     full RX_BUFFER_SIZE-1 packet is ~1023 bytes x 10 bits / 1200 baud ~= 850 ms, and a few
 *     hundred ms is the ordinary case. Deinit reaches here from port_deinit_mode(), which
 *     runs on the httpd worker holding that port's pm_lock, so the wait is charged to an
 *     HTTP request and stalls every other operation on the same port meanwhile. Accepted:
 *     a bounded wait here could only give up and tear the port down anyway, with a receiver
 *     task still inside the serial descriptor that serial_deinit() is about to free — which
 *     is the crash this lock exists to prevent. */
static void serial_path_lock(transp_tcp_task_ctx_t *ctx)
{
    if (ctx->serial_lock != NULL) {
        xSemaphoreTake(ctx->serial_lock, portMAX_DELAY);
    }
}


static void serial_path_unlock(transp_tcp_task_ctx_t *ctx)
{
    if (ctx->serial_lock != NULL) {
        xSemaphoreGive(ctx->serial_lock);
    }
}


// Find context by serial_desc_t descriptor
static transp_tcp_task_ctx_t* find_ctx_by_serial_desc(const serial_desc_t* serial_desc)
{
    for (unsigned i = 0; i < TRANSPARENT_TCP_MAX_TASK_COUNT; i++) {
        if (transp_tcp_task_ctx[i].serial_desc == serial_desc) {
            return &transp_tcp_task_ctx[i];
        }
    }
    return 0;
}


// Find context by tcp_desc_t descriptor
static transp_tcp_task_ctx_t* find_ctx_by_tcp_desc(const tcp_desc_t* tcp_desc)
{
    for (unsigned i = 0; i < TRANSPARENT_TCP_MAX_TASK_COUNT; i++) {
        if (transp_tcp_task_ctx[i].tcp_desc == tcp_desc) {
            return &transp_tcp_task_ctx[i];
        }
    }
    return 0;
}


// Rate-limited error for a serial packet that could not be pushed to TCP.
//
// Throttled for exactly the reason tcp_server.c/tcp_client.c throttle their lock-timeout
// warning, and it has to be, or that reason stops holding: this runs on the UART event
// task, ESP_LOGE writes synchronously to the console UART0 (~4 ms for a line at 115200),
// and the failure is not rare — a peer whose receive window has stalled fails EVERY packet,
// so an unthrottled line per packet costs the serial traffic it is reporting on.
//
// Throttling here only works because the LAYER BELOW throttles too: the same stalled peer
// makes send() return EAGAIN on every packet, and tcp_server.c/tcp_client.c used to print
// that errno per packet (under conn_lock, at that). They now report it through their own
// log_send_error(), on the same first-and-every-64th schedule and outside the lock, so the
// two levels no longer add up to one line per packet regardless of what this one does.
//
// The counter is not reset on success — only per init of the port. Clearing it whenever a
// packet gets through would restore the per-packet log rate under an alternating
// success/failure pattern, which is a regime this exists to survive.
static void log_tcp_send_failure(transp_tcp_task_ctx_t *ctx)
{
    ctx->tcp_send_failures++;
    if ((ctx->tcp_send_failures == 1u) || ((ctx->tcp_send_failures % 64u) == 0u)) {
        ESP_LOGE(TAG, "Port[%u]: Failed to send data to TCP from serial (%u dropped so far)",
                 ctx->index + 1, (unsigned)ctx->tcp_send_failures);
    }
}


// Callback function for receiving data from serial port
static void process_data_from_serial(serial_desc_t *desc, uint8_t *data, size_t len)
{
    ESP_LOGD(TAG, "Received data from serial, length: %zu", len);

    transp_tcp_task_ctx_t* ctx = find_ctx_by_serial_desc(desc);
    if (!ctx) {
        // Not an error: transparent_tcp_deinit_port() clears ctx->serial_desc while this
        // port's UART event task is still running (serial_deinit() joins it only
        // afterwards), so a lookup miss is the normal signal of a teardown in progress.
        // At ESP_LOGE this printed once per packet for the whole teardown window, on the
        // task least able to afford it — see log_tcp_send_failure() above.
        ESP_LOGD(TAG, "Unknown serial_desc in process_data_from_serial(), port is being deinitialized");
        return;
    }
    if (!ctx->initialized) {
        ESP_LOGW(TAG, "Context is not initialized, skipping packet");
        return;
    }

    ESP_LOGD(TAG, "Port[%u]: Received %zu bytes from serial, sending them to TCP", ctx->index + 1, len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);

    rs485_busy_monitor_update_activity(ctx->index);

    esp_err_t conn = ctx->tcp_connected_func(ctx->tcp_desc);
    if (conn != ESP_OK) {
        ESP_LOGW(TAG, "Port[%u]: No TCP connection, packet from serial will be skipped", ctx->index + 1);
        return;
    }

    // Push to the port's current peer. This callback serves BOTH bridge modes, and
    // "current peer" means something slightly different in each:
    //   - server mode: the single admitted client (max_connections == 1), registered by
    //     tcp_server.c as soon as the connection is admitted, so serial data reaches a
    //     client that has never transmitted anything itself;
    //   - client mode: our own outgoing socket, owned and maintained by tcp_client.c
    //     (which connects, reconnects and resets it itself; max_connections is not used).
    // Either way it is the one peer this port talks to, and transparent mode has no
    // strict request/response matching, so an unsolicited serial->TCP push is correct.
    //
    // This runs on the UART event task while the receiver task may be tearing the
    // connection down on the other core, so the socket is deliberately NOT read here and
    // passed on: the send function resolves the target and sends it under the descriptor's
    // connection lock. Reading desc->last_client_sock into an argument, as this used to,
    // meant the fd could be closed — and its number reused by another connection — between
    // the read and the send(), putting RS-485 bytes on a stranger's socket.
    esp_err_t err = ctx->tcp_send_func(ctx->tcp_desc, data, len);

    if (err != ESP_OK) {
        log_tcp_send_failure(ctx);
    }
}


// Callback function for receiving data from TCP socket
static void process_data_from_tcp(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
{
    ESP_LOGD(TAG, "Received data from TCP, length: %zu", len);

    transp_tcp_task_ctx_t* ctx = find_ctx_by_tcp_desc(desc);
    if (!ctx) {
        // Same as in process_data_from_serial(): deinit clears ctx->tcp_desc while the TCP
        // receiver tasks are still running (tcp_*_deinit() joins them only afterwards), so
        // a lookup miss means "this port is being torn down", not "impossible state".
        ESP_LOGD(TAG, "Unknown tcp_desc in process_data_from_tcp(), port is being deinitialized");
        return;
    }
    if (!ctx->initialized) {
        ESP_LOGW(TAG, "Context is not initialized, skipping packet");
        return;
    }

    if (client_sock < 0) {
        ESP_LOGE(TAG, "Port[%u]: no client connected", ctx->index + 1);
        return;
    }

    ESP_LOGD(TAG, "Port[%u]: Received %zu bytes from TCP, sending them to serial", ctx->index + 1, len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);

    rs485_busy_monitor_update_activity(ctx->index);

    // Resolve the serial descriptor and send through it with the port's serial lock held.
    //
    // This runs in a TCP receiver task while transparent_tcp_deinit_port() may be tearing
    // the port down on the port_manager/httpd task, and the context lookup above is NOT
    // enough on its own: a producer that has already passed it holds a context whose
    // serial_desc is about to be cleared and then freed by serial_deinit(). Reading the
    // field unsynchronised gets a NULL dereference (serial_send() dereferences desc on its
    // first line) or, if the pointer was loaded a moment earlier, a use-after-free.
    //
    // The lock makes "read the descriptor and send through it" atomic against deinit's
    // "clear the descriptor", and deinit calls serial_deinit() — the free — only after
    // releasing it. So this send either completes before the pointer is cleared or sees
    // NULL and drops the packet; it can never reach a descriptor that is being freed.
    //
    // Deadlock-free, and for the same reasons as the identical arrangement in repeater.c:
    // nothing downstream of serial_send() ever takes this mutex (the sniffer takes its own,
    // never in the other order), and the UART event task — the one task serial_deinit()
    // blocks on — does not take it at all, so deinit cannot end up waiting on its own join.
    serial_path_lock(ctx);
    serial_desc_t *serial_desc = ctx->serial_desc;
    if (!serial_desc) {
        serial_path_unlock(ctx);
        ESP_LOGD(TAG, "Port[%u]: Serial port is being deinitialized, dropping TCP packet", ctx->index + 1);
        return;
    }
    esp_err_t err = serial_send(serial_desc, data, len);
    serial_path_unlock(ctx);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Port[%u]: Failed to send data to serial from TCP", ctx->index + 1);
    }
}


esp_err_t transparent_tcp_init_port(unsigned index, serial_config_t *config,
                                    bridge_mode_t mode, int port, uint32_t ip,
                                    serial_desc_t **serial_desc, tcp_desc_t **tcp_desc)
{
    if (index >= TRANSPARENT_TCP_MAX_TASK_COUNT) {
        ESP_LOGE(TAG, "Port[%u]: Port number out of range", index + 1);
        return ESP_ERR_INVALID_ARG;
    }

    transp_tcp_task_ctx_t* ctx = &transp_tcp_task_ctx[index];
    if (ctx->initialized) {
        ESP_LOGW(TAG, "Port[%u]: Already initialized", index + 1);
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Port[%u]: Initializing in transparent TCP mode...", index + 1);

    // Before anything is allocated, so this failure needs no cleanup. Created once per port
    // and kept for the lifetime of the firmware (see the field comment); a re-init of the
    // same port reuses the handle.
    if (ctx->serial_lock == NULL) {
        ctx->serial_lock = xSemaphoreCreateMutex();
        if (ctx->serial_lock == NULL) {
            // Fail rather than run without it: without this mutex a TCP packet arriving
            // during teardown dereferences a serial descriptor that is being freed.
            ESP_LOGE(TAG, "Port[%u]: Unable to create serial-path mutex", index + 1);
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = ESP_OK;
    tcp_send_func_t tcp_send_func = 0;
    tcp_connected_func_t tcp_connected_func = 0;
    tcp_deinit_func_t tcp_deinit_func = 0;

    switch (mode) {
        case BRIDGE_MODE_SERVER:
            err = tcp_server_init(port, process_data_from_tcp, tcp_desc);
            if (err == ESP_OK) {
                // Transparent mode routes to a single peer, so admit exactly one client
                // at a time; while one is connected, a new connection is rejected rather
                // than allowed to preempt it.
                // Guard on success: on failure *tcp_desc is not set by tcp_server_init.
                tcp_server_set_max_connections(*tcp_desc, 1);
                // No close_handler: this port keeps no per-connection state of its own,
                // and invalidating the send target is tcp_server's job now — it clears the
                // field, bumps the connection generation and closes the socket in one
                // locked step, which a callback running before the close cannot do.
            }
            tcp_send_func = tcp_server_send_to_current_client;
            tcp_connected_func = tcp_server_connected;
            tcp_deinit_func = tcp_server_deinit;
            break;
        case BRIDGE_MODE_CLIENT:
            err = tcp_client_init(ip, port, process_data_from_tcp, tcp_desc);
            tcp_send_func = tcp_client_send_to_current_client;
            tcp_connected_func = tcp_client_connected;
            tcp_deinit_func = tcp_client_deinit;
            break;
        default:
            ESP_LOGE(TAG, "Port[%u]: Unknown bridge mode: %d", index + 1, mode);
            return ESP_FAIL;
    }

    if (err != ESP_OK) {
        return err;
    }

    *serial_desc = serial_init(config, process_data_from_serial);

    if (!*serial_desc) {
        ESP_LOGE(TAG, "Port[%u]: Failed to initialize serial port, cleaning up TCP", index + 1);
        tcp_deinit_func(*tcp_desc);
        *tcp_desc = NULL;  // prevent caller from holding a dangling pointer
        return ESP_FAIL;
    }

    ctx->index = index;
    ctx->serial_desc = *serial_desc;
    ctx->tcp_desc = *tcp_desc;
    ctx->mode = mode;
    ctx->tcp_send_func = tcp_send_func;
    ctx->tcp_connected_func = tcp_connected_func;
    ctx->tcp_deinit_func = tcp_deinit_func;
    ctx->tcp_send_failures = 0;     // fresh session: log the first failure of THIS session

    ctx->initialized = 1;

    ESP_LOGD(TAG, "Port[%u]: Initialized", index + 1);
    return ESP_OK;
}

esp_err_t transparent_tcp_deinit_port(unsigned index)
{
    if (index >= TRANSPARENT_TCP_MAX_TASK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Port[%u]: Deinitializing...", index + 1);

    transp_tcp_task_ctx_t* ctx = &transp_tcp_task_ctx[index];
    if (!ctx->initialized) {
        ESP_LOGW(TAG, "Port[%u]: Not initialized, skipping deinit", index + 1);
        return ESP_OK;
    }
    ctx->initialized = 0;  // clear before calling deinit to prevent re-entrancy

    // This port has producers pointing BOTH ways — the UART event task pushes into the TCP
    // descriptor (process_data_from_serial -> tcp_send_func) and the TCP receiver tasks push
    // into the serial descriptor (process_data_from_tcp -> serial_send) — and tcp_*_deinit()
    // and serial_deinit() each join only their OWN tasks. So whichever descriptor is freed
    // first, the other side's producer is still running: ordering alone cannot make this
    // safe, it only chooses which direction gets the use-after-free. The two directions are
    // therefore closed by two different mechanisms.
    //
    // TCP -> serial is closed by the port's serial lock. Clearing ctx->serial_desc under it
    // means a producer either finishes its send before the pointer goes away or observes
    // NULL and drops the packet; serial_deinit() (the free) runs only after the lock is
    // released, so it cannot overtake a send in flight. Clearing the field alone would NOT
    // do this: it only narrows the window to "producer loaded the pointer before the clear,
    // freed after", which is precisely the interleaving that crashes. serial_deinit() is
    // called outside the lock on purpose — same reasoning as repeater_deinit_port(): it
    // blocks on the UART event task, and holding a lock across a task join is a habit worth
    // not forming even where, as here, that task never takes this one.
    //
    // serial -> TCP is closed by ORDER: serial_deinit() joins the UART event task via
    // EVENT_TASK_FINISHED, so once it returns that producer physically does not exist and
    // the TCP descriptor can be freed with no one left to send through it. That matters more
    // than it used to, because tcp_send_func now waits up to TCP_DESC_SEND_LOCK_TIMEOUT_MS
    // on desc->conn_lock: with the frees the other way round, the UART task could be parked
    // INSIDE the descriptor while tcp_server_deinit() vSemaphoreDelete()d that mutex and
    // free()d the memory around it. tcp_server_deinit() waits on active_connections, which
    // counts receiver tasks only — the UART task is invisible to it. Between the clear below
    // and that join the UART task can still read ctx->tcp_desc as NULL; every consumer of it
    // (tcp_server_connected / tcp_*_send_to_current_client) rejects NULL, so the packet is
    // dropped rather than dereferenced.
    //
    // Clearing the two fields also keeps the CONTEXT LOOKUPS honest after the frees: a later
    // init of another port can get the same heap address back from calloc()/malloc(), and a
    // stale pointer left here would make find_ctx_by_tcp_desc() hand that port's traffic to
    // this dead context.
    serial_path_lock(ctx);
    serial_desc_t *serial_desc = ctx->serial_desc;
    tcp_desc_t    *tcp_desc    = ctx->tcp_desc;
    ctx->tcp_desc    = NULL;    // prevent stale pointer from matching in find_ctx_by_tcp_desc()
    ctx->serial_desc = NULL;    // prevent stale pointer from matching in find_ctx_by_serial_desc()
    serial_path_unlock(ctx);

    // Serial FIRST, TCP second — the same order as the neighbouring bridge branch
    // (modbus_tcp_deinit_port), for the serial->TCP reason spelled out above. The other
    // port_deinit_mode() branches (passive, repeater) have no TCP side at all, so there is
    // no order for them to agree with.
    serial_deinit(serial_desc);
    ctx->tcp_deinit_func(tcp_desc);

    ESP_LOGD(TAG, "Port[%u]: Deinitialized", index + 1);
    return ESP_OK;
}
