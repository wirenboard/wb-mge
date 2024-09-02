#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "ethernet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_server.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include "nv_storage.h"
#include "serial.h"
#include "setting_items.h"
#include "tcp_client.h"
#include "tcp_server.h"
#include "wifi_apsta.h"

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

static void eth_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
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
    ESP_ERROR_CHECK(nvs_init());
    setting_item_iface_t setting_item_iface = {
        .save_num = nvs_write_u32,
        .save_str = nvs_write_str,
        .save_bool = nvs_write_u8,
        .read_num = nvs_read_u32,
        .read_str = nvs_read_str,
        .read_bool = nvs_read_u8,
    };
    setting_items_init(&setting_item_iface);

    char hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    // Если нет ячейки с именем хоста, то устанавливаем дефолтные значения
    if (setting_items_read_raw("hostname", hostname, SETTING_ITEM_TYPE_STR) != 0) {
        ESP_ERROR_CHECK(setting_items_set_defaults());
    } else {
        ESP_LOGI(TAG, "hostname: %s", hostname);
    }

    serial_config_t serial_config = {0};
    setting_items_read_raw("baudrate", &serial_config.baudrate, SETTING_ITEM_TYPE_NUM);
    setting_items_read_raw("databits", &serial_config.databits, SETTING_ITEM_TYPE_NUM);
    setting_items_read_raw("parity", &serial_config.parity, SETTING_ITEM_TYPE_NUM);
    setting_items_read_raw("stopbits", &serial_config.stopbits, SETTING_ITEM_TYPE_NUM);

    ESP_ERROR_CHECK(serial_init(&serial_config, serial_receive_handler));

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", hostname);

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

    // esp_netif_ip_info_t static_ip;
    // static_ip.ip.addr = ipaddr_addr("192.168.33.33");
    // static_ip.netmask.addr = ipaddr_addr("255.255.255.0");
    // static_ip.gw.addr = ipaddr_addr("192.168.33.1");
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
