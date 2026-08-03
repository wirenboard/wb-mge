#include "tcp_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"


#define KEEPALIVE_IDLE                  5
#define KEEPALIVE_INTERVAL              5
#define KEEPALIVE_COUNT                 3
#define RX_BUFFER_SIZE                  1024
#define TCP_SERVER_TASK_STACK_SIZE      4096u
#define TCP_SERVER_TASK_PRIORITY        5
#define TCP_SERVER_LISTEN_BACKLOG       5

#define EVENT_TASK_FINISHED             BIT1
#define EVENT_TASK_EXIT_REQ             BIT8


static const char *TAG = "tcp_server";


static inline bool check_task_exit_req(tcp_desc_t *desc)
{
    EventBits_t bits = xEventGroupWaitBits(desc->event_group, EVENT_TASK_EXIT_REQ, pdFALSE, pdTRUE, 0);
    if (bits & EVENT_TASK_EXIT_REQ) {
        return true;
    }
    return false;
}


static int create_listen_socket(int port)
{
    ESP_LOGD(TAG, "Creating listen socket on port %d", port);

    /* AF_INET6 with in6addr_any and the DEFAULT IPV6_V6ONLY (0) is a DUAL-STACK listener:
     * netconn_bind() rewrites a bind to IP6_ADDR_ANY into IP_ANY_TYPE when the netconn is
     * not v6-only (api_lib.c:324-331), and tcp_input() matches an IPADDR_TYPE_ANY listen
     * pcb against IPv4 SYNs as well (tcp_in.c:337-345). IPv4 clients therefore still reach
     * this port exactly as before.
     *
     * This is not about serving IPv6. It is about being the SAME representation as
     * esp_http_server, which binds PF_INET6/in6addr_any (httpd_main.c:350-393), so that
     * lwIP can see the two as a collision. While this socket was AF_INET/INADDR_ANY it
     * could not: both sides set SO_REUSEADDR, which makes tcp_bind() skip the
     * address-in-use check outright (tcp.c:721-731), and tcp_listen()'s duplicate check
     * compares with ip_addr_eq(), which returns 0 whenever IP_GET_TYPE differs
     * (ip_addr.h:219) — IPADDR_TYPE_ANY vs IPADDR_TYPE_V4. Both listens then succeeded and
     * the port ended up with TWO listeners answering alternate connections: on the bench,
     * port 80 shared between httpd and a bridge served 15 HTTP and 9 Modbus replies out of
     * 24. With one representation, tcp_listen() finds an equal address and returns ERR_USE
     * (EADDRINUSE), the retry loop below gives up, and the second server reports a failed
     * init instead of silently corrupting the first one.
     *
     * Two things must NOT be "cleaned up" here:
     *   - SO_REUSEADDR stays. Without it tcp_bind() starts considering TIME_WAIT pcbs, and
     *     with CONFIG_LWIP_TCP_MSL=60000 that blocks re-binding a port for 2*MSL = 120 s
     *     after any device-initiated close — i.e. every settings change that moves a port.
     *   - IPV6_V6ONLY stays at its default 0. Setting it would make this a V6-only
     *     listener: unreachable from every IPv4 client, and invisible to httpd's pcb again.
     *
     * IPPROTO_IP is kept (it is 0, "the default protocol for this family and type") rather
     * than IPPROTO_IPV6: the third argument of socket() is a protocol number, and 41 is an
     * option LEVEL, not a protocol a SOCK_STREAM socket can carry. lwIP ignores it for TCP,
     * so nothing here would notice the difference — which is exactly why the value is pinned
     * by test_listen_socket_requests_dual_stack_family instead of by a runtime failure.
     * httpd passes 0 here too (httpd_main.c:353). */
    int addr_family = (int)AF_INET6;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_storage dest_addr;

    /* Zero the WHOLE address before filling it — this is load-bearing, not hygiene.
     * dest_addr is an uninitialised stack object, and sockaddr_in6 has two fields the IPv4
     * form did not: sin6_flowinfo and sin6_scope_id. lwIP reads sin6_scope_id on the bind
     * path (SOCKADDR6_TO_IP6ADDR_PORT, sockets.c:152-158) and zones the ip_addr_t with it;
     * a zoned address then fails the ip_addr_eq(addr, IP6_ADDR_ANY) test in netconn_bind()
     * that selects dual stack, and the outcome is a silently V6-only listener that no IPv4
     * client can reach and that collides with nothing.
     *
     * That macro applies the scope id only to an address that HAS a scope
     * (ip6_addr_has_scope(), ip6_zone.h:179-182), and the unspecified address :: has none,
     * so a garbage tail does not bite this particular address today. It is zeroed anyway
     * and written down here because nothing in the code says "::", only in6addr_any: the
     * day this binds a link-local or multicast address, the garbage becomes a zone index.
     * esp_http_server gets the same guarantee for free from a designated initialiser. */
    memset(&dest_addr, 0, sizeof(dest_addr));
    struct sockaddr_in6 *dest_addr_ip6 = (struct sockaddr_in6 *)&dest_addr;
    dest_addr_ip6->sin6_family = AF_INET6;
    dest_addr_ip6->sin6_addr = in6addr_any;
    dest_addr_ip6->sin6_port = htons(port);

    /* Retry bind/listen up to N times with backoff. Under rapid mode toggles
     * (test_uart_teardown_no_crash) the previous deinit's listen socket may
     * still be in lwIP's pcb table when this init runs — bind() / listen()
     * returns errno EADDRINUSE/ECONNABORTED transiently. SO_REUSEADDR alone is
     * not sufficient: it allows reusing TIME_WAIT addresses but not addresses
     * still actively bound to a not-yet-released netconn. */
    const int max_attempts = 10;
    int listen_sock = -1;
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (listen_sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            return listen_sock;
        }

        int opt = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
            int e = errno;
            close(listen_sock);
            if (attempt + 1 < max_attempts) {
                ESP_LOGW(TAG, "bind(port=%d) errno %d, retry %d/%d in 100ms",
                         port, e, attempt + 1, max_attempts);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            ESP_LOGE(TAG, "Socket unable to bind: errno %d (gave up after %d attempts)",
                     e, max_attempts);
            return -1;
        }

        if (listen(listen_sock, TCP_SERVER_LISTEN_BACKLOG) != 0) {
            int e = errno;
            close(listen_sock);
            if (attempt + 1 < max_attempts) {
                ESP_LOGW(TAG, "listen(port=%d) errno %d, retry %d/%d in 100ms",
                         port, e, attempt + 1, max_attempts);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            ESP_LOGE(TAG, "Error occurred during listen: errno %d (gave up after %d attempts)",
                     e, max_attempts);
            return -1;
        }

        ESP_LOGD(TAG, "Socket listening on port %d", port);

        /* Put a short timeout on the listen socket so accept() returns periodically
         * with EAGAIN/EWOULDBLOCK. This lets tcp_server_task() re-check the deinit
         * exit flag without depending on close(listen_sock) to unblock a forever-
         * blocked accept() — under QEMU slirp that wake-up is delayed/unreliable and
         * makes tcp_server_deinit() hang, blocking the single httpd worker. Mirrors
         * the SO_RCVTIMEO already used on accepted client sockets. */
        struct timeval acc_timeout = { .tv_sec = 0, .tv_usec = 200000 };  /* 200 ms */
        if (setsockopt(listen_sock, SOL_SOCKET, SO_RCVTIMEO, &acc_timeout, sizeof(acc_timeout)) != 0) {
            ESP_LOGW(TAG, "Failed to set SO_RCVTIMEO on listen socket: errno %d", errno);
        }

        return listen_sock;
    }
    return -1;
}


// The peer address comes back as sockaddr_storage, not sockaddr_in: the listen socket is
// dual-stack (see create_listen_socket()), so accept() may fill in either a sockaddr_in or
// a sockaddr_in6 and only ss_family says which.
static int accept_connection(int listen_sock, struct sockaddr_storage* source_addr)
{
    static int keep_alive = 1;
    static int keep_idle = KEEPALIVE_IDLE;
    static int keep_interval = KEEPALIVE_INTERVAL;
    static int keep_count = KEEPALIVE_COUNT;
    static int no_delay_flag = 1;

    // The full sockaddr_storage. lwip_accept() clamps *addrlen DOWN to the length of the
    // form it actually wrote (sockets.c:744-747) but truncates the copy if we pass less,
    // so a sizeof(struct sockaddr_in) here would cut an IPv6 peer short.
    socklen_t addr_len = sizeof(*source_addr);
    int client_sock = accept(listen_sock, (struct sockaddr *)source_addr, &addr_len);
    if (client_sock < 0) {
        return client_sock;
    }

    setsockopt(client_sock, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive));
    setsockopt(client_sock, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle));
    setsockopt(client_sock, IPPROTO_TCP, TCP_KEEPINTVL, &keep_interval, sizeof(keep_interval));
    setsockopt(client_sock, IPPROTO_TCP, TCP_KEEPCNT, &keep_count, sizeof(keep_count));

    // No delay for send() function (disable Nagle's algorithm)
    // It is necessary that data packets are not combined when sent and to increase the performance
    setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &no_delay_flag, sizeof(no_delay_flag));

    // Add recv timeout so receiver_task can periodically check exit request.
    // Without this, recv() blocks indefinitely when the peer holds the connection
    // open and tcp_server_deinit() cannot complete (active_connections stays > 0).
    struct timeval rcv_timeout = { .tv_sec = 0, .tv_usec = 100000 };  // 100 ms
    if (setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout, sizeof(rcv_timeout)) != 0) {
        ESP_LOGE(TAG, "Failed to set SO_RCVTIMEO on client socket: errno %d", errno);
    }

    return client_sock;
}


// Arguments passed to each receiver_task (heap-allocated, freed by receiver_task)
typedef struct {
    tcp_desc_t *desc;
    int client_sock;
} receiver_task_args_t;


// Point the descriptor's send target at a client connection, under conn_lock so a producer
// reading last_client_sock never observes a half-updated registration.
//
// Serves both roles: registering a NEWLY ADMITTED connection (the acceptor) and re-asserting
// an ALREADY REGISTERED one (the receiver task, on entry and on every received packet).
// They are the same operation — publish this fd as the current target — and the two used to
// be separate functions only because one of them also bumped conn_generation.
//
// It deliberately does NOT touch conn_generation, in either role. The generation exists so a
// consumer holding a captured (fd, generation) pair can tell "the same connection" from
// "the same fd number", and only a CLOSE can make an fd number mean a different connection:
// lwIP hands a number out again only once the socket holding it has been closed, and every
// client-socket close in this module goes through retire_client_conn(), which bumps. A bump
// here would therefore invalidate nothing that the retire does not already invalidate — it
// would only add false negatives, and on the uncapped gateway (max_connections == 0, many
// masters sharing one descriptor) those are paid for in real traffic: any stranger merely
// connecting would drop the in-flight reply of every other master on the port.
static void register_client_conn(tcp_desc_t *desc, int client_sock)
{
    tcp_desc_conn_lock_acquire(desc, portMAX_DELAY);
    desc->last_client_sock = client_sock;
    tcp_desc_conn_lock_release(desc);
}


// Retire one client connection: stop routing to it, invalidate any (fd, generation) pair
// captured for it, and close the socket — all with conn_lock held.
//
// This is the SOLE source of generation bumps, and the sole place a client socket is closed.
// Those two facts are one invariant: an fd number can only start meaning a different
// connection after a close, so a bump on every close is exactly what a captured pair needs
// to be safe. Any future close() of a client socket added outside this function silently
// breaks that — route it through here instead.
//
// close() MUST stay inside the locked region. That is the whole invariant: lwIP hands the
// freed fd number straight to the next socket (another bridge port, a tcp_client reconnect,
// httpd), so a producer that got as far as send() with this fd must be kept out until the
// close is done and the field is cleared. Clearing the field alone does not do that — the
// producer may already hold the value.
//
// The caller must not hold conn_lock, and must decrement active_connections only AFTER
// this returns: tcp_server_deinit() frees desc as soon as the count reaches zero, and this
// function still touches desc (and its mutex) up to the unlock.
static void retire_client_conn(tcp_desc_t *desc, int client_sock)
{
    tcp_desc_conn_lock_acquire(desc, portMAX_DELAY);
    // Only clear when the retiring socket is still the one we would reply to: on an
    // uncapped server another client may already have taken the slot.
    if (desc->last_client_sock == client_sock) {
        desc->last_client_sock = -1;
    }
    // Under the lock, so a plain ++ would be safe against every other WRITER. The atomic is
    // for the one unlocked READER, tcp_desc_conn_generation(): a plain store paired with its
    // __atomic_load_n() is a data race by the C11 model. RELAXED costs nothing here (no
    // fence, and the mutex already provides the ordering) — see the field comment in
    // tcp_desc.h.
    __atomic_add_fetch(&desc->conn_generation, 1, __ATOMIC_RELAXED);
    shutdown(client_sock, SHUT_RDWR);
    close(client_sock);
    tcp_desc_conn_lock_release(desc);
}


// Rate-limited warning for a packet dropped because conn_lock could not be taken in time.
// The caller is the UART event task, where an unthrottled log storm would itself cost
// serial packets — and the condition is pathological (a teardown outlasting the bounded
// wait), so the first occurrence plus a periodic reminder is all that is useful.
//
// The counter lives in the DESCRIPTOR, i.e. per port, which is what the "Port %d" in the
// message claims it is. A file-scope static, as this used to be, is shared by every port and
// incremented from several tasks (each port's UART event task, plus modbus_tcp_server_task
// through the captured-client path) with no synchronisation: the printed total was a racy
// sum over unrelated ports. Relaxed ordering is enough — nothing is published through this
// value, it only decides whether to print.
static void log_send_lock_timeout(tcp_desc_t *desc)
{
    uint32_t dropped = __atomic_add_fetch(&desc->send_lock_timeouts, 1, __ATOMIC_RELAXED);

    if ((dropped == 1u) || ((dropped % 64u) == 0u)) {
        ESP_LOGW(TAG, "Port %d: connection lock busy for %ums, packet dropped (%u dropped so far)",
                 desc->port, (unsigned)TCP_DESC_SEND_LOCK_TIMEOUT_MS, (unsigned)dropped);
    }
}


// Rate-limited error for a packet that send() refused. Throttled on the same terms as
// log_send_lock_timeout() above, and for a stronger reason: this failure is not
// pathological but ROUTINE in one very common regime — a peer whose receive window has
// stalled makes the MSG_DONTWAIT send() return EAGAIN for every packet, forever. The
// caller is usually the UART event task, where each ~4 ms synchronous console line costs
// more serial traffic than the drop it announces, so an unthrottled line per packet turns
// a reporting mechanism into the fault.
//
// MUST be called with conn_lock RELEASED. The two locked senders below therefore capture
// errno under the lock, release, and only then call this: the print is what makes the
// critical section long, and lengthening it also lengthens the portMAX_DELAY wait of
// retire_client_conn() on the other side. The counter itself is per descriptor (the
// "Port %d" is then true) and bumped atomically because several producers share one.
static void log_send_error(tcp_desc_t *desc, int send_errno)
{
    uint32_t failures = __atomic_add_fetch(&desc->send_errors, 1, __ATOMIC_RELAXED);

    if ((failures == 1u) || ((failures % 64u) == 0u)) {
        ESP_LOGE(TAG, "Port %d: error occurred during sending: errno %d (%u failed so far)",
                 desc->port, send_errno, (unsigned)failures);
    }
}


// Runs the recv/dispatch/close lifecycle for one accepted connection.
// Shared by receiver_task (production) and the unit-test entry point.
static void run_receiver(tcp_desc_t *desc, int client_sock)
{
    char rx_buffer[RX_BUFFER_SIZE];
    int len;

    // Register the client as soon as it is admitted, not on its first packet.
    // Consumers that route unsolicited traffic to a connected client (transparent_tcp
    // sending serial->TCP) read last_client_sock gated only by tcp_server_connected(),
    // which passes as soon as active_connections != 0. Registering only on receive left
    // last_client_sock at -1 for a client that had connected but not yet sent anything,
    // so serial data was dropped with "No client connected" until the client spoke first.
    //
    // The acceptor already registered this socket before it incremented
    // active_connections (see tcp_server_task) — that is what closes the window for the
    // production path. This assignment is the same value again: it keeps run_receiver()
    // self-contained for tcp_server_run_receiver_for_test(), which has no acceptor.
    //
    // Registering here is only correct because run_receiver() runs exclusively for
    // ADMITTED connections — a client rejected by the max_connections cap never reaches
    // it. In the acceptor the equivalent assignment must therefore stay AFTER the cap
    // check; placing it before that check would let a rejected newcomer clobber the fd of
    // the client currently being served.
    //
    // Clearing the field on disconnect is NOT the close_handler's job any more:
    // retire_client_conn() below does it under conn_lock, together with the generation
    // bump and close(), which is the only ordering that keeps a producer out of send()
    // while the fd is being recycled.
    register_client_conn(desc, client_sock);

    do {
        len = recv(client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);

        if (len < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                // Recv timed out (SO_RCVTIMEO): check if deinit requested exit
                if (check_task_exit_req(desc)) {
                    ESP_LOGD(TAG, "Port %d: exit requested, closing receiver", desc->port);
                    break;
                }
                len = 1;   // Keep the do-while alive; retry recv()
                continue;
            }
            esp_log_level_t log_level = ESP_LOG_ERROR;
            if (check_task_exit_req(desc)) {
                log_level = ESP_LOG_DEBUG;
            }
            ESP_LOG_LEVEL(log_level, TAG, "Error occurred on port %d during receiving: errno %d", desc->port, errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection on port %d closed", desc->port);
        } else {
            ESP_LOGD(TAG, "Port %d received %d bytes", desc->port, len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buffer, len, ESP_LOG_DEBUG);
            // Re-assert last_client_sock before invoking the callback. On a capped
            // server this is a no-op: the acceptor and the entry registration above
            // already point at this socket.
            //
            // It does NOT give an uncapped server (max_connections == 0) a "last sender
            // wins" field: the acceptor re-registers on every admit, so a client that
            // merely connects overwrites the socket of one that is actively sending.
            // Kept as defensive bookkeeping only — it kicks the field back to a socket
            // known to be alive and talking. No uncapped consumer reads it today; one
            // that needed a reliable peer identity would have to be given real
            // synchronisation, not this line.
            //
            // The lock is released before the callback runs: receive handlers send from
            // their own context via tcp_server_send(), and holding conn_lock across a
            // handler would put a producer's bounded wait behind arbitrary handler work.
            register_client_conn(desc, client_sock);
            desc->receive_handler(desc, client_sock, (uint8_t *)rx_buffer, len);
            // Check exit request after each received packet so deinit() can complete
            // even when data flows continuously and recv() never times out with EAGAIN.
            if (check_task_exit_req(desc)) {
                ESP_LOGD(TAG, "Port %d: exit requested after data receive, closing receiver", desc->port);
                break;
            }
        }
    } while (len > 0);

    /* Notify handler that this connection is closing so it can free any
     * per-connection state (e.g. Modbus frame reassembly buffer).
     * Called BEFORE retire_client_conn() and therefore without conn_lock held: a
     * close_handler is free to take locks of its own (modbus_tcp takes the reassembly
     * mutex), and nesting those under conn_lock would create a lock order to maintain
     * for no benefit — the fd is still valid here, and retiring it is what protects it. */
    if (desc->close_handler) {
        desc->close_handler(desc, client_sock);
    }

    // Log before retiring so desc->port is accessed while desc is still valid.
    // deinit() waits for active_connections to reach 0 before freeing desc.
    ESP_LOGD(TAG, "Port %d receiver task finished", desc->port);

    // Clear the send target, invalidate captured (fd, generation) pairs and close the
    // socket, all under conn_lock — see retire_client_conn().
    retire_client_conn(desc, client_sock);

    // Decrement LAST. The previous order (decrement, then close) let the acceptor admit
    // the next client a few microseconds earlier, but desc is freed by deinit() the moment
    // this count reaches zero, and retire_client_conn() dereferences desc — including its
    // mutex — right up to the unlock. Publishing "done" before the descriptor is actually
    // done with is a use-after-free waiting to happen.
    __atomic_fetch_sub(&desc->active_connections, 1, __ATOMIC_SEQ_CST);
}


// Per-client receiver task: reads data from one client socket and invokes receive_handler.
// Terminates when the client disconnects or an error occurs.
static void receiver_task(void *pvParameters)
{
    receiver_task_args_t *args = (receiver_task_args_t *)pvParameters;
    tcp_desc_t *desc = args->desc;
    int client_sock = args->client_sock;
    free(args);

    run_receiver(desc, client_sock);

    vTaskDelete(NULL);
}


// Acceptor task: only accepts new connections and spawns a receiver_task for each.
static void tcp_server_task(void *pvParameters)
{
    tcp_desc_t *desc = (tcp_desc_t *)pvParameters;
    ESP_LOGD(TAG, "TCP server acceptor task started");

    while (1) {
        if (check_task_exit_req(desc)) {
            break;
        }

        struct sockaddr_storage source_addr;
        int client_sock = accept_connection(desc->listen_sock, &source_addr);
        if (client_sock < 0) {
            if (check_task_exit_req(desc)) {
                ESP_LOGD(TAG, "Socket on port %d returned error %d during connection accept", desc->port, errno);
                break;
            }

            /* accept() hit its SO_RCVTIMEO with no pending connection: loop back so the
             * exit flag at the top of the loop is re-checked. The listen socket is fine. */
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                continue;
            }

            /* Resource exhaustion (socket table full, out of memory, etc.):
             * the listen socket itself is still valid — do NOT close it.
             * Just wait briefly; resources will free up when a client disconnects. */
            if ((errno == ENFILE) || (errno == EMFILE) || (errno == ENOBUFS) || (errno == ENOMEM)) {
                ESP_LOGW(TAG, "Port %d: accept() resource exhaustion (errno=%d), retrying in 100 ms",
                         desc->port, errno);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            /* For other errors the listen socket may itself be broken — close and recreate. */
            ESP_LOGE(TAG, "Unable to accept connection on port %d, errno: %d", desc->port, errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            close(desc->listen_sock);
            desc->listen_sock = create_listen_socket(desc->port);
            if (desc->listen_sock < 0) {
                ESP_LOGE(TAG, "Failed to re-create listen socket");
                break;
            }
            continue;
        }

        // Print client IP address and port. Switched on ss_family because the listen socket
        // is dual-stack; the buffer is sized for the longer (IPv6) form.
        char addr_str[INET6_ADDRSTRLEN];
        uint16_t client_port = 0;
        addr_str[0] = 0;
        if (source_addr.ss_family == AF_INET) {
            // Still the branch every ordinary client takes. lwIP does NOT map an IPv4 peer
            // into the v4-mapped ::ffff:a.b.c.d form the way Linux does on a dual-stack
            // socket: lwip_accept() builds the sockaddr from the accepted pcb's remote_ip,
            // which for an IPv4 SYN carries IPADDR_TYPE_V4, and IPADDR_PORT_TO_SOCKADDR
            // then writes a genuine sockaddr_in with sin_family == AF_INET
            // (sockets.c:170-176, 743). So this log keeps printing 10.0.0.5, not
            // ::ffff:10.0.0.5. Do not "simplify" it into an IPv6-only branch.
            struct sockaddr_in *peer4 = (struct sockaddr_in *)&source_addr;
            inet_ntoa_r(peer4->sin_addr, addr_str, sizeof(addr_str) - 1);
            client_port = ntohs(peer4->sin_port);
        } else if (source_addr.ss_family == AF_INET6) {
            struct sockaddr_in6 *peer6 = (struct sockaddr_in6 *)&source_addr;
            inet_ntop(AF_INET6, &peer6->sin6_addr, addr_str, sizeof(addr_str));
            client_port = ntohs(peer6->sin6_port);
        }
        ESP_LOGI(TAG, "Socket on port %d accepted connection from %s, port: %d", desc->port, addr_str, client_port);

        // Enforce the per-server connection cap by rejecting the newcomer and keeping
        // the client already being served. The transparent bridge uses
        // max_connections == 1, so this keeps one master on the serial line instead of
        // letting a second one in. modbus/cache use max_connections == 0 (no cap), so
        // this branch never fires for them.
        if (desc->max_connections != 0 && desc->active_connections >= desc->max_connections) {
            ESP_LOGI(TAG, "Port %d: connection limit reached, rejecting new client", desc->port);
            // Retire rather than plain close: this fd was never registered, so nothing can
            // be sending on it, but routing every client-socket close through one function
            // keeps "a client fd is only ever closed under conn_lock" an invariant that can
            // be checked by reading this file rather than by reasoning about each site.
            retire_client_conn(desc, client_sock);
            continue;
        }

        // Allocate args for receiver_task on the heap; freed by receiver_task itself
        receiver_task_args_t *args = malloc(sizeof(receiver_task_args_t));
        if (!args) {
            ESP_LOGE(TAG, "Port %d: failed to allocate receiver_task args, closing connection", desc->port);
            retire_client_conn(desc, client_sock);
            continue;
        }
        args->desc = desc;
        args->client_sock = client_sock;

        // Register the socket BEFORE publishing the connection via active_connections.
        // tcp_server_connected() reports "connected" the moment the counter goes up, and
        // consumers then read last_client_sock; registering only in the receiver task
        // would leave a window (until that task is scheduled) where the server looks
        // connected while the field is still -1, and a serial packet arriving right then
        // would be dropped as "No client connected" — the same bug, just a few ms wide.
        //
        // Safe to do here: both admission gates (the max_connections cap and the args
        // malloc) have already passed, so a rejected client can never reach this line and
        // clobber the fd of the client currently being served.
        //
        // conn_generation is deliberately NOT moved on here — see register_client_conn().
        // An admit cannot make an earlier capture unsafe: the fd this client got was free
        // only because the socket that held it was closed, and that close bumped.
        register_client_conn(desc, client_sock);

        __atomic_fetch_add(&desc->active_connections, 1, __ATOMIC_SEQ_CST);

        // Create a unique task name using the socket fd number
        char task_name[32];
        snprintf(task_name, sizeof(task_name), "tcp_recv_%d", client_sock);

        BaseType_t ret = xTaskCreate(receiver_task, task_name, TCP_SERVER_TASK_STACK_SIZE, args, TCP_SERVER_TASK_PRIORITY, NULL);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Port %d: failed to create receiver_task, closing connection", desc->port);
            free(args);
            // Roll the registration back BEFORE dropping the count, mirroring
            // run_receiver()'s teardown order: while active_connections is still non-zero
            // tcp_server_connected() reports ESP_OK, so the send target must never be left
            // pointing at a closed (and possibly already recycled) fd. retire_client_conn()
            // clears the field only if it still points at THIS socket, invalidates the
            // generation and closes the socket, all under conn_lock — and, as in
            // run_receiver(), the decrement comes after that, because deinit() may free
            // desc the moment the count reaches zero.
            retire_client_conn(desc, client_sock);
            __atomic_fetch_sub(&desc->active_connections, 1, __ATOMIC_SEQ_CST);
        }
    }

    close(desc->listen_sock);
    desc->listen_sock = -1;
    ESP_LOGI(TAG, "TCP server acceptor task finished");
    xEventGroupSetBits(desc->event_group, EVENT_TASK_FINISHED);
    vTaskDelete(NULL);
}


esp_err_t tcp_server_init(int port, tcp_receive_handler_t tcps_receive_handler, tcp_desc_t **out_desc)
{
    if (tcps_receive_handler == NULL) {
        ESP_LOGE(TAG, "tcps_receive_handler is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (out_desc == NULL) {
        ESP_LOGE(TAG, "out_desc is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    int listen_sock = create_listen_socket(port);
    if (listen_sock < 0) {
        return ESP_FAIL;
    }

    tcp_desc_t *desc = calloc(1, sizeof(tcp_desc_t));
    if (!desc) {
        close(listen_sock);
        return ESP_ERR_NO_MEM;
    }

    EventGroupHandle_t event_group = xEventGroupCreate();
    if (event_group == NULL) {
        ESP_LOGE(TAG, "Unable to create event group");
        free(desc);
        close(listen_sock);
        return ESP_ERR_NO_MEM;
    }

    // Fail rather than run without it: the lock is what keeps a producer out of send()
    // while a socket is being closed, so a descriptor without one is not safe to serve.
    if (!tcp_desc_conn_lock_init(desc)) {
        ESP_LOGE(TAG, "Unable to create connection mutex");
        vEventGroupDelete(event_group);
        free(desc);
        close(listen_sock);
        return ESP_ERR_NO_MEM;
    }

    desc->listen_sock = listen_sock;
    desc->last_client_sock = -1;
    desc->conn_generation = 0;
    desc->remote_ip = 0;
    desc->port = port;
    desc->receive_handler = tcps_receive_handler;
    desc->active_connections = 0;
    desc->task_handle = NULL;
    desc->event_group = event_group;

    TaskHandle_t task_handle = NULL;
    BaseType_t ret = xTaskCreate(tcp_server_task, "tcp_server", TCP_SERVER_TASK_STACK_SIZE, desc, TCP_SERVER_TASK_PRIORITY, &task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Unable to create TCP acceptor task");
        tcp_desc_conn_lock_deinit(desc);
        vEventGroupDelete(event_group);
        free(desc);
        close(listen_sock);
        return ESP_ERR_NO_MEM;
    }

    desc->task_handle = task_handle;

    *out_desc = desc;
    return ESP_OK;
}


void tcp_server_set_max_connections(tcp_desc_t *desc, uint32_t max_connections)
{
    if (desc) {
        desc->max_connections = max_connections;
    }
}


// The send itself, with the errno of a failure handed back instead of logged.
//
// Splitting the report off is what lets the two locked senders below keep the ESP_LOGE out
// of the critical section: they capture *out_errno under conn_lock and call
// log_send_error() after releasing it. *out_errno is written ONLY when send() fails, so a
// caller initialises it to 0 and treats non-zero as "there is something to report".
static esp_err_t send_on_socket(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len,
                                int *out_errno)
{
    if (!desc || (client_sock < 0)) {
        ESP_LOGE(TAG, "No client connected");
        return ESP_FAIL;
    }

    // Using non-blocking function to avoid blocking uart_event_task()
    // Otherwise UART event queue overflows and packets start to merge and drop
    // TCP has its own transmit buffer and window under the hood, should not be a problem
    int res = send(client_sock, data, len, MSG_DONTWAIT);

    if (res < 0) {
        *out_errno = errno;
        return ESP_FAIL;
    }

    if (res != len) {
        ESP_LOGW(TAG, "Not all data sent, required: %u, sent: %d", len, res);
    }

    return ESP_OK;
}


// Send on a socket the CALLER owns.
//
// Safe only for a caller that cannot have its fd closed underneath it: the per-connection
// receiver task (and anything it calls synchronously) is such a caller — it is the task
// that closes the socket, and it does so only after the receive handler has returned.
// Anyone else must go through tcp_server_send_to_current_client() or
// tcp_server_send_to_captured_client(), which validate and send under conn_lock.
//
// This caller holds no lock, so the failure is reported inline; it is still throttled,
// because a stalled receive window fails every packet here too.
esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
{
    int send_errno = 0;
    esp_err_t err = send_on_socket(desc, client_sock, data, len, &send_errno);

    if (send_errno != 0) {
        log_send_error(desc, send_errno);
    }

    return err;
}


// Send unsolicited data to whichever client is registered right now.
//
// This is the serial->TCP path of a transparent bridge in server mode, and it runs on the
// UART event task. Reading last_client_sock and sending are one atomic step against
// retire_client_conn(), so the fd cannot be closed (nor recycled for another connection)
// between the two — which is exactly what happened when the caller sampled the field and
// passed the value in.
//
// The wait for the lock is bounded (TCP_DESC_SEND_LOCK_TIMEOUT_MS) so a stuck teardown can
// only cost this packet, never stall the UART task; on timeout the packet is dropped, with
// a rate-limited warning.
esp_err_t tcp_server_send_to_current_client(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    if (!desc) {
        ESP_LOGE(TAG, "No client connected");
        return ESP_FAIL;
    }

    if (!tcp_desc_conn_lock_acquire(desc, pdMS_TO_TICKS(TCP_DESC_SEND_LOCK_TIMEOUT_MS))) {
        log_send_lock_timeout(desc);
        return ESP_FAIL;
    }

    int send_errno = 0;
    esp_err_t err = send_on_socket(desc, desc->last_client_sock, data, len, &send_errno);

    tcp_desc_conn_lock_release(desc);

    // Deliberately after the release: an EAGAIN storm would otherwise hold conn_lock across
    // a synchronous console write on every packet — see log_send_error().
    if (send_errno != 0) {
        log_send_error(desc, send_errno);
    }

    return err;
}


// Send to a connection captured earlier, identified by the (client_sock, generation) pair
// the caller sampled with tcp_desc_conn_generation() at capture time.
//
// This is the Modbus gateway's reply path. The pair is sampled where the request ENTERS
// the system — in modbus_tcp's receive handler, which runs in the receiver task of the
// connection the bytes arrived on — and then travels with the request through the packet
// queue. It is emphatically NOT re-sampled when the request is popped: by then the request
// may have sat in the queue while its client disconnected, and sampling at pop time would
// validate the reply against whichever connection exists at that moment. See
// fetch_tcp_request() in modbus_tcp.c, which adopts both halves straight off the queue.
//
// From capture to send is a long window — queue wait plus tens of milliseconds of RTU
// exchange on RS-485, with the reply finally sent from the UART event task. In that window
// the client can disconnect and lwIP can hand its fd number to a completely unrelated
// socket, so the fd alone identifies nothing — the generation is what distinguishes
// "same connection" from "same number".
//
// A generation mismatch drops the packet: the Modbus master sees a timeout and retries,
// which is the correct outcome for a response whose requester is gone.
esp_err_t tcp_server_send_to_captured_client(tcp_desc_t *desc, int client_sock,
                                             uint32_t generation, uint8_t *data, size_t len)
{
    if (!desc) {
        ESP_LOGE(TAG, "No client connected");
        return ESP_FAIL;
    }

    if (!tcp_desc_conn_lock_acquire(desc, pdMS_TO_TICKS(TCP_DESC_SEND_LOCK_TIMEOUT_MS))) {
        log_send_lock_timeout(desc);
        return ESP_FAIL;
    }

    esp_err_t err;
    int send_errno = 0;
    // What this compares is DESCRIPTOR-WIDE, and on an uncapped server that is weaker than
    // it looks. conn_generation counts retirements on the whole descriptor, not on one
    // connection, so on the modbus_tcp gateway (max_connections == 0, many simultaneous
    // masters on one port) the test does not mean "is this still the same connection?" but
    // "has no connection at all been retired since the capture?".
    //
    // Concretely: master A's request goes out on RS-485, unrelated client C drops while the
    // RTU turnaround is running, the generation moves, and A's perfectly valid reply is
    // discarded. A then times out and retries. Under churn (a poller that reconnects per
    // poll, several masters cycling) that can cost a noticeable share of replies on an
    // otherwise healthy link. A client merely CONNECTING no longer costs anything — admits
    // do not bump, see register_client_conn() — which halves the sources of this loss.
    //
    // Accepted deliberately, because the error is one-sided. A false negative costs one
    // Modbus retry — a protocol that is built around timeouts and retries. The only
    // alternative failure, letting a stale pair through, writes RS-485 payload into
    // whichever socket inherited the fd number. Making this exact would mean tracking a
    // generation per fd instead of per descriptor; until the drop rate is shown to matter
    // in the field, the cheap conservative test is the right trade.
    if (generation != desc->conn_generation) {
        // Not an error worth ESP_LOGE: the peer disconnecting mid-exchange is normal, and
        // so (see above) is an unrelated client having hung up.
        ESP_LOGD(TAG, "Port %d: sock=%d captured at generation %u, now %u — dropping response",
                 desc->port, client_sock, (unsigned)generation, (unsigned)desc->conn_generation);
        err = ESP_FAIL;
    } else {
        err = send_on_socket(desc, client_sock, data, len, &send_errno);
    }

    tcp_desc_conn_lock_release(desc);

    // Outside the lock, for the reason given in log_send_error().
    if (send_errno != 0) {
        log_send_error(desc, send_errno);
    }

    return err;
}


esp_err_t tcp_server_connected(tcp_desc_t *desc)
{
    if (!desc || !desc->active_connections) {
        return ESP_FAIL;
    }
    return ESP_OK;
}


esp_err_t tcp_server_deinit(tcp_desc_t *desc)
{
    if (desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (desc->task_handle == NULL || desc->event_group == NULL) {
        ESP_LOGE(TAG, "TCP server not initialized");
        return ESP_ERR_NOT_ALLOWED;
    }

    ESP_LOGD(TAG, "Deinitializing...");

    // Signal acceptor task to stop and close listen socket to unblock accept()
    xEventGroupSetBits(desc->event_group, EVENT_TASK_EXIT_REQ);
    if (desc->listen_sock >= 0) {
        ESP_LOGD(TAG, "Closing TCP listen socket");
        close(desc->listen_sock);
    }

    // Wait for acceptor task to finish.
    ESP_LOGD(TAG, "Waiting for TCP server acceptor task finished...");
    xEventGroupWaitBits(desc->event_group, EVENT_TASK_FINISHED, pdFALSE, pdTRUE, portMAX_DELAY);

    // Wait for all receiver tasks to finish.  Each receiver task decrements
    // active_connections and calls vTaskDelete() immediately after, so polling
    // here is safe.  The event_group and desc must remain valid until every
    // receiver task has finished (receiver tasks access both via check_task_exit_req
    // and desc->port logs).
    ESP_LOGD(TAG, "Waiting for TCP server receiver tasks finished...");
    while (desc->active_connections > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Safe now: every receiver has released conn_lock before dropping the count that this
    // loop waits on (see run_receiver), so nobody can be inside the mutex when it is
    // deleted or when desc is freed.
    tcp_desc_conn_lock_deinit(desc);
    vEventGroupDelete(desc->event_group);
    free(desc);

    ESP_LOGD(TAG, "Deinitialized");
    return ESP_OK;
}

#ifdef __unittest_env__
/* Run the receiver_task logic synchronously for unit testing.
 * Allows tests to verify close_handler and active_connections behavior
 * without requiring full task infrastructure. */
void tcp_server_run_receiver_for_test(tcp_desc_t *desc, int client_sock)
{
    run_receiver(desc, client_sock);
}
#endif /* __unittest_env__ */
