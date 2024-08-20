#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_apsta.h"
#include "tcp_server.h"
#include "tcp_client.h"
#include "http_server.h"
#include "serial.h"
#include "nv_storage.h"

static const char *TAG = "main";

void tcps_receive_handler(uint8_t *data, uint8_t len)
{
    ESP_LOGI(TAG, "TCP received %d bytes", len);
    ESP_LOG_BUFFER_HEX(TAG, data, len);
    serial_send(data, len);
}

void tcpc_receive_handler(uint8_t *data, uint8_t len)
{
    ESP_LOGI(TAG, "TCP received %d bytes", len);
    ESP_LOG_BUFFER_HEX(TAG, data, len);
    tcp_client_send(data, len);
}

void serial_receive_handler(uint8_t *data, uint8_t len)
{
    ESP_LOGI(TAG, "Serial received %d bytes", len);
    ESP_LOG_BUFFER_HEX(TAG, data, len);
    tcp_server_send(data, len);
}

void app_main(void)
{
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(serial_init(&uart_config, serial_receive_handler));
    ESP_ERROR_CHECK(nvs_init());
    ESP_ERROR_CHECK(wifi_init_apsta("WB-MGE", "12345678", "TP-LINK", "paroltplink"));
    ESP_ERROR_CHECK(tcp_server_init(3333, tcps_receive_handler));
    // vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(tcp_client_init("192.168.55.106", 1234, tcpc_receive_handler));

    ssdp_config_t ssdp_config = SDDP_DEFAULT_CONFIG();
    ssdp_config.model_url = "https://wirenboard.com/ru/product/WB-MGE";
    ssdp_config.friendly_name = "WB-MGE";
    ssdp_config.model_name = "WB-MGE v.3";
    ESP_ERROR_CHECK(http_server_init(&ssdp_config));
}
