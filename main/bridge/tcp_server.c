#include "tcp_server.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"


#define KEEPALIVE_IDLE                  5
#define KEEPALIVE_INTERVAL              5
#define KEEPALIVE_COUNT                 3
#define RX_BUFFER_SIZE                  1024
#define TCP_SERVER_TASK_STACK_SIZE      4096
#define TCP_SERVER_TASK_PRIORITY        5

#define EVENT_TASK_STARTED              BIT0
#define EVENT_TASK_FINISHED             BIT1
#define EVENT_TASK_EXIT_REQ             BIT8


static const char *TAG = "tcp_server";
static bool copy_protection = false;


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
    int ip_protocol = 0;
    struct sockaddr_storage dest_addr;

    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(port);
    ip_protocol = IPPROTO_IP;

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return listen_sock;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ESP_LOGD(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        close(listen_sock);
        return -1;
    }
    ESP_LOGD(TAG, "Socket bound, port %d", port);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(listen_sock);
        return -1;
    }

    ESP_LOGD(TAG, "Socket listening");
    return listen_sock;
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

    return client_sock;
}


static void receive_data(tcp_desc_t *desc)
{
    char rx_buffer[RX_BUFFER_SIZE];
    int len;

    do {
        len = recv(desc->client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);

        if (len < 0) {
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
            if (!copy_protection) {
                desc->receive_handler(desc, (uint8_t *)rx_buffer, len);
            }
        }
    } while (len > 0);
}


static void tcp_server_task(void *pvParameters)
{
    tcp_desc_t *desc = (tcp_desc_t *)pvParameters;
    xEventGroupSetBits(desc->event_group, EVENT_TASK_STARTED);
    ESP_LOGD(TAG, "TCP server task started");

    while (1) {
        if (check_task_exit_req(desc)) {
            break;
        }

        struct sockaddr_in source_addr;
        desc->client_sock = accept_connection(desc->listen_sock, &source_addr);
        if (desc->client_sock < 0) {
            if (check_task_exit_req(desc)) {
                ESP_LOGD(TAG, "Socket on port %d returned error %d during connection accept", desc->port, errno);
                break;
            } else {
                ESP_LOGE(TAG, "Unable to accept connection on port %d, errno: %d", desc->port, errno);
                // Try to re-create listen socket
                vTaskDelay(pdMS_TO_TICKS(1000));
                close(desc->listen_sock);
                desc->listen_sock = create_listen_socket(desc->port);
                if (desc->listen_sock < 0) {
                    ESP_LOGE(TAG, "Failed to re-create listen socket");
                    break;
                }
                continue;
            }
        }

        // Print client IP address and port
        char addr_str[32];
        if (source_addr.sin_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        } else {
            addr_str[0] = 0;
        }
        ESP_LOGI(TAG, "Socket on port %d accepted connection from %s, port: %d", desc->port, addr_str, htons(source_addr.sin_port));

        desc->active_connections++;

        receive_data(desc);

        shutdown(desc->client_sock, SHUT_RDWR);
        close(desc->client_sock);
        desc->client_sock = -1;
        desc->active_connections--;
    }

    close(desc->listen_sock);
    desc->listen_sock = -1;
    ESP_LOGI(TAG, "TCP server task finished");
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
    desc->client_sock = -1;
    desc->remote_ip = 0;
    desc->port = port;
    desc->receive_handler = tcps_receive_handler;
    desc->active_connections = 0;
    desc->task_handle = NULL;
    desc->event_group = event_group;

    TaskHandle_t task_handle = NULL;
    BaseType_t ret = xTaskCreate(tcp_server_task, "tcp_server", TCP_SERVER_TASK_STACK_SIZE, desc, TCP_SERVER_TASK_PRIORITY, &task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Unable to create TCP client task");
        vEventGroupDelete(event_group);
        free(desc);
        close(listen_sock);
        return ESP_ERR_NO_MEM;
    }

    desc->task_handle = task_handle;
    xEventGroupWaitBits(desc->event_group, EVENT_TASK_STARTED, pdFALSE, pdTRUE, portMAX_DELAY);

    *out_desc = desc;
    return ESP_OK;
}


esp_err_t tcp_server_send(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    if (copy_protection) {
        return ESP_OK;
    }

    if (!desc || (desc->client_sock < 0)) {
        ESP_LOGE(TAG, "No client connected");
        return ESP_FAIL;
    }

    // Using non-blocking function to avoid blocking uart_event_task()
    // Otherwise UART event queue overflows and packets start to merge and drop
    // TCP has its own transmit buffer and window under the hood, should not be a problem
    int res = send(desc->client_sock, data, len, MSG_DONTWAIT);

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
    if (!desc || (desc->client_sock < 0)) {
        return ESP_FAIL;
    }
    if (!desc->active_connections) {
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

    xEventGroupSetBits(desc->event_group, EVENT_TASK_EXIT_REQ);
    if (desc->listen_sock >= 0) {
        ESP_LOGD(TAG, "Closing TCP listen socket");
        close(desc->listen_sock);
    }
    if (desc->client_sock >= 0) {
        ESP_LOGD(TAG, "Shutting down TCP client socket");
        shutdown(desc->client_sock, SHUT_RDWR);
    }

    ESP_LOGD(TAG, "Waiting for TCP server task finished...");
    xEventGroupWaitBits(desc->event_group, EVENT_TASK_FINISHED, pdFALSE, pdTRUE, portMAX_DELAY);

    vEventGroupDelete(desc->event_group);
    free(desc);

    ESP_LOGD(TAG, "Deinitialized");
    return ESP_OK;
}


void tcp_server_activate_copy_protection(void)
{
    copy_protection = true;
}
