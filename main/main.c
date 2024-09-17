#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
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

#define BASE_HOSTNAME "WB-MGE"

static const char *TAG = "main";

static void eth_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
            ESP_LOGI(TAG, "Ethernet Link Up");
            ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0],
                     mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
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
    // генерация уникального hostname
    char default_hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_ETH);
    snprintf(default_hostname, SETTING_ITEM_MAX_STR_LEN, "%s-%02X%02X%02X", BASE_HOSTNAME, mac[3], mac[4], mac[5]);
    ESP_ERROR_CHECK(setting_items_init(default_hostname, &setting_item_iface));

    char hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    // Если нет ячейки с именем хоста, то устанавливаем дефолтные значения для всех ячеек
    if (setting_items_read_raw("hostname", hostname, SETTING_ITEM_TYPE_STR) != 0) {
        ESP_ERROR_CHECK(setting_items_set_defaults());
    } else {
        ESP_LOGI(TAG, "hostname: %s", hostname);
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", hostname);

    // apsta = Access Point + Station
    wifi_apsta_config_t apsta_cfg = {0};
    esp_netif_ip_info_t ap_ip_info;
    setting_items_read_raw("ap_ip_static", &ap_ip_info.ip, SETTING_ITEM_TYPE_NUM);
    setting_items_read_raw("ap_mask_static", &ap_ip_info.netmask, SETTING_ITEM_TYPE_NUM);
    setting_items_read_raw("ap_gw_static", &ap_ip_info.gw, SETTING_ITEM_TYPE_NUM);
    apsta_cfg.ap_ip_info = &ap_ip_info;
    setting_items_read_raw("ap_ssid", &apsta_cfg.ap_ssid, SETTING_ITEM_TYPE_STR);
    setting_items_read_raw("ap_pass", &apsta_cfg.ap_pass, SETTING_ITEM_TYPE_STR);
    setting_items_read_raw("sta_ssid", &apsta_cfg.sta_ssid, SETTING_ITEM_TYPE_STR);
    setting_items_read_raw("sta_pass", &apsta_cfg.sta_pass, SETTING_ITEM_TYPE_STR);
    setting_items_read_raw("wifi_mode", &apsta_cfg.wifi_mode, SETTING_ITEM_TYPE_NUM);
    ESP_ERROR_CHECK(wifi_init_apsta(&apsta_cfg));

    bool eth_dhcpc = false;
    esp_netif_ip_info_t *eth_ip_info = NULL;
    esp_netif_ip_info_t static_ip_info = {0};
    setting_items_read_raw("eth_dhcpc", &eth_dhcpc, SETTING_ITEM_TYPE_BOOL);
    setting_items_read_raw("eth_ip_static", &static_ip_info.ip, SETTING_ITEM_TYPE_NUM);
    setting_items_read_raw("eth_mask_static", &static_ip_info.netmask, SETTING_ITEM_TYPE_NUM);
    setting_items_read_raw("eth_gw_static", &static_ip_info.gw, SETTING_ITEM_TYPE_NUM);

    if (!eth_dhcpc) {
        eth_ip_info = &static_ip_info;
    }
    ESP_ERROR_CHECK(ethernet_init(&eth_event, NULL, eth_ip_info));

    ssdp_config_t ssdp_config = NULL;
    ESP_ERROR_CHECK(http_server_init(&ssdp_config));
}
