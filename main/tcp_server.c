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

static const char *TAG = "tcp-server";

static void tcp_server_task(void *pvParameters)
{
    tcp_desc_t *desc = (tcp_desc_t *)pvParameters;
    char addr_str[128];
    char rx_buffer[RX_BUFFER_SIZE];
    int len;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;

    while (1) {
        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        desc->client_sock = accept(desc->listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        
        if (desc->client_sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        desc->active_connections++;
        
        // Set tcp keepalive option
        setsockopt(desc->client_sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(desc->client_sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(desc->client_sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(desc->client_sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
        
        ESP_LOGD(TAG, "Socket accepted ip address: %s", addr_str);

        do {
            len = recv(desc->client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        
            if (len < 0) {
                ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
            } else if (len == 0) {
                ESP_LOGW(TAG, "Connection closed");
            } else {
                ESP_LOGD(TAG, "Received %d bytes", len);
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buffer, len, ESP_LOG_DEBUG);
                desc->receive_handler(desc, (uint8_t *)rx_buffer, len);
            }
        } while (len > 0);

        shutdown(desc->client_sock, 0);
        close(desc->client_sock);

        desc->active_connections--; // Decrement per-server
    }

    vTaskDelete(NULL);
}

esp_err_t tcp_server_init(int port, tcp_receive_handler_t tcps_receive_handler, tcp_desc_t **out_desc)
{
    if (tcps_receive_handler == NULL) {
        ESP_LOGE(TAG, "tcps_receive_handler is NULL");
        return ESP_ERR_INVALID_ARG;
    }

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
        return ESP_FAIL;
    }
    
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ESP_LOGD(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        close(listen_sock);
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "Socket bound, port %d", port);

    err = listen(listen_sock, 1);

    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(listen_sock);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Socket listening");

    tcp_desc_t *desc = calloc(1, sizeof(tcp_desc_t));

    if (!desc) {
        close(listen_sock);
        return ESP_ERR_NO_MEM;
    }

    desc->listen_sock = listen_sock;
    desc->client_sock = -1;
    desc->receive_handler = tcps_receive_handler;
    desc->active_connections = 0; // Initialize per-server counter
    *out_desc = desc;

    xTaskCreate(tcp_server_task, "tcp_server", TCP_SERVER_TASK_STACK_SIZE, desc,
                TCP_SERVER_TASK_PRIORITY, NULL);
    return ESP_OK;
}

esp_err_t tcp_server_send(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    if (!desc || (desc->client_sock < 0)) {
        ESP_LOGE(TAG, "No client connected");
        return ESP_FAIL;
    }

    int to_write = len;

    while (to_write > 0) {
        int written = send(desc->client_sock, data + (len - to_write), to_write, 0);
        
        if (written < 0) {
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            return ESP_FAIL;
        }
        
        to_write -= written;
    }

    return ESP_OK;
}
