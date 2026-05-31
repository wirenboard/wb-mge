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
#include "freertos/atomic.h"


#define KEEPALIVE_IDLE                  5
#define KEEPALIVE_INTERVAL              5
#define KEEPALIVE_COUNT                 3
#define RX_BUFFER_SIZE                  1024
#define TCP_CLIENT_TASK_STACK_SIZE      4096
#define TCP_CLIENT_TASK_PRIORITY        5
#define TCP_CLIENT_FIRST_CONN_DELAY_MS  4000
#define TCP_CLIENT_RECONN_DELAY_MS      1000
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


static void receive_data(tcp_desc_t *desc)
{
    char ip_str[INET_ADDRSTRLEN];
    struct in_addr sin_addr = {.s_addr = desc->remote_ip};
    inet_ntop(AF_INET, &sin_addr, ip_str, sizeof(ip_str));

    char rx_buffer[RX_BUFFER_SIZE];
    int len = 0;

    while (1) {
        len = recv(desc->last_client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);

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
            desc->receive_handler(desc, desc->last_client_sock, (uint8_t *)rx_buffer, len);
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

    while (1) {
        if (check_task_exit_req(desc)) {
            break;
        }

        desc->last_client_sock = create_socket();
        if (desc->last_client_sock < 0) {
            continue;
        }

        if (connect_socket(desc, desc->last_client_sock, desc->remote_ip, desc->port) != 0) {
            close_socket(desc->last_client_sock);
            desc->last_client_sock = -1;
            delay_until_exit_req(desc, pdMS_TO_TICKS(TCP_CLIENT_RECONN_DELAY_MS));
            continue;
        }

        Atomic_Increment_u32(&desc->active_connections);

        receive_data(desc);
        ESP_LOGW(TAG, "Disconnected from server: %s, port: %d", ip_str, desc->port);
        close_socket(desc->last_client_sock);
        desc->last_client_sock = -1;
        delay_until_exit_req(desc, pdMS_TO_TICKS(TCP_CLIENT_RECONN_DELAY_MS));
        // Reset to 0 rather than decrement: only one client task writes this field.
        // Plain store is safe on ESP32: internal SRAM is shared (no per-core data cache),
        // so the write is immediately visible to concurrent readers without a memory barrier.
        desc->active_connections = 0;
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

    desc->listen_sock = -1;         // not used for client
    desc->last_client_sock = -1;
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
        vEventGroupDelete(event_group);
        free(desc);
        return ESP_ERR_NO_MEM;
    }

    desc->task_handle = task_handle;
    *out_desc = desc;

    return ESP_OK;
}


esp_err_t tcp_client_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
{
    // In client mode there is always exactly one outbound connection; use the stored socket.
    (void)client_sock;
    if (!desc || (desc->last_client_sock < 0)) {
        ESP_LOGE(TAG, "No client socket");
        return ESP_FAIL;
    }

    // Using non-blocking function to avoid blocking uart_event_task()
    // Otherwise UART event queue overflows and packets start to merge and drop
    // TCP has its own transmit buffer and window under the hood, should not be a problem
    int res = send(desc->last_client_sock, data, len, MSG_DONTWAIT);

    if (res < 0) {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        return ESP_FAIL;
    }

    if (res != len) {
        ESP_LOGW(TAG, "Not all data sent, required: %u, sent: %d", len, res);
    }

    return ESP_OK;
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
    if (desc->last_client_sock >= 0) {
        ESP_LOGD(TAG, "Closing TCP client socket");
        close(desc->last_client_sock);
    }

    ESP_LOGD(TAG, "Waiting for TCP client task finished...");
    xEventGroupWaitBits(desc->event_group, EVENT_TASK_FINISHED, pdFALSE, pdTRUE, portMAX_DELAY);

    vEventGroupDelete(desc->event_group);
    free(desc);

    ESP_LOGD(TAG, "Deinitialized");
    return ESP_OK;
}
