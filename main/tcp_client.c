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

#define RX_BUFFER_SIZE                  1024
#define TCP_CLIENT_TASK_STACK_SIZE      4096
#define TCP_CLIENT_TASK_PRIORITY        5

static const char *TAG = "tcp-client";

static void close_socket(int sock)
{
    if (sock != -1) {
        ESP_LOGE(TAG, "Shutting down socket and restarting...");
        shutdown(sock, SHUT_RDWR);
        closesocket(sock);
    }
}

static int create_socket(void)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
    } else {
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
    ESP_LOGI(TAG, "Connecting to %s:%d", ip_str, port);

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    } else {
        ESP_LOGI(TAG, "Successfully connected");
    }
    return err;
}

static void receive_data(tcp_desc_t *desc)
{
    char rx_buffer[RX_BUFFER_SIZE];
    int len = 0;

    while (1) {
        len = recv(desc->client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "recv failed: errno %d", errno);
            break;
        } else {
            ESP_LOGD(TAG, "Received %d bytes", len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buffer, len, ESP_LOG_DEBUG);
            desc->receive_handler(desc, (uint8_t *)rx_buffer, len);
        }
    }
}

static void tcp_client_task(void *pvParameters)
{
    tcp_desc_t *desc = (tcp_desc_t *)pvParameters;

    while (1) {
        desc->listen_sock = -1; // not used for client
        desc->client_sock = create_socket();
        if (desc->client_sock < 0) {
            continue;
        }

        if (connect_socket(desc->client_sock, /*ip*/0, desc->port) != 0) {
            close_socket(desc->client_sock);
            continue;
        }

        receive_data(desc);
        ESP_LOGI(TAG, "Disconnected from server");
        close_socket(desc->client_sock);
    }
}

esp_err_t tcp_client_init(uint32_t host_ip, uint16_t host_port,
                          tcp_receive_handler_t tcpc_receive_handler, tcp_desc_t **out_desc)
{
    if (tcpc_receive_handler == NULL) {
        ESP_LOGE(TAG, "tcpc_receive_handler is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    tcp_desc_t *desc = calloc(1, sizeof(tcp_desc_t));
    if (!desc) {
        return ESP_ERR_NO_MEM;
    }

    desc->port = host_port;
    desc->receive_handler = tcpc_receive_handler;
    desc->listen_sock = -1;
    desc->client_sock = -1;
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
    
    int err = send(desc->client_sock, data, len, 0);
    if (err < 0) {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}
