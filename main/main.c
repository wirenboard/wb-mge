#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "ethernet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_server.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include "nv_storage.h"
#include "serial.h"
#include "tcp_client.h"
#include "tcp_server.h"
#include "wifi_apsta.h"

#define MDNS_HOSTNAME "wb-mge"

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

/** Event handler for Ethernet events */
static void eth_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
            ESP_LOGI(TAG, "Ethernet Link Up");
            ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",  //
                     mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Ethernet Link Down");
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet Started");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet Stopped");
            break;
        default:
            break;
    }
}

void app_main(void)
{
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(serial_init(&uart_config, serial_receive_handler));
    ESP_ERROR_CHECK(nvs_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(MDNS_HOSTNAME));
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", MDNS_HOSTNAME);

    esp_netif_ip_info_t ap_ip_info;
    ap_ip_info.ip.addr = ipaddr_addr("192.168.33.33");
    ap_ip_info.netmask.addr = ipaddr_addr("255.255.255.0");
    ap_ip_info.gw.addr = ipaddr_addr("192.168.33.1");
    wifi_apsta_config_t apsta_cfg = {
        .ap_ssid = "WB-MGE",
        .ap_pass = "12345678",
        .ap_ip_info = &ap_ip_info,
        .sta_ssid = "TP-LINK",
        .sta_pass = "paroltplink",
        .wifi_mode = WIFI_MODE_APSTA,
    };
    ESP_ERROR_CHECK(wifi_init_apsta(&apsta_cfg));

    esp_netif_ip_info_t static_ip;
    static_ip.ip.addr = ipaddr_addr("192.168.33.33");
    static_ip.netmask.addr = ipaddr_addr("255.255.255.0");
    static_ip.gw.addr = ipaddr_addr("192.168.33.1");
    ESP_ERROR_CHECK(ethernet_init(&eth_event, NULL, NULL));

    ESP_ERROR_CHECK(tcp_server_init(3333, tcps_receive_handler));
    // vTaskDelay(5000 / portTICK_PERIOD_MS);
    // ESP_ERROR_CHECK(tcp_client_init("192.168.55.106", 1234, tcpc_receive_handler));

    ssdp_config_t ssdp_config = SDDP_DEFAULT_CONFIG();
    ssdp_config.model_url = "https://wirenboard.com/ru/product/WB-MGE";
    ssdp_config.friendly_name = "WB-MGE";
    ssdp_config.model_name = "WB-MGE v.3";
    ESP_ERROR_CHECK(http_server_init(&ssdp_config));
}
