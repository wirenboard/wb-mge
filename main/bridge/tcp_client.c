#include "tcp_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define KEEPALIVE_IDLE                  5
#define KEEPALIVE_INTERVAL              5
#define KEEPALIVE_COUNT                 3
#define RX_BUFFER_SIZE                  1024
#define TCP_CLIENT_TASK_STACK_SIZE      4096
#define TCP_CLIENT_TASK_PRIORITY        5
#define TCP_CLIENT_FIRST_CONN_DELAY_MS  4000
#define TCP_CLIENT_RECONN_DELAY_MS      1000
#define TCP_CLIENT_MAX_RECONN_DELAY_MS  30000  // exponential-backoff ceiling for retries to an unreachable host
#define TCP_CLIENT_CONNECT_TIMEOUT_MS   3000   // bounded connect timeout (was a blocking connect with no timeout)
#define TCP_CLIENT_CONNECT_POLL_MS      200    // re-check the exit-request flag this often during connect

#define EVENT_TASK_STARTED              BIT0
#define EVENT_TASK_FINISHED             BIT1
#define EVENT_TASK_EXIT_REQ             BIT8


static const char *TAG = "tcp_client";


static inline bool delay_until_exit_req(tcp_desc_t *desc, TickType_t ticks)
{
    EventBits_t bits = xEventGroupWaitBits(desc->event_group, EVENT_TASK_EXIT_REQ, pdFALSE, pdTRUE, ticks);
    if (bits & EVENT_TASK_EXIT_REQ) {
        return true;
    }
    return false;
}


static inline bool check_task_exit_req(tcp_desc_t *desc)
{
    return delay_until_exit_req(desc, 0);
}


static void close_socket(int sock)
{
    if (sock != -1) {
        ESP_LOGD(TAG, "Shutting down socket %d", sock);
        shutdown(sock, SHUT_RDWR);
        closesocket(sock);
    }
}


// Publish a freshly created socket as this descriptor's connection.
// Under conn_lock, for the same reason as in tcp_server.c: the field is read by the UART
// event task through tcp_client_send_to_current_client().
//
// No generation bump, and none is needed — see the equivalent function in tcp_server.c: a
// captured (fd, generation) pair can only be fooled by REUSE of the fd number, reuse
// requires the previous socket to have been closed, and every close in this module goes
// through retire_client_conn(), which does bump.
static void register_client_conn(tcp_desc_t *desc, int sock)
{
    tcp_desc_conn_lock_acquire(desc, portMAX_DELAY);
    desc->last_client_sock = sock;
    tcp_desc_conn_lock_release(desc);
}


// Retire the current connection: stop routing to it, invalidate captured pairs and close
// it — all under conn_lock, so the UART event task cannot be inside send() with this fd
// while it is closed and its number handed to some other socket (this is a bridge port; a
// second port's client, or httpd, is a realistic next owner of that number).
//
// Idempotent: a descriptor with no socket retires to a no-op, so deinit and the client
// task can both call it without coordinating.
static void retire_client_conn(tcp_desc_t *desc)
{
    tcp_desc_conn_lock_acquire(desc, portMAX_DELAY);
    int sock = desc->last_client_sock;
    desc->last_client_sock = -1;
    // Atomic for the sake of the one UNLOCKED reader, tcp_desc_conn_generation(): pairing a
    // plain store with its __atomic_load_n() is a data race by the C11 model. Under the lock
    // this costs exactly what the plain increment cost — see the field comment in tcp_desc.h.
    //
    // Bumped even when sock is already -1, i.e. when nothing is closed here. Deliberate: the
    // counter is an upper bound on closes, and an extra bump can only invalidate a captured
    // pair that would have been accepted, never the reverse.
    __atomic_add_fetch(&desc->conn_generation, 1, __ATOMIC_RELAXED);
    close_socket(sock);
    tcp_desc_conn_lock_release(desc);
}


// Rate-limited warning for a packet dropped because conn_lock could not be taken in time —
// see the identical helper in tcp_server.c for why the log is throttled and why the counter
// lives in the descriptor rather than in a static here.
static void log_send_lock_timeout(tcp_desc_t *desc)
{
    uint32_t dropped = __atomic_add_fetch(&desc->send_lock_timeouts, 1, __ATOMIC_RELAXED);

    if ((dropped == 1u) || ((dropped % 64u) == 0u)) {
        ESP_LOGW(TAG, "Port %d: connection lock busy for %ums, packet dropped (%u dropped so far)",
                 desc->port, (unsigned)TCP_DESC_SEND_LOCK_TIMEOUT_MS, (unsigned)dropped);
    }
}


// Rate-limited error for a packet that send() refused — the twin of the helper of the same
// name in tcp_server.c, see it for why this failure in particular must be throttled (a
// stalled peer receive window fails EVERY packet, on the UART event task) and why the
// counter lives in the descriptor.
//
// MUST be called with conn_lock RELEASED: the only sender below captures errno under the
// lock and reports it afterwards, so the synchronous console write does not extend the
// critical section — and with it the portMAX_DELAY wait of retire_client_conn().
static void log_send_error(tcp_desc_t *desc, int send_errno)
{
    uint32_t failures = __atomic_add_fetch(&desc->send_errors, 1, __ATOMIC_RELAXED);

    if ((failures == 1u) || ((failures % 64u) == 0u)) {
        ESP_LOGE(TAG, "Port %d: error occurred during sending: errno %d (%u failed so far)",
                 desc->port, send_errno, (unsigned)failures);
    }
}


static int create_socket(void)
{
    static int keep_alive = 1;
    static int keep_idle = KEEPALIVE_IDLE;
    static int keep_interval = KEEPALIVE_INTERVAL;
    static int keep_count = KEEPALIVE_COUNT;
    static int no_delay_flag = 1;

    ESP_LOGD(TAG, "Creating client socket");

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
    } else {
        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keep_interval, sizeof(keep_interval));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keep_count, sizeof(keep_count));

        // No delay for send() function (disable Nagle's algorithm)
        // It is necessary that data packets are not combined when sent and to increase the performance
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &no_delay_flag, sizeof(no_delay_flag));

        ESP_LOGD(TAG, "Socket created");
    }

    return sock;
}


// Non-blocking connect with a bounded, abort-aware timeout.
// A plain blocking connect() to an unreachable host blocks for the whole lwIP
// connect timeout (many seconds). tcp_client_deinit() runs under the
// port_manager lock and waits for this task to finish, so a stuck connect would
// freeze settings/port-mode changes. By doing the connect in non-blocking mode
// and polling the exit-request flag, deinit can abort the connect promptly.
static int connect_socket(tcp_desc_t *desc, int sock, uint32_t ip, int port)
{
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = ip;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dest_addr.sin_addr, ip_str, sizeof(ip_str));
    ESP_LOGI(TAG, "Connecting to %s, port: %d", ip_str, port);

    // Switch to non-blocking mode so connect() returns immediately with EINPROGRESS
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        // F_GETFL failed: a corrupt flags value would silently leave the socket
        // blocking on restore, re-introducing the hang this code prevents
        ESP_LOGW(TAG, "fcntl(F_GETFL) failed for %s, port: %d, errno: %d", ip_str, port, errno);
        return -1;
    }
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (err == 0) {
        // Connection completed immediately; restore blocking mode for recv()
        fcntl(sock, F_SETFL, flags);
        ESP_LOGI(TAG, "Successfully connected to %s, port: %d", ip_str, port);
        return 0;
    }

    if (errno != EINPROGRESS) {
        // Real immediate failure; caller closes the socket
        ESP_LOGW(TAG, "Socket unable to connect to %s, port: %d, errno: %d", ip_str, port, errno);
        return err;
    }

    // Connection is in progress: wait for it, polling the exit-request flag so
    // deinit can abort within one poll interval. Caller closes the socket on any
    // error/timeout/abort path; only restore blocking mode on success.
    // Use a real-clock deadline so repeated EINTR interruptions cannot extend
    // the wait beyond the configured timeout. TickType_t subtraction is
    // wrap-safe under unsigned arithmetic.
    TickType_t start_tick = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(TCP_CLIENT_CONNECT_TIMEOUT_MS)) {
        if (check_task_exit_req(desc)) {
            return -1;
        }

        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(sock, &writefds);

        struct timeval tv;
        tv.tv_sec = TCP_CLIENT_CONNECT_POLL_MS / 1000;
        tv.tv_usec = (TCP_CLIENT_CONNECT_POLL_MS % 1000) * 1000;

        int sel = select(sock + 1, NULL, &writefds, NULL, &tv);
        if (sel < 0) {
            if (errno == EINTR) {
                continue;
            }
            ESP_LOGW(TAG, "Socket unable to connect to %s, port: %d, errno: %d", ip_str, port, errno);
            return -1;
        }

        if (sel > 0 && FD_ISSET(sock, &writefds)) {
            int so_err = 0;
            socklen_t l = sizeof(so_err);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_err, &l);
            if (so_err == 0) {
                // Connection established; restore blocking mode for recv()
                fcntl(sock, F_SETFL, flags);
                ESP_LOGI(TAG, "Successfully connected to %s, port: %d", ip_str, port);
                return 0;
            }
            errno = so_err;
            ESP_LOGW(TAG, "Socket unable to connect to %s, port: %d, errno: %d", ip_str, port, errno);
            return -1;
        }

        // select() returned 0: no event this interval, keep waiting until the
        // real-clock deadline (re-checked by the loop condition)
    }

    ESP_LOGW(TAG, "Connect to %s, port: %d timed out after %d ms", ip_str, port, TCP_CLIENT_CONNECT_TIMEOUT_MS);
    return -1;
}


// Pump the connection until it drops. The socket is passed in rather than read back out of
// desc->last_client_sock: this task created that fd and owns it for the whole call, so the
// local is both cheaper and — more to the point — keeps "last_client_sock is only ever read
// under conn_lock" a real invariant rather than one with a documented exception. Reading the
// field here would also be observably wrong once tcp_client_deinit() retires the connection
// concurrently: the field goes to -1 while this fd is still the one recv() must be woken on.
static void receive_data(tcp_desc_t *desc, int sock)
{
    char ip_str[INET_ADDRSTRLEN];
    struct in_addr sin_addr = {.s_addr = desc->remote_ip};
    inet_ntop(AF_INET, &sin_addr, ip_str, sizeof(ip_str));

    char rx_buffer[RX_BUFFER_SIZE];
    int len = 0;

    while (1) {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);

        if (len < 0) {
            esp_log_level_t log_level = ESP_LOG_ERROR;
            if (check_task_exit_req(desc)) {
                log_level = ESP_LOG_DEBUG;
            }
            ESP_LOG_LEVEL(log_level, TAG, "Receive from %s, port %d failed, errno: %d", ip_str, desc->port, errno);
            break;
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection to %s, port %d closed", ip_str, desc->port);
            break;
        } else {
            ESP_LOGD(TAG, "Received %d bytes from %s, port %d", len, ip_str, desc->port);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buffer, len, ESP_LOG_DEBUG);
            desc->receive_handler(desc, sock, (uint8_t *)rx_buffer, len);
        }
    }
}


static void tcp_client_task(void *pvParameters)
{
    tcp_desc_t *desc = (tcp_desc_t *)pvParameters;
    xEventGroupSetBits(desc->event_group, EVENT_TASK_STARTED);
    ESP_LOGD(TAG, "TCP client task started");

    char ip_str[INET_ADDRSTRLEN];
    struct in_addr sin_addr = {.s_addr = desc->remote_ip};
    inet_ntop(AF_INET, &sin_addr, ip_str, sizeof(ip_str));

    // Small delay for network to become ready before connecting to server
    delay_until_exit_req(desc, pdMS_TO_TICKS(TCP_CLIENT_FIRST_CONN_DELAY_MS));

    int reconnect_delay_ms = TCP_CLIENT_RECONN_DELAY_MS;  // grows on consecutive connect failures (exponential backoff)

    while (1) {
        if (check_task_exit_req(desc)) {
            break;
        }

        int sock = create_socket();
        if (sock < 0) {
            continue;
        }
        register_client_conn(desc, sock);

        if (connect_socket(desc, sock, desc->remote_ip, desc->port) != 0) {
            retire_client_conn(desc);
            delay_until_exit_req(desc, pdMS_TO_TICKS(reconnect_delay_ms));
            // Exponential backoff: double the delay after each consecutive failure, capped at the ceiling,
            // so an unreachable host is retried ever more slowly instead of being hammered every few seconds.
            reconnect_delay_ms *= 2;
            if (reconnect_delay_ms > TCP_CLIENT_MAX_RECONN_DELAY_MS) { reconnect_delay_ms = TCP_CLIENT_MAX_RECONN_DELAY_MS; }
            continue;
        }

        __atomic_fetch_add(&desc->active_connections, 1, __ATOMIC_SEQ_CST);
        reconnect_delay_ms = TCP_CLIENT_RECONN_DELAY_MS;  // connection succeeded — reset backoff

        receive_data(desc, sock);
        ESP_LOGW(TAG, "Disconnected from server: %s, port: %d", ip_str, desc->port);
        retire_client_conn(desc);
        // Reset to 0 rather than decrement: only one client task writes this field,
        // so the single-writer property holds. Use an atomic store (matching the
        // __atomic_fetch_add above and the __ATOMIC_SEQ_CST invariant documented in
        // tcp_desc.h) to keep the access discipline on this field uniform.
        // Clear it right after the socket is closed, before the backoff delay, so
        // tcp_client_connected() does not report "connected" while we wait to reconnect.
        __atomic_store_n(&desc->active_connections, 0, __ATOMIC_SEQ_CST);
        delay_until_exit_req(desc, pdMS_TO_TICKS(reconnect_delay_ms));
    }

    ESP_LOGI(TAG, "TCP client task finished");
    xEventGroupSetBits(desc->event_group, EVENT_TASK_FINISHED);
    vTaskDelete(NULL);
}


esp_err_t tcp_client_init(uint32_t host_ip, uint16_t host_port,
                          tcp_receive_handler_t tcpc_receive_handler, tcp_desc_t **out_desc)
{
    if (tcpc_receive_handler == NULL) {
        ESP_LOGE(TAG, "tcpc_receive_handler is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (out_desc == NULL) {
        ESP_LOGE(TAG, "out_desc is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Remote IP: %d.%d.%d.%d", (int)((host_ip >> 0) & 0xFF), (int)((host_ip >> 8) & 0xFF), (int)((host_ip >> 16) & 0xFF), (int)((host_ip >> 24) & 0xFF));

    tcp_desc_t *desc = calloc(1, sizeof(tcp_desc_t));
    if (!desc) {
        return ESP_ERR_NO_MEM;
    }

    EventGroupHandle_t event_group = xEventGroupCreate();
    if (event_group == NULL) {
        ESP_LOGE(TAG, "Unable to create event group");
        free(desc);
        return ESP_ERR_NO_MEM;
    }

    // Fail rather than run without it: the lock is what keeps the UART event task out of
    // send() while the socket is being closed and its fd number recycled.
    if (!tcp_desc_conn_lock_init(desc)) {
        ESP_LOGE(TAG, "Unable to create connection mutex");
        vEventGroupDelete(event_group);
        free(desc);
        return ESP_ERR_NO_MEM;
    }

    desc->listen_sock = -1;         // not used for client
    desc->last_client_sock = -1;
    desc->conn_generation = 0;
    desc->remote_ip = host_ip;
    desc->port = host_port;
    desc->receive_handler = tcpc_receive_handler;
    desc->active_connections = 0;
    desc->task_handle = NULL;
    desc->event_group = event_group;

    TaskHandle_t task_handle = NULL;
    BaseType_t ret = xTaskCreate(tcp_client_task, "tcp_client", TCP_CLIENT_TASK_STACK_SIZE, desc, TCP_CLIENT_TASK_PRIORITY, &task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Unable to create TCP client task");
        tcp_desc_conn_lock_deinit(desc);
        vEventGroupDelete(event_group);
        free(desc);
        return ESP_ERR_NO_MEM;
    }

    desc->task_handle = task_handle;
    *out_desc = desc;

    return ESP_OK;
}


// Send on the current outbound connection (client mode has exactly one).
//
// Called from the UART event task, so the socket is read and used under conn_lock: the
// client task closes and re-creates that socket on every reconnect, and the fd number it
// releases is immediately available to any other socket in the system.
//
// The wait for the lock is bounded (TCP_DESC_SEND_LOCK_TIMEOUT_MS); on timeout the packet
// is dropped rather than the UART task blocked — the same trade-off that makes the send
// itself MSG_DONTWAIT.
esp_err_t tcp_client_send_to_current_client(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    if (!desc) {
        ESP_LOGE(TAG, "No client socket");
        return ESP_FAIL;
    }

    if (!tcp_desc_conn_lock_acquire(desc, pdMS_TO_TICKS(TCP_DESC_SEND_LOCK_TIMEOUT_MS))) {
        log_send_lock_timeout(desc);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    int send_errno = 0;
    int sock = desc->last_client_sock;

    if (sock < 0) {
        ESP_LOGE(TAG, "No client socket");
    } else {
        // Using non-blocking function to avoid blocking uart_event_task()
        // Otherwise UART event queue overflows and packets start to merge and drop
        // TCP has its own transmit buffer and window under the hood, should not be a problem
        int res = send(sock, data, len, MSG_DONTWAIT);

        if (res < 0) {
            // Only captured here; reported below, once the lock is gone.
            send_errno = errno;
        } else {
            if (res != len) {
                ESP_LOGW(TAG, "Not all data sent, required: %u, sent: %d", len, res);
            }
            ret = ESP_OK;
        }
    }

    tcp_desc_conn_lock_release(desc);

    // Deliberately after the release: an EAGAIN storm would otherwise hold conn_lock across
    // a synchronous console write on every packet — see log_send_error().
    if (send_errno != 0) {
        log_send_error(desc, send_errno);
    }

    return ret;
}


esp_err_t tcp_client_connected(tcp_desc_t *desc)
{
    if (!desc || !desc->active_connections) {
        return ESP_FAIL;
    }
    return ESP_OK;
}


esp_err_t tcp_client_deinit(tcp_desc_t *desc)
{
    if (desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (desc->task_handle == NULL || desc->event_group == NULL) {
        ESP_LOGE(TAG, "TCP client not initialized");
        return ESP_ERR_NOT_ALLOWED;
    }

    ESP_LOGD(TAG, "Deinitializing...");

    xEventGroupSetBits(desc->event_group, EVENT_TASK_EXIT_REQ);
    // Close the socket to unblock a recv() the client task may be sitting in. Through the
    // same retire path as everywhere else, so the field is cleared and the generation
    // invalidated under conn_lock: otherwise a serial packet arriving right here would be
    // sent on an fd this function has just closed.
    //
    // This does NOT mean the descriptor is socket-free from here on. The client task may
    // have passed check_task_exit_req() and be inside create_socket()/connect_socket()
    // right now, in which case it publishes a NEW socket after this retire — the retire is
    // idempotent, not a latch. That socket is still cleaned up, by the task's own retire on
    // its way out (connect failure, or the end of receive_data()), and the
    // EVENT_TASK_FINISHED wait below is what makes the cleanup ordered: the task is gone
    // before the mutex and the descriptor are freed. So the guarantee this retire provides
    // is "no fd survives deinit", reached by two retires, not by this one alone.
    //
    // Known residuals, unrelated to the above and deliberately left alone:
    //   - if connect() completes IMMEDIATELY (connect_socket() returning 0 on the spot,
    //     realistically only for a loopback/link-local peer), the task goes straight into
    //     receive_data(), whose recv() has no SO_RCVTIMEO — unlike tcp_server, which sets
    //     one in accept_connection(). Nothing then interrupts it until the peer sends or
    //     closes, and the wait below is portMAX_DELAY, so deinit blocks for as long as that
    //     takes;
    //   - tcp_client_task() publishes the fd with register_client_conn() BEFORE it calls
    //     connect_socket(), and connect_socket() then works on its own local copy, outside
    //     conn_lock. The retire below can therefore land in that gap and close the fd while
    //     the client task sits between connect_socket()'s fcntl(F_GETFL) and its connect();
    //     if lwIP has already handed the number to another socket, the task sets O_NONBLOCK
    //     on, and connects, a stranger's fd. Registering only AFTER a successful connect
    //     would close the gap, at the price of a connect() the descriptor does not know
    //     about and this function therefore could not abort — a worse trade for a window
    //     that is pre-existing, narrow (a couple of syscalls) and reachable only by a deinit
    //     concurrent with the very first moments of a connection attempt.
    ESP_LOGD(TAG, "Closing TCP client socket");
    retire_client_conn(desc);

    ESP_LOGD(TAG, "Waiting for TCP client task finished...");
    xEventGroupWaitBits(desc->event_group, EVENT_TASK_FINISHED, pdFALSE, pdTRUE, portMAX_DELAY);

    // No active_connections wait here, and none is needed: client mode owns exactly one
    // task, and this is an exact join on it rather than tcp_server's poll of a counter.
    // Every task that takes conn_lock from INSIDE this module is therefore gone by now.
    //
    // What the join does NOT cover — in either module — is the external producer: the UART
    // event task calls tcp_client_send_to_current_client() and can be waiting up to
    // TCP_DESC_SEND_LOCK_TIMEOUT_MS on the mutex deleted just below. Keeping that task out
    // is the caller's job, and transparent_tcp_deinit_port() (the only caller) does it by
    // running serial_deinit() first — see the ordering note there.
    tcp_desc_conn_lock_deinit(desc);
    vEventGroupDelete(desc->event_group);
    free(desc);

    ESP_LOGD(TAG, "Deinitialized");
    return ESP_OK;
}
