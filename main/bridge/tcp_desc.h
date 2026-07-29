#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* Bounded wait a PRODUCER task uses when it needs conn_lock to send.
 *
 * The serial->TCP path runs on uart_event_task(), which must never block for
 * long: tcp_server_send() deliberately uses MSG_DONTWAIT because a blocked UART
 * task overflows the UART event queue and packets start to merge and drop. So a
 * producer waits for the lock only briefly and drops the packet on timeout,
 * while teardown paths (which own the connection) may wait indefinitely.
 *
 * 20 ms is a CAP ON THE DAMAGE, not a figure derived from the worst-case holder.
 * The holder can legitimately exceed it:
 *   - a producer holds the lock across the send, which is non-blocking
 *     (microseconds), and across whatever that path still logs under it. ESP_LOG*
 *     writes synchronously to the console UART0 at 115200 8N1, so a ~50-character
 *     line costs ~4 ms with the lock still held. The worst offender used to be
 *     here: a client whose receive window has stalled makes send() return EAGAIN
 *     on EVERY packet, and the errno ESP_LOGE fired per packet. That one is now
 *     emitted after the lock is released AND throttled (send_errors below,
 *     log_send_error() in tcp_server.c / tcp_client.c); what is left under the lock
 *     — "No client connected", the partial-send warning — is not a per-packet
 *     regime;
 *   - the retiring side holds it across shutdown()+close(). close() is usually one
 *     lwIP tcpip-thread turnaround, but netconn_close() retries while pbuf memory
 *     is short and gives up only after LWIP_TCP_CLOSE_TIMEOUT_MS_DEFAULT (20000 ms,
 *     lwip/opt.h; there is no Kconfig symbol for it) — three orders of magnitude
 *     above this timeout.
 * So a timeout here does NOT prove something is pathologically wrong; it means the
 * lock was busy longer than a serial packet is worth waiting for. The correct
 * response is exactly what the callers do: drop the packet and move on.
 *
 * What the number IS chosen against is the cost of waiting it out. 20 ms at
 * BAUDRATE_MAX (115200 — the highest configurable rate, main/setting_validators.c)
 * accumulates 20 ms x 11520 B/s = ~230 bytes, comfortably inside the 1000-byte
 * driver RX buffer (SERIAL_BUF_SIZE) and a handful of entries in the 20-slot UART
 * event queue (SERIAL_QUEUE_SIZE). Every lower baud rate accumulates less, so a
 * worst-case wait cannot by itself cause the overflow MSG_DONTWAIT avoids.
 */
#define TCP_DESC_SEND_LOCK_TIMEOUT_MS   20u

typedef struct tcp_desc_t tcp_desc_t;

typedef void (*tcp_receive_handler_t)(struct tcp_desc_t *desc, int client_sock, uint8_t *, size_t);

/* Optional: invoked by the receiver task when a client connection closes, so a
 * handler can release any per-connection state (e.g. reassembly buffers). */
typedef void (*tcp_close_handler_t)(struct tcp_desc_t *desc, int client_sock);

typedef struct tcp_desc_t {
    int listen_sock;                        // set to -1 in case of client
    // Socket a consumer should send unsolicited data to. In server mode the ACCEPTOR
    // registers it the moment a connection is admitted — before it publishes the
    // connection via active_connections — so a client that has not sent anything yet is
    // already reachable; the receiver task re-asserts it on every received packet. Set to
    // -1 ("no client") by tcp_server_init() and cleared back to -1 when the connection is
    // retired (receiver teardown, or rollback of a receiver task that failed to spawn),
    // in both cases only when the field still points at that socket.
    //
    // Once the descriptor is published to its tasks, EVERY read and write of this field is
    // done with conn_lock held (the sole exception is the "no client" initialisation in
    // tcp_server_init()/tcp_client_init(), which runs before any task exists), and the
    // retiring side keeps the lock across close(), so no task can ever be inside send()
    // with an fd that is being closed (and possibly recycled by lwIP for a different
    // connection). Consumers must not sample it and pass the value on: use
    // tcp_server_send_to_current_client() / tcp_client_send_to_current_client(), which do
    // the read and the send under one lock hold.
    //
    // A task that owns a socket outright uses its own local instead of reading this field
    // at all — see receive_data() in tcp_client.c, which takes the fd as a parameter.
    //
    // Well-defined ONLY on a capped single-client server (max_connections == 1), where it
    // is simply "the one admitted client". On an uncapped multi-client server
    // (max_connections == 0) the field is contended between the receivers AND the
    // acceptor, which re-registers on every admit: a newly connected silent client
    // overwrites the socket of a client that is actively sending. It therefore identifies
    // neither the last sender nor a stable peer, and no uncapped consumer reads it today
    // (modbus_tcp/cache reply via the client_sock passed to their receive handler). The
    // lock makes those writes ordered; it does not give the field a meaning it lacks.
    //
    // In client mode the field is owned by tcp_client.c and holds our outgoing socket.
    int last_client_sock;
    uint32_t remote_ip;
    int port;
    tcp_receive_handler_t receive_handler;
    tcp_close_handler_t close_handler;      // optional, may be NULL
    uint32_t max_connections;   // 0 = unlimited. When the cap is reached the acceptor rejects new clients and keeps the ones already being served (see tcp_server_task).
    // Per-server active connection count (server mode only).
    // Updated via GCC atomic builtins (__atomic_fetch_add/sub, __ATOMIC_SEQ_CST); polled
    // through plain volatile reads in deinit, so the volatile qualifier must stay.
    // WARNING: GCC/C11 atomic builtins rely on the Xtensa s32c1i instruction, which does
    // NOT work on external PSRAM — they compile fine but silently break (esp-idf #4635).
    // Therefore tcp_desc_t must live in internal RAM: never allocate it with MALLOC_CAP_SPIRAM.
    volatile uint32_t active_connections;
    TaskHandle_t task_handle;
    EventGroupHandle_t event_group;
    // Guards last_client_sock and conn_generation, and — the whole point — is held by the
    // retiring side ACROSS close(), so a producer task cannot be inside send() with an fd
    // that is being closed. Created by tcp_server_init()/tcp_client_init(), which fail if
    // it cannot be created; NULL only on a descriptor built by hand (unit-test fixtures).
    SemaphoreHandle_t conn_lock;
    // Counts RETIREMENTS of connections on this descriptor: incremented under conn_lock by
    // retire_client_conn(), the sole place a client socket is closed. Registering a
    // connection does not bump it, deliberately — an fd number can only start meaning a
    // different connection after a close, so bumping on close alone is what a captured pair
    // needs, and bumping on admit as well would only add false negatives (see
    // register_client_conn() in tcp_server.c). A consumer that captures a
    // (client_sock, generation) pair and sends later validates it with
    // tcp_server_send_to_captured_client(): equal generations mean no connection has been
    // retired since the capture, so the fd cannot have been recycled for a different peer.
    // Comparing the fd against last_client_sock instead would NOT be enough — lwIP hands
    // out the same fd number to the next connection, so the comparison passes while the
    // peer behind it is a stranger.
    //
    // "AT LEAST one bump per close" is the guarantee, not "exactly one": tcp_client.c's
    // retire_client_conn() bumps UNCONDITIONALLY, including when last_client_sock is already
    // -1 and nothing is actually closed — tcp_client_deinit() on a descriptor that never
    // connected, or the client task retiring again on its way out after deinit already
    // retired for it. Those extra bumps err in the conservative direction, since they can
    // only invalidate a pair that would otherwise have been accepted, never the reverse; the
    // counter is an upper bound on closes, which is what a safety check wants.
    //
    // This counter belongs to the DESCRIPTOR, not to an individual connection, and on an
    // uncapped server (max_connections == 0 — which is how the modbus_tcp gateway runs)
    // that distinction is visible in production. Many clients share one descriptor and one
    // counter, so ANY client DISCONNECTING invalidates the captured pairs of all the
    // others: a reply owed to a perfectly healthy master A is dropped because unrelated
    // client C happened to hang up while A's request was out on RS-485. See the note at the
    // comparison in tcp_server_send_to_captured_client().
    //
    // So the check is deliberately conservative, and on an uncapped server it does not
    // really answer "is this the same connection?" but the weaker "has no connection been
    // retired since the capture?". The price is paid in false negatives — dropped replies,
    // one Modbus retry each — and never in false positives. That direction is the point:
    // the opposite error puts RS-485 bytes on a stranger's socket.
    //
    // A plain uint32_t, not a C11 _Atomic — but every access goes through a GCC atomic
    // builtin, on BOTH sides, and that pairing is the point. Writes happen under conn_lock
    // and would be safe as a plain store as far as the mutex is concerned; the ONE unlocked
    // access is the capture-time read in tcp_desc_conn_generation() (see the rationale
    // there — losing a race there can only cost a dropped packet), and a plain store racing
    // an __atomic_load_n() is a data race by the C11 model however well it happens to
    // compile on Xtensa. So the bump is written as __atomic_add_fetch(RELAXED) in
    // retire_client_conn() (tcp_server.c and tcp_client.c): under the lock it costs exactly
    // what the plain increment cost, and it makes the store/load pair well-defined.
    // Comparisons are always under the lock, so they stay plain reads.
    // RELAXED is enough on both sides — nothing is published through this value, it only
    // decides whether a captured pair is still valid.
    // Being touched by atomic builtins adds no new s32c1i/PSRAM constraint on top of the one
    // active_connections already imposes. Wrap-around after 2^32 transitions is harmless: a
    // stale pair would have to survive exactly that many transitions to alias again.
    uint32_t conn_generation;
    // Packets dropped on this port because a producer could not take conn_lock within
    // TCP_DESC_SEND_LOCK_TIMEOUT_MS. Diagnostic only — it exists to throttle the warning
    // that reports it (log_send_lock_timeout() in tcp_server.c / tcp_client.c). Per
    // descriptor, so the "Port %d" in that message is true; bumped with
    // __atomic_add_fetch(RELAXED) because several tasks can produce into one descriptor
    // (a port's UART event task and modbus_tcp_server_task), and — like active_connections
    // — that keeps tcp_desc_t out of PSRAM (see the s32c1i warning above).
    uint32_t send_lock_timeouts;
    // Packets dropped on this descriptor because send() itself failed. Diagnostic only —
    // like send_lock_timeouts, it exists to throttle the error that reports it
    // (log_send_error() in tcp_server.c / tcp_client.c), and for a sharper reason: the
    // dominant failure is a peer whose receive window has stalled, which makes the
    // MSG_DONTWAIT send() return EAGAIN for EVERY packet. Unthrottled, that is one
    // synchronous ~4 ms console line per serial packet, on the UART event task, which costs
    // more of the traffic than the drop it is reporting.
    // Bumped with __atomic_add_fetch(RELAXED) for the same reason as send_lock_timeouts
    // (several producer tasks share one descriptor) and — again like it — never reset on a
    // successful send: clearing it would restore the per-packet log rate under an
    // alternating success/failure pattern, which is a regime this exists to survive.
    uint32_t send_errors;
} tcp_desc_t;


/* Create the connection lock. Returns false if the mutex cannot be allocated;
 * callers must fail their init in that case rather than run unprotected. */
static inline bool tcp_desc_conn_lock_init(tcp_desc_t *desc)
{
    desc->conn_lock = xSemaphoreCreateMutex();
    return desc->conn_lock != NULL;
}


/* Delete the connection lock. Call it only as the last step before free(desc); the handle
 * must not be used again afterwards.
 *
 * It deliberately does NOT clear desc->conn_lock. Clearing looks tidier but opens a window:
 * between the store and the free() a few instructions later, a producer would read
 * conn_lock == NULL, and tcp_desc_conn_lock_acquire() reads NULL as "no contention
 * possible" and returns true — so the producer would go on to send with no lock at all,
 * quietly, instead of failing. Leaving the handle dangling keeps "this descriptor has no
 * lock" a purely fixture-side state (hand-built descriptors in unit tests, which are
 * single-threaded) rather than something a live descriptor can transiently enter.
 * Correctness of the teardown itself still rests on the caller's ordering — every producer
 * must already be gone — which is what tcp_server_deinit() / tcp_client_deinit() document
 * at their call sites. */
static inline void tcp_desc_conn_lock_deinit(tcp_desc_t *desc)
{
    if (desc->conn_lock != NULL) {
        vSemaphoreDelete(desc->conn_lock);
    }
}


/* Take the connection lock, waiting at most timeout_ticks. Returns false on timeout —
 * the caller must then NOT touch last_client_sock / conn_generation and must drop
 * whatever it was about to send.
 *
 * A descriptor without a lock is treated as "no contention possible": that state cannot
 * come out of tcp_server_init()/tcp_client_init() (they fail instead), it only exists for
 * hand-built descriptors in unit tests, which are single-threaded. */
static inline bool tcp_desc_conn_lock_acquire(tcp_desc_t *desc, TickType_t timeout_ticks)
{
    if (desc->conn_lock == NULL) {
        return true;
    }
    return xSemaphoreTake(desc->conn_lock, timeout_ticks) == pdTRUE;
}


static inline void tcp_desc_conn_lock_release(tcp_desc_t *desc)
{
    if (desc->conn_lock != NULL) {
        xSemaphoreGive(desc->conn_lock);
    }
}


/* Sample the current connection generation, to be paired with a client socket and handed
 * back to tcp_server_send_to_captured_client() later.
 *
 * Deliberately lock-free: an aligned 32-bit read is atomic on the target, and the value is
 * only ever compared under the lock afterwards. A read that raced with a bump therefore
 * yields the older value, which can only make the later comparison fail (the counter never
 * goes back), i.e. it can cost a dropped packet but can never let a stale pair through.
 *
 * Lock-free, but not qualifier-free: the field is a plain uint32_t, so a plain read here
 * would let the compiler hoist or fold it. Read it through __atomic_load_n(RELAXED) — the
 * same discipline active_connections gets from its atomic builtins and volatile, at the same
 * cost (a single load; RELAXED adds no fence). */
static inline uint32_t tcp_desc_conn_generation(const tcp_desc_t *desc)
{
    return __atomic_load_n(&desc->conn_generation, __ATOMIC_RELAXED);
}
