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

static int sock = -1;
static int port = 0;
static tcpc_receive_handler_t tcp_client_receive_handler = NULL;

static void close_socket(int sock)
{
    if (sock != -1) {
        ESP_LOGE(TAG, "Shutting down socket and restarting...");
        shutdown(sock, SHUT_RDWR);
        closesocket(sock);
    }
}

static int create_socket()
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

static void receive_data(int sock)
{
    char rx_buffer[RX_BUFFER_SIZE];
    int len = 0;
    while (1) {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "recv failed: errno %d", errno);
            break;
        } else {
            ESP_LOGD(TAG, "Received %d bytes", len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buffer, len, ESP_LOG_DEBUG);
            tcp_client_receive_handler((uint8_t *)rx_buffer, len);
        }
    }
}

static void tcp_client_task(void *pvParameters)
{
    uint32_t ip = *((uint32_t *)pvParameters);

    while (1) {
        sock = create_socket();
        if (sock < 0) {
            continue;
        }

        if (connect_socket(sock, ip, port) != 0) {
            close_socket(sock);
            continue;
        }

        receive_data(sock);
        ESP_LOGI(TAG, "Disconnected from server");
        close_socket(sock);
    }
}

esp_err_t tcp_client_init(uint32_t host_ip, uint16_t host_port,
                          tcpc_receive_handler_t tcpc_receive_handler)
{
    if (tcpc_receive_handler == NULL) {
        ESP_LOGE(TAG, "tcpc_receive_handler is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    port = host_port;
    tcp_client_receive_handler = tcpc_receive_handler;
    xTaskCreate(tcp_client_task, "tcp_client", TCP_CLIENT_TASK_STACK_SIZE, &host_ip,
                TCP_CLIENT_TASK_PRIORITY, NULL);  // TODO: check stack size
    return ESP_OK;
}

esp_err_t tcp_client_send(uint8_t *data, size_t len)
{
    int err = send(sock, data, len, 0);
    if (err < 0) {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}
