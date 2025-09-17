#include "tcp_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
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

static const char *TAG = "tcp-client";

static void close_socket(int sock)
{
    if (sock != -1) {
        ESP_LOGW(TAG, "Shutting down socket and restarting...");
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

        ESP_LOGI(TAG, "Socket created");
    }

    return sock;
}

static int connect_socket(int sock, uint32_t ip, int port)
{
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = ip;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dest_addr.sin_addr, ip_str, sizeof(ip_str));
    ESP_LOGI(TAG, "Connecting to %s, port: %d", ip_str, port);

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (err != 0) {
        ESP_LOGW(TAG, "Socket unable to connect to %s, port: %d, errno: %d", ip_str, port, errno);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    } else {
        ESP_LOGI(TAG, "Successfully connected to %s, port: %d", ip_str, port);
    }

    return err;
}

static void receive_data(tcp_desc_t *desc)
{
    char ip_str[INET_ADDRSTRLEN];
    struct in_addr sin_addr = {.s_addr = desc->remote_ip};
    inet_ntop(AF_INET, &sin_addr, ip_str, sizeof(ip_str));

    char rx_buffer[RX_BUFFER_SIZE];
    int len = 0;

    while (1) {
        len = recv(desc->client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);

        if (len < 0) {
            ESP_LOGE(TAG, "Receive from %s, port %d failed, errno: %d", ip_str, desc->port, errno);
            break;
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection to %s, port %d closed", ip_str, desc->port);
            break;
        } else {
            ESP_LOGD(TAG, "Received %d bytes from %s, port %d", len, ip_str, desc->port);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buffer, len, ESP_LOG_DEBUG);
            desc->receive_handler(desc, (uint8_t *)rx_buffer, len);
        }
    }
}

static void tcp_client_task(void *pvParameters)
{
    tcp_desc_t *desc = (tcp_desc_t *)pvParameters;

    char ip_str[INET_ADDRSTRLEN];
    struct in_addr sin_addr = {.s_addr = desc->remote_ip};
    inet_ntop(AF_INET, &sin_addr, ip_str, sizeof(ip_str));

    // Небольшая задержка, пока сеть заработает и можно будет подключаться к серверу
    vTaskDelay(pdMS_TO_TICKS(TCP_CLIENT_FIRST_CONN_DELAY_MS));

    while (1) {
        desc->listen_sock = -1; // not used for client
        desc->client_sock = create_socket();
        if (desc->client_sock < 0) {
            continue;
        }

        if (connect_socket(desc->client_sock, desc->remote_ip, desc->port) != 0) {
            close_socket(desc->client_sock);
            continue;
        }

        desc->active_connections++;

        receive_data(desc);
        ESP_LOGW(TAG, "Disconnected from server: %s, port: %d", ip_str, desc->port);
        close_socket(desc->client_sock);
        desc->active_connections = 0;
    }
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

    desc->listen_sock = -1;
    desc->client_sock = -1;
    desc->remote_ip = host_ip;
    desc->port = host_port;
    desc->receive_handler = tcpc_receive_handler;
    desc->active_connections = 0;
    *out_desc = desc;

    xTaskCreate(tcp_client_task, "tcp_client", TCP_CLIENT_TASK_STACK_SIZE, desc, TCP_CLIENT_TASK_PRIORITY, NULL);
    return ESP_OK;
}

esp_err_t tcp_client_send(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    if (!desc || (desc->client_sock < 0)) {
        ESP_LOGE(TAG, "No client socket");
        return ESP_FAIL;
    }

    // Используется неблокирующая функция, чтобы не блокировать задачу uart_event_task()
    // Иначе переполняется очередь событий UART и пакеты начинают склеиваться и дропаться
    // В TCP под капотом есть свой буфер передачи и окно, проблем быть не должно
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

esp_err_t tcp_client_connected(tcp_desc_t *desc)
{
    if (!desc || (desc->client_sock < 0)) {
        return ESP_FAIL;
    }
    if (!desc->active_connections) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
