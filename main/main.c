#include <string.h>

#include "bridge.h"
#include "config.h"
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
#include "sys_info.h"

static const char *TAG = "main";

// Выводит все настройки (кроме паролей) в лог.
// TODO: В релизе можно удалить
static inline void print_setting_items(void)
{
    int items_num = setting_items_get_keys(NULL);
    const char *keys[items_num];
    setting_items_get_keys(keys);

    for (int i = 0; i < items_num; i++) {
        setting_item_type_t type = setting_items_get_type_in_json(keys[i]);
        switch (type) {
            case SETTING_ITEM_TYPE_NUM: {
                uint32_t value = 0;
                setting_items_read(keys[i], &value);
                ESP_LOGI(TAG, "%s: %lu", keys[i], value);
                break;
            }
            case SETTING_ITEM_TYPE_STR: {
                char value[SETTING_ITEM_MAX_STR_LEN] = {0};
                setting_items_read(keys[i], value);
                ESP_LOGI(TAG, "%s: %s", keys[i], value);
                break;
            }
            case SETTING_ITEM_TYPE_BOOL: {
                uint8_t value = 0;
                setting_items_read(keys[i], &value);
                ESP_LOGI(TAG, "%s: %s", keys[i], value ? "true" : "false");
                break;
            }
            default:
                ESP_LOGW(TAG, "Unknown setting item type for key: %s", keys[i]);
                break;
        }
    }
}

static void eth_connect_event_handler(void *arg, esp_event_base_t event_base,
    int32_t event_id, void *event_data)
{
    switch (event_id) {
        case IP_EVENT_ETH_GOT_IP:
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            snprintf(sys_info.eth_ip, SYS_INFO_MAX_STR_LEN, IPSTR, IP2STR(&event->ip_info.ip));
            snprintf(sys_info.eth_mask, SYS_INFO_MAX_STR_LEN, IPSTR, IP2STR(&event->ip_info.netmask));
            snprintf(sys_info.eth_gw, SYS_INFO_MAX_STR_LEN, IPSTR, IP2STR(&event->ip_info.gw));
            break;
        case ETHERNET_EVENT_CONNECTED:
            uint8_t mac_addr[6] = {0};
            esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
            esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
            snprintf(sys_info.eth_mac, SYS_INFO_MAX_STR_LEN, MACSTR, MAC2STR(mac_addr));
            sys_info.eth_is_connected = true;
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            sys_info.eth_is_connected = false;
            break;
        default:
            break;
    }
}

static void wifi_sta_connect_event_handler(void *arg, esp_event_base_t event_base,
    int32_t event_id, void *event_data)
{
    switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
            const esp_netif_ip_info_t *ip_info = &event->ip_info;
            snprintf(sys_info.wifi_sta_ip, SYS_INFO_MAX_STR_LEN, IPSTR, IP2STR(&ip_info->ip));
            snprintf(sys_info.wifi_sta_mask, SYS_INFO_MAX_STR_LEN, IPSTR, IP2STR(&ip_info->netmask));
            snprintf(sys_info.wifi_sta_gw, SYS_INFO_MAX_STR_LEN, IPSTR, IP2STR(&ip_info->gw));
            break;
        case WIFI_EVENT_STA_CONNECTED:
            sys_info.wifi_sta_is_connected = true;
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            sys_info.wifi_sta_is_connected = false;
            break;
        default:
            break;
    }
}

static void wifi_ap_connect_event_handler(void *arg, esp_event_base_t event_base,
    int32_t event_id, void *event_data)
{
    switch (event_id) {
        case WIFI_EVENT_AP_STACONNECTED:
            sys_info.wifi_ap_connections_count++;
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            sys_info.wifi_ap_connections_count--;
            break;
        default:
            break;
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    setting_item_iface_t setting_item_iface = {
        .has_key = nvs_has_key,
        .save_num = nvs_write_u32,
        .save_str = nvs_write_str,
        .save_bool = nvs_write_u8,
        .read_num = nvs_read_u32,
        .read_str = nvs_read_str,
        .read_bool = nvs_read_u8,
    };
    // генерация уникального hostname
    char generated_hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    snprintf(generated_hostname, SETTING_ITEM_MAX_STR_LEN, "%s-%02X%02X%02X", BASE_HOSTNAME, mac[3],
             mac[4], mac[5]);
    ESP_ERROR_CHECK(setting_items_init(generated_hostname, &setting_item_iface));
    ESP_LOGI(TAG, "Hostname: %s", generated_hostname);

    char hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    // Имя хоста берется из хранилища
    if (setting_items_read_raw(KEY_HOSTNAME, hostname, SETTING_ITEM_TYPE_STR) != 0) {
        ESP_LOGE(TAG, "Failed to read hostname from storage");
    } else {
        ESP_ERROR_CHECK(mdns_init());
        ESP_ERROR_CHECK(mdns_hostname_set(hostname));
        ESP_LOGI(TAG, "mdns hostname set to: [%s]", hostname);
    }

    // apsta = Access Point + Station
    wifi_apsta_config_t apsta_cfg = {0};
    esp_netif_ip_info_t ap_ip_info;
    setting_items_read_raw(KEY_AP_IP_STATIC, &ap_ip_info.ip, SETTING_ITEM_TYPE_NUM);
    setting_items_read_raw(KEY_AP_MASK_STATIC, &ap_ip_info.netmask, SETTING_ITEM_TYPE_NUM);
    setting_items_read_raw(KEY_AP_GW_STATIC, &ap_ip_info.gw, SETTING_ITEM_TYPE_NUM);
    apsta_cfg.ap_ip_info = &ap_ip_info;
    setting_items_read_raw(KEY_AP_SSID, &apsta_cfg.ap_ssid, SETTING_ITEM_TYPE_STR);
    setting_items_read_raw(KEY_AP_PASS, &apsta_cfg.ap_pass, SETTING_ITEM_TYPE_STR);
    setting_items_read_raw(KEY_STA_SSID, &apsta_cfg.sta_ssid, SETTING_ITEM_TYPE_STR);
    setting_items_read_raw(KEY_STA_PASS, &apsta_cfg.sta_pass, SETTING_ITEM_TYPE_STR);
    setting_items_read_raw(KEY_WIFI_MODE, &apsta_cfg.wifi_mode, SETTING_ITEM_TYPE_NUM);
    apsta_cfg.sta_event_handler = &wifi_sta_connect_event_handler;
    apsta_cfg.ap_event_handler = &wifi_ap_connect_event_handler;
    ESP_ERROR_CHECK(wifi_init_apsta(&apsta_cfg));

    bool eth_dhcpc = false;
    esp_netif_ip_info_t *eth_ip_info = NULL;
    esp_netif_ip_info_t static_ip_info = {0};
    setting_items_read_raw(KEY_ETH_DHCPC, &eth_dhcpc, SETTING_ITEM_TYPE_BOOL);
    setting_items_read_raw(KEY_ETH_IP_STATIC, &static_ip_info.ip, SETTING_ITEM_TYPE_NUM);
    setting_items_read_raw(KEY_ETH_MASK_STATIC, &static_ip_info.netmask, SETTING_ITEM_TYPE_NUM);
    setting_items_read_raw(KEY_ETH_GW_STATIC, &static_ip_info.gw, SETTING_ITEM_TYPE_NUM);

    if (!eth_dhcpc) {
        eth_ip_info = &static_ip_info;
    }
    ESP_ERROR_CHECK(ethernet_init(&eth_connect_event_handler, eth_ip_info));

    ssdp_config_t ssdp_config = NULL;  // TODO: Add SSDP
    ESP_ERROR_CHECK(http_server_init(&ssdp_config));

    sys_info_init();
    print_setting_items();

    while (1)
    {
        if ((sys_info.wifi_ap_connections_count > 0) ||
            sys_info.eth_is_connected ||
            sys_info.wifi_sta_is_connected)
        {
            ESP_ERROR_CHECK(bridge_init());
            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGW(TAG, "Waiting for network connection");
        }
    }
}
