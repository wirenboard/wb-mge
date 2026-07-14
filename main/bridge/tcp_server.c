#include "tcp_server.h"

#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"


#define KEEPALIVE_IDLE                  5
#define KEEPALIVE_INTERVAL              5
#define KEEPALIVE_COUNT                 3
#define RX_BUFFER_SIZE                  1024
/* TCP_SERVER_TASK_STACK_SIZE now lives in tcp_server.h: the receive handler runs
 * on the task it sizes, and cache_modbus_server reports it in its diagnostic
 * stack registers — a private copy here would silently drift out of sync. */
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

    int addr_family = (int)AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_storage dest_addr;

    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(port);

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


static int accept_connection(int listen_sock, struct sockaddr_in* source_addr)
{
    static int keep_alive = 1;
    static int keep_idle = KEEPALIVE_IDLE;
    static int keep_interval = KEEPALIVE_INTERVAL;
    static int keep_count = KEEPALIVE_COUNT;
    static int no_delay_flag = 1;

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


// Runs the recv/dispatch/close lifecycle for one accepted connection.
// Shared by receiver_task (production) and the unit-test entry point.
static void run_receiver(tcp_desc_t *desc, int client_sock)
{
    char rx_buffer[RX_BUFFER_SIZE];
    int len;

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
            // Update last_client_sock before invoking callback so that
            // consumers (e.g. transparent_tcp) can send a reply to the last sender.
            desc->last_client_sock = client_sock;
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
     * per-connection state (e.g. Modbus frame reassembly buffer). */
    if (desc->close_handler) {
        desc->close_handler(desc, client_sock);
    }

    shutdown(client_sock, SHUT_RDWR);

    // Log before decrementing so desc->port is accessed while desc is still valid.
    // deinit() waits for active_connections to reach 0 before freeing desc.
    ESP_LOGD(TAG, "Port %d receiver task finished", desc->port);
    // Decrement before close(): the fd must stay allocated while this server's
    // connection count still reads >= max_connections. Otherwise the freed fd
    // could be recycled by another tcp_server instance in the window before the
    // decrement, and the acceptor's drop-old preemption would shutdown() a socket
    // that no longer belongs to this server. close() touches no desc fields, so it
    // is safe to run after the decrement (deinit may free desc once it hits 0).
    __atomic_fetch_sub(&desc->active_connections, 1, __ATOMIC_SEQ_CST);
    close(client_sock);
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

    int prev_client_sock = -1;

    while (1) {
        if (check_task_exit_req(desc)) {
            break;
        }

        struct sockaddr_in source_addr;
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

        // Print client IP address and port
        char addr_str[32];
        if (source_addr.sin_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        } else {
            addr_str[0] = 0;
        }
        ESP_LOGI(TAG, "Socket on port %d accepted connection from %s, port: %d", desc->port, addr_str, htons(source_addr.sin_port));

        // Enforce the per-server connection cap by dropping the previously-served
        // client so the new one takes over. Only max_connections == 1 is supported
        // (used by the transparent bridge, which serves exactly one client): a single
        // prev_client_sock is tracked, so this preempts one client, not the oldest of
        // many. The acceptor is the only task that spawns receivers, so prev_client_sock
        // (this task's local) reliably identifies the currently-served socket.
        if (desc->max_connections != 0 && desc->active_connections >= desc->max_connections) {
            // run_receiver closes the old fd only AFTER decrementing active_connections,
            // so while the counter still reads >= max the old fd is guaranteed open and
            // cannot equal client_sock; the prev_client_sock != client_sock check is kept
            // as cheap defensive guarding of the fd-reuse case regardless.
            if (prev_client_sock >= 0 && prev_client_sock != client_sock) {
                ESP_LOGI(TAG, "Port %d: connection limit reached, dropping previous client to admit the new one", desc->port);
                shutdown(prev_client_sock, SHUT_RDWR);  // unblocks the old receiver's recv(); it then decrements active_connections and closes the fd
            }
            // Wait for the preempted receiver to fully exit before admitting the new client.
            bool exiting = false;
            while (desc->active_connections >= desc->max_connections) {
                if (check_task_exit_req(desc)) { exiting = true; break; }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (exiting) {
                close(client_sock);
                break;   // deinit requested — leave the acceptor loop
            }
        }

        // Allocate args for receiver_task on the heap; freed by receiver_task itself
        receiver_task_args_t *args = malloc(sizeof(receiver_task_args_t));
        if (!args) {
            ESP_LOGE(TAG, "Port %d: failed to allocate receiver_task args, closing connection", desc->port);
            close(client_sock);
            continue;
        }
        args->desc = desc;
        args->client_sock = client_sock;

        __atomic_fetch_add(&desc->active_connections, 1, __ATOMIC_SEQ_CST);

        // Create a unique task name using the socket fd number
        char task_name[32];
        snprintf(task_name, sizeof(task_name), "tcp_recv_%d", client_sock);

        BaseType_t ret = xTaskCreate(receiver_task, task_name, TCP_SERVER_TASK_STACK_SIZE, args, TCP_SERVER_TASK_PRIORITY, NULL);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Port %d: failed to create receiver_task, closing connection", desc->port);
            free(args);
            close(client_sock);
            __atomic_fetch_sub(&desc->active_connections, 1, __ATOMIC_SEQ_CST);
        } else {
            // Record the now-served socket so the next over-limit accept preempts it.
            prev_client_sock = client_sock;
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

    desc->listen_sock = listen_sock;
    desc->last_client_sock = -1;
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


esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
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
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        return ESP_FAIL;
    }

    if (res != len) {
        ESP_LOGW(TAG, "Not all data sent, required: %u, sent: %d", len, res);
    }

    return ESP_OK;
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
