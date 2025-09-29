#include "setting_items.h"
#include "ethernet.h"
#include "wifi_apsta.h"
#include "mdns.h"
#include "sys_info.h"
#include "lwip/ip4_addr.h"
#include "esp_mac.h"
#include "sys/socket.h"
#include "esp_log.h"


#define NETWORK_DEBUG_LOG_ENABLE        1       // TODO: Возможно, вынести в настройки


typedef struct {
    bool dhcp_client;
    esp_netif_ip_info_t static_ip;
} ethernet_settings_t;

typedef struct {
    wifi_mode_t wifi_mode;
    char ap_ssid[WIFI_SSID_MAX_LEN];
    char ap_pass[WIFI_PASS_MAX_LEN];
    wifi_auth_mode_t ap_auth_mode;
    esp_netif_ip_info_t ap_static_ip;
    char sta_ssid[WIFI_SSID_MAX_LEN];
    char sta_pass[WIFI_PASS_MAX_LEN];
    wifi_auth_mode_t sta_auth_mode;
    bool sta_dhcp_client;
    esp_netif_ip_info_t sta_static_ip;
} wifi_settings_t;

typedef struct {
    char hostname[SETTING_ITEM_MAX_STR_LEN];
    ethernet_settings_t eth_settings;
    wifi_settings_t wifi_settings;
} network_settings_t;


static network_settings_t current_settings = {0};

static const char* TAG = "network";


// Helper function to convert string IP to uint32_t
static uint32_t str_to_ip(const char *ip_str) {
    uint32_t ip = 0;
    if (ip_str && strnlen(ip_str, SETTING_ITEM_MAX_STR_LEN) > 0) {
        inet_pton(AF_INET, ip_str, &ip);
    }
    return ip;
}


static void ip_to_str(uint32_t ip, char* out_ip_str)
{
    uint8_t* ip_bytes = (uint8_t*)&ip;
    sprintf(out_ip_str, "%d.%d.%d.%d", ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3]);
}


static wifi_mode_t str_to_wifi_mode(const char *str)
{
    if (strcmp(str, WIFI_MODE_AP_STR) == 0) {
        return WIFI_MODE_AP;
    } else if (strcmp(str, WIFI_MODE_STA_STR) == 0) {
        return WIFI_MODE_STA;
    } else if (strcmp(str, WIFI_MODE_APSTA_STR) == 0) {
        return WIFI_MODE_APSTA;
    } else if (strcmp(str, WIFI_MODE_NONE_STR) == 0) {
        return WIFI_MODE_NULL;
    } else {
        ESP_LOGW(TAG, "Unknown WiFi mode '%s', using NONE mode", str);
        return WIFI_MODE_NULL;
    }
}


static const char* wifi_mode_to_str(wifi_mode_t mode)
{
    switch (mode) {
        case WIFI_MODE_AP:
            return WIFI_MODE_AP_STR;
        case WIFI_MODE_STA:
            return WIFI_MODE_STA_STR;
        case WIFI_MODE_APSTA:
            return WIFI_MODE_APSTA_STR;
        case WIFI_MODE_NULL:
        default:
            return WIFI_MODE_NONE_STR;
    }
}


static wifi_auth_mode_t str_to_wifi_auth_mode(const char *str) {
    if (strcmp(str, WIFI_AUTH_WPA2_PSK_STR) == 0) {
        return WIFI_AUTH_WPA2_PSK;
    } else if (strcmp(str, WIFI_AUTH_WPA3_PSK_STR) == 0) {
        return WIFI_AUTH_WPA3_PSK;
    } else if (strcmp(str, WIFI_AUTH_OPEN_STR) == 0) {
        return WIFI_AUTH_OPEN;
    } else {
        ESP_LOGW(TAG, "Unknown WiFi auth type '%s', using OPEN auth type", str);
        return WIFI_AUTH_OPEN;
    }
}


static esp_err_t read_ethernet_settings(ethernet_settings_t* eth_settings)
{
    char temp_value[SETTING_ITEM_MAX_STR_LEN] = {0};
    memset(eth_settings, 0, sizeof(*eth_settings));
    esp_err_t result = ESP_OK;

    // Read DHCP configuration
    eth_settings->dhcp_client = setting_items_read_bool(KEY_ETH_DHCPC);

    // Read static IP configuration
    if (setting_items_read(KEY_ETH_IP_STATIC, temp_value) == ESP_OK) {
        eth_settings->static_ip.ip.addr = str_to_ip(temp_value);
    } else {
        result = ESP_FAIL;
    }
    if (setting_items_read(KEY_ETH_MASK_STATIC, temp_value) == ESP_OK) {
        eth_settings->static_ip.netmask.addr = str_to_ip(temp_value);
    } else {
        result = ESP_FAIL;
    }
    if (setting_items_read(KEY_ETH_GW_STATIC, temp_value) == ESP_OK) {
        eth_settings->static_ip.gw.addr = str_to_ip(temp_value);
    } else {
        result = ESP_FAIL;
    }

    return result;
}


static esp_err_t read_wifi_settings(wifi_settings_t* wifi_settings)
{
    char temp_value[SETTING_ITEM_MAX_STR_LEN] = {0};
    memset(wifi_settings, 0, sizeof(*wifi_settings));
    esp_err_t result = ESP_OK;

    // Read WiFi mode
    if (setting_items_read(KEY_WIFI_MODE, temp_value) == ESP_OK) {
        wifi_settings->wifi_mode = str_to_wifi_mode(temp_value);
    } else {
        wifi_settings->wifi_mode = WIFI_MODE_NULL;
        result = ESP_FAIL;
    }

    // Read AP credentials
    if (setting_items_read(KEY_AP_SSID, temp_value) == ESP_OK) {
        strncpy(wifi_settings->ap_ssid, temp_value, sizeof(wifi_settings->ap_ssid) - 1);
    } else {
        result = ESP_FAIL;
    }
    if (setting_items_read(KEY_AP_PASS, temp_value) == ESP_OK) {
        strncpy(wifi_settings->ap_pass, temp_value, sizeof(wifi_settings->ap_pass) - 1);
    } else {
        result = ESP_FAIL;
    }

    // Read AP auth mode
    if (setting_items_read(KEY_WIFI_AUTH_AP, temp_value) == ESP_OK) {
        wifi_settings->ap_auth_mode = str_to_wifi_auth_mode(temp_value);
    } else {
        wifi_settings->ap_auth_mode = WIFI_AUTH_OPEN;
        result = ESP_FAIL;
    }

    // Read AP IP configuration
    if (setting_items_read(KEY_AP_IP_STATIC, temp_value) == ESP_OK) {
        wifi_settings->ap_static_ip.ip.addr = str_to_ip(temp_value);
    } else {
        result = ESP_FAIL;
    }
    if (setting_items_read(KEY_AP_MASK_STATIC, temp_value) == ESP_OK) {
        wifi_settings->ap_static_ip.netmask.addr = str_to_ip(temp_value);
    } else {
        result = ESP_FAIL;
    }
    if (setting_items_read(KEY_AP_GW_STATIC, temp_value) == ESP_OK) {
        wifi_settings->ap_static_ip.gw.addr = str_to_ip(temp_value);
    } else {
        result = ESP_FAIL;
    }

    // Read WiFi STA credentials
    if (setting_items_read(KEY_STA_SSID, temp_value) == ESP_OK) {
        strncpy(wifi_settings->sta_ssid, temp_value, sizeof(wifi_settings->sta_ssid) - 1);
    } else {
        result = ESP_FAIL;
    }
    if (setting_items_read(KEY_STA_PASS, temp_value) == ESP_OK) {
        strncpy(wifi_settings->sta_pass, temp_value, sizeof(wifi_settings->sta_pass) - 1);
    } else {
        result = ESP_FAIL;
    }

    // Read STA auth mode
    if (setting_items_read(KEY_WIFI_AUTH_STA, temp_value) == ESP_OK) {
        wifi_settings->sta_auth_mode = str_to_wifi_auth_mode(temp_value);
    } else {
        wifi_settings->sta_auth_mode = WIFI_AUTH_OPEN;
        result = ESP_FAIL;
    }

    // Read STA DHCP client configuration
    wifi_settings->sta_dhcp_client = setting_items_read_bool(KEY_STA_DHCPC);

    // Read STA static IP configuration
    if (setting_items_read(KEY_STA_IP_STATIC, temp_value) == ESP_OK) {
        wifi_settings->sta_static_ip.ip.addr = str_to_ip(temp_value);
    } else {
        result = ESP_FAIL;
    }
    if (setting_items_read(KEY_STA_MASK_STATIC, temp_value) == ESP_OK) {
        wifi_settings->sta_static_ip.netmask.addr = str_to_ip(temp_value);
    } else {
        result = ESP_FAIL;
    }
    if (setting_items_read(KEY_STA_GW_STATIC, temp_value) == ESP_OK) {
        wifi_settings->sta_static_ip.gw.addr = str_to_ip(temp_value);
    } else {
        result = ESP_FAIL;
    }

    return result;
}


static inline esp_err_t read_hostname(char hostname[SETTING_ITEM_MAX_STR_LEN])
{
    return setting_items_read(KEY_HOSTNAME, hostname);
}


static void update_sys_info_wifi_state(wifi_settings_t* wifi_settings)
{
    if ((wifi_settings->wifi_mode == WIFI_MODE_AP) || (wifi_settings->wifi_mode == WIFI_MODE_APSTA)) {
        ip_to_str(wifi_settings->ap_static_ip.ip.addr, sys_info.wifi_ap_ip);
        ip_to_str(wifi_settings->ap_static_ip.netmask.addr, sys_info.wifi_ap_mask);
        ip_to_str(wifi_settings->ap_static_ip.gw.addr, sys_info.wifi_ap_gw);
    } else {
        memset(sys_info.wifi_ap_ip, 0, sizeof(sys_info.wifi_ap_ip));
        memset(sys_info.wifi_ap_mask, 0, sizeof(sys_info.wifi_ap_mask));
        memset(sys_info.wifi_ap_gw, 0, sizeof(sys_info.wifi_ap_gw));
    }

    strncpy(sys_info.wifi_sta_con_ssid, wifi_settings->sta_ssid, sizeof(sys_info.wifi_sta_con_ssid) - 1);
    sys_info.wifi_sta_con_ssid[sizeof(sys_info.wifi_sta_con_ssid) - 1] = 0;

    snprintf(sys_info.wifi_mode, sizeof(sys_info.wifi_mode), "%s", wifi_mode_to_str(wifi_settings->wifi_mode));
    sys_info.wifi_mode[sizeof(sys_info.wifi_mode) - 1] = 0;

    sys_info.wifi_enabled = (wifi_settings->wifi_mode != WIFI_MODE_NULL);
}


static void update_sys_info_eth_mac(void)
{
    esp_eth_handle_t eth_handle = ethernet_get_handle();
    if (eth_handle != NULL) {
        uint8_t eth_mac[6] = {0};
        esp_err_t ret = esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, eth_mac);
        if (ret == ESP_OK) {
            ret = snprintf(sys_info.eth_mac, SYS_INFO_MAX_STR_LEN, MACSTR, MAC2STR(eth_mac));
            if (ret >= SYS_INFO_MAX_STR_LEN) {
                ESP_LOGW(TAG, "Ethernet MAC address string was truncated");
            }
            ESP_LOGI(TAG, "Ethernet MAC: " MACSTR, MAC2STR(eth_mac));
        } else {
            ESP_LOGW(TAG, "Failed to get Ethernet MAC address: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "Ethernet handle is NULL, cannot get MAC address");
    }
}


static void update_sys_info_wifi_mac(void)
{
    uint8_t wifi_sta_mac[6] = {0};
    uint8_t wifi_ap_mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, wifi_sta_mac);
    esp_wifi_get_mac(WIFI_IF_AP, wifi_ap_mac);
    ESP_LOGI(TAG, "WiFi STA MAC: " MACSTR, MAC2STR(wifi_sta_mac));
    ESP_LOGI(TAG, "WiFi AP MAC:  " MACSTR, MAC2STR(wifi_ap_mac));
    int ret1 = snprintf(sys_info.wifi_sta_mac, SYS_INFO_MAX_STR_LEN, MACSTR, MAC2STR(wifi_sta_mac));
    int ret2 = snprintf(sys_info.wifi_ap_mac, SYS_INFO_MAX_STR_LEN, MACSTR, MAC2STR(wifi_ap_mac));
    if ((ret1 >= SYS_INFO_MAX_STR_LEN) || (ret2 >= SYS_INFO_MAX_STR_LEN)) {
        ESP_LOGW(TAG, "WiFi MAC address string was truncated");
    }
}


static void update_sys_info_eth_ip(esp_netif_ip_info_t* ip_info)
{
    if (ip_info != NULL) {
        snprintf(sys_info.eth_ip, SYS_INFO_MAX_STR_LEN, IPSTR, IP2STR(&ip_info->ip));
        snprintf(sys_info.eth_mask, SYS_INFO_MAX_STR_LEN, IPSTR, IP2STR(&ip_info->netmask));
        snprintf(sys_info.eth_gw, SYS_INFO_MAX_STR_LEN, IPSTR, IP2STR(&ip_info->gw));
    } else {
        memset(sys_info.eth_ip, 0, sizeof(sys_info.eth_ip));
        memset(sys_info.eth_mask, 0, sizeof(sys_info.eth_mask));
        memset(sys_info.eth_gw, 0, sizeof(sys_info.eth_gw));
    }
}


static void update_sys_info_wifi_sta_ip(esp_netif_ip_info_t* ip_info)
{
    if (ip_info != NULL) {
        snprintf(sys_info.wifi_sta_ip, SYS_INFO_MAX_STR_LEN, IPSTR, IP2STR(&ip_info->ip));
        snprintf(sys_info.wifi_sta_mask, SYS_INFO_MAX_STR_LEN, IPSTR, IP2STR(&ip_info->netmask));
        snprintf(sys_info.wifi_sta_gw, SYS_INFO_MAX_STR_LEN, IPSTR, IP2STR(&ip_info->gw));
    } else {
        memset(sys_info.wifi_sta_ip, 0, sizeof(sys_info.wifi_sta_ip));
        memset(sys_info.wifi_sta_mask, 0, sizeof(sys_info.wifi_sta_mask));
        memset(sys_info.wifi_sta_gw, 0, sizeof(sys_info.wifi_sta_gw));
    }
}


static inline void update_sys_info_eth_conn(bool connected)
{
    sys_info.eth_is_connected = connected;
}


static inline void update_sys_info_wifi_sta_conn(bool connected)
{
    sys_info.wifi_sta_is_connected = connected;
}


static inline void update_sys_info_wifi_ap_conn_count(bool inc_ndec)
{
    if (inc_ndec) {
        sys_info.wifi_ap_connections_count++;
    } else {
        sys_info.wifi_ap_connections_count--;
    }
}


static void eth_connect_event_handler(void *arg, esp_event_base_t event_base,
    int32_t event_id, void *event_data)
{
    switch (event_id) {
        case IP_EVENT_ETH_GOT_IP:
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            update_sys_info_eth_ip(&event->ip_info);
            break;
        case ETHERNET_EVENT_CONNECTED:
            update_sys_info_eth_conn(true);
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            update_sys_info_eth_conn(false);
            update_sys_info_eth_ip(NULL);
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
            update_sys_info_wifi_sta_ip(&event->ip_info);
            break;
        case WIFI_EVENT_STA_CONNECTED:
            update_sys_info_wifi_sta_conn(true);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            update_sys_info_wifi_sta_conn(false);
            update_sys_info_wifi_sta_ip(NULL);
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
            update_sys_info_wifi_ap_conn_count(true);
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            update_sys_info_wifi_ap_conn_count(false);
            break;
        default:
            break;
    }
}


static void make_wifi_apsta_cfg(wifi_settings_t* wifi_settings, wifi_apsta_config_t* apsta_cfg)
{
    apsta_cfg->wifi_mode = wifi_settings->wifi_mode;

    apsta_cfg->ap_ip_info = &wifi_settings->ap_static_ip;
    if (!wifi_settings->sta_dhcp_client) {
        apsta_cfg->sta_ip_info = &wifi_settings->sta_static_ip;
    } else {
        apsta_cfg->sta_ip_info = NULL;
    }

    strncpy(apsta_cfg->ap_ssid, wifi_settings->ap_ssid, sizeof(apsta_cfg->ap_ssid) - 1);
    apsta_cfg->ap_ssid[sizeof(apsta_cfg->ap_ssid) - 1] = '\0';

    strncpy(apsta_cfg->ap_pass, wifi_settings->ap_pass, sizeof(apsta_cfg->ap_pass) - 1);
    apsta_cfg->ap_pass[sizeof(apsta_cfg->ap_pass) - 1] = '\0';

    strncpy(apsta_cfg->sta_ssid, wifi_settings->sta_ssid, sizeof(apsta_cfg->sta_ssid) - 1);
    apsta_cfg->sta_ssid[sizeof(apsta_cfg->sta_ssid) - 1] = '\0';

    strncpy(apsta_cfg->sta_pass, wifi_settings->sta_pass, sizeof(apsta_cfg->sta_pass) - 1);
    apsta_cfg->sta_pass[sizeof(apsta_cfg->sta_pass) - 1] = '\0';

    apsta_cfg->wifi_auth_mode_ap = wifi_settings->ap_auth_mode;
    apsta_cfg->wifi_auth_mode_sta = wifi_settings->sta_auth_mode;

    apsta_cfg->sta_event_handler = &wifi_sta_connect_event_handler;
    apsta_cfg->ap_event_handler = &wifi_ap_connect_event_handler;
}


static esp_err_t init_wifi(wifi_settings_t* wifi_settings, char* hostname)
{
    wifi_apsta_config_t apsta_cfg = {0};
    make_wifi_apsta_cfg(wifi_settings, &apsta_cfg);

    #if QEMU_BUILD
        ESP_LOGI(TAG, "Initializing WiFi mock for QEMU");
        esp_err_t result = wifi_init_apsta_qemu(&apsta_cfg, hostname);
    #else
        ESP_LOGI(TAG, "Initializing WiFi for hardware");
        esp_err_t result = wifi_init_apsta(&apsta_cfg, hostname);
    #endif

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "WiFi initialized");
    } else {
        ESP_LOGE(TAG, "Failed to initialize WiFi");
    }

    return result;
}


static esp_err_t init_ethernet(ethernet_settings_t* eth_settings, char* hostname)
{
    esp_netif_ip_info_t* eth_ip_info = NULL;
    if (!eth_settings->dhcp_client) {
        eth_ip_info = &eth_settings->static_ip;
    }

    esp_err_t result = ethernet_init(&eth_connect_event_handler, eth_ip_info, hostname);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Ethernet initialized");
    } else {
        ESP_LOGE(TAG, "Failed to initialize Ethernet");
    }

    return result;
}


esp_err_t network_init(void)
{
    if (NETWORK_DEBUG_LOG_ENABLE) {
        esp_log_level_set(TAG, ESP_LOG_DEBUG);
    }

    if (esp_event_loop_create_default() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize default event loop");
        return ESP_FAIL;
    }

    if (read_hostname(current_settings.hostname) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read hostname, using default value");
        strncpy(current_settings.hostname, BASE_HOSTNAME, sizeof(current_settings.hostname) - 1);
        current_settings.hostname[sizeof(current_settings.hostname) - 1] = '\0';
    }

    if (mdns_init() == ESP_OK) {
        if (mdns_hostname_set(current_settings.hostname) == ESP_OK) {
            ESP_LOGI(TAG, "mDNS hostname set to: %s", current_settings.hostname);
        } else {
            ESP_LOGE(TAG, "Unable to set mDNS hostname");
        }
    } else {
        ESP_LOGE(TAG, "Failed to initialize mDNS");
        // Not fatal error
    }

    if (esp_netif_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP network interface");
        return ESP_FAIL;
    }

    if (read_ethernet_settings(&current_settings.eth_settings) == ESP_OK) {
        init_ethernet(&current_settings.eth_settings, current_settings.hostname);
        update_sys_info_eth_mac();
    } else {
        ESP_LOGE(TAG, "Unable to read Ethernet settings");
    }

    if (read_wifi_settings(&current_settings.wifi_settings) == ESP_OK) {
        init_wifi(&current_settings.wifi_settings, current_settings.hostname);
        update_sys_info_wifi_mac();
        update_sys_info_wifi_state(&current_settings.wifi_settings);
    } else {
        ESP_LOGE(TAG, "Unable to read WiFi settings");
    }

    return ESP_OK;
}


bool network_check_eth_settings_changed(void)
{
    char hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    esp_err_t ret = read_hostname(hostname);
    if (ret == ESP_OK) {
        if (strncmp(current_settings.hostname, hostname, SETTING_ITEM_MAX_STR_LEN - 1) != 0) {
            return true;
        }
    }

    ethernet_settings_t eth_settings;
    ret = read_ethernet_settings(&eth_settings);
    if (ret != ESP_OK) {
        return false;
    }

    if (current_settings.eth_settings.dhcp_client != eth_settings.dhcp_client) {
        return true;
    }

    if (!current_settings.eth_settings.dhcp_client) {
        if (memcmp(&current_settings.eth_settings.static_ip, &eth_settings.static_ip, sizeof(eth_settings.static_ip)) != 0) {
            return true;
        }
    }

    return false;
}


esp_err_t network_update_eth_settings(void)
{
    ESP_LOGD(TAG, "Updating Ethernet settings...");

    char hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    esp_err_t ret = read_hostname(hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read hostname");
        return ret;
    }

    ethernet_settings_t eth_settings;
    ret = read_ethernet_settings(&eth_settings);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read Ethernet settings");
        return ret;
    }

    memcpy(current_settings.hostname, hostname, sizeof(current_settings.hostname));
    memcpy(&current_settings.eth_settings, &eth_settings, sizeof(current_settings.eth_settings));

    esp_netif_ip_info_t* static_ip = NULL;
    if (!eth_settings.dhcp_client) {
        static_ip = &eth_settings.static_ip;
    }
    ret = ethernet_set_ip_hostname(static_ip, hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update Ethernet settings");
        return ret;
    }

    ESP_LOGD(TAG, "Ethernet settings updated");
    return ESP_OK;
}


bool network_check_wifi_settings_changed(void)
{
    wifi_settings_t new_settings;
    esp_err_t ret = read_wifi_settings(&new_settings);
    if (ret != ESP_OK) {
        return false;
    }

    wifi_settings_t* curr_settings = &current_settings.wifi_settings;
    if (curr_settings->wifi_mode != new_settings.wifi_mode) {
        return true;
    }

    char hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    ret = read_hostname(hostname);
    if (ret == ESP_OK) {
        if ((strncmp(current_settings.hostname, hostname, SETTING_ITEM_MAX_STR_LEN - 1) != 0) &&
            (new_settings.wifi_mode != WIFI_MODE_NULL))
        {
            return true;
        }
    }

    bool ap_enabled = (new_settings.wifi_mode == WIFI_MODE_AP) || (new_settings.wifi_mode == WIFI_MODE_APSTA);
    if (ap_enabled) {
        bool changed = (curr_settings->ap_auth_mode != new_settings.ap_auth_mode);
        changed = changed || (strncmp(curr_settings->ap_ssid, new_settings.ap_ssid, sizeof(new_settings.ap_ssid) - 1) != 0);
        changed = changed || (strncmp(curr_settings->ap_pass, new_settings.ap_pass, sizeof(new_settings.ap_pass) - 1) != 0);
        changed = changed || (memcmp(&curr_settings->ap_static_ip, &new_settings.ap_static_ip, sizeof(new_settings.ap_static_ip)) != 0);
        if (changed) {
            return true;
        }
    }

    bool sta_enabled = (new_settings.wifi_mode == WIFI_MODE_STA) || (new_settings.wifi_mode == WIFI_MODE_APSTA);
    if (sta_enabled) {
        bool changed = (curr_settings->sta_auth_mode != new_settings.sta_auth_mode);
        changed = changed || (strncmp(curr_settings->sta_ssid, new_settings.sta_ssid, sizeof(new_settings.sta_ssid) - 1) != 0);
        changed = changed || (strncmp(curr_settings->sta_pass, new_settings.sta_pass, sizeof(new_settings.sta_pass) - 1) != 0);
        changed = changed || (curr_settings->sta_dhcp_client != new_settings.sta_dhcp_client);
        if (!changed && new_settings.sta_dhcp_client) {
            changed = changed || (memcmp(&curr_settings->sta_static_ip, &new_settings.sta_static_ip, sizeof(new_settings.sta_static_ip)) != 0);
        }
        if (changed) {
            return true;
        }
    }

    return false;
}


esp_err_t network_update_wifi_settings(void)
{
    ESP_LOGD(TAG, "Updating WiFi settings...");

    char hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    esp_err_t ret = read_hostname(hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read hostname");
        return ret;
    }

    wifi_settings_t wifi_settings;
    ret = read_wifi_settings(&wifi_settings);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read WiFi settings");
        return ret;
    }

    #if QEMU_BUILD
        ESP_LOGW(TAG, "WiFi config change is not supported in QEMU build");
        ret = ESP_OK;
    #else
        wifi_apsta_config_t apsta_cfg = {0};
        make_wifi_apsta_cfg(&wifi_settings, &apsta_cfg);
        ret = wifi_set_apsta_config(&apsta_cfg, hostname);
    #endif

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update WiFi settings");
        return ret;
    }

    memcpy(current_settings.hostname, hostname, sizeof(current_settings.hostname));
    memcpy(&current_settings.wifi_settings, &wifi_settings, sizeof(current_settings.wifi_settings));

    update_sys_info_wifi_state(&current_settings.wifi_settings);

    ESP_LOGD(TAG, "WiFi settings updated");
    return ESP_OK;
}


bool network_check_mdns_settings_changed(void)
{
    char hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    esp_err_t ret = read_hostname(hostname);
    if (ret == ESP_OK) {
        if (strncmp(current_settings.hostname, hostname, SETTING_ITEM_MAX_STR_LEN - 1) != 0) {
            return true;
        }
    }
    return false;
}

esp_err_t network_update_mdns_settings(void)
{
    ESP_LOGD(TAG, "Updating mDNS settings...");

    char hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    esp_err_t ret = read_hostname(hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read hostname");
        return ret;
    }

    memcpy(current_settings.hostname, hostname, sizeof(current_settings.hostname));

    if (mdns_hostname_set(hostname) == ESP_OK) {
        ESP_LOGI(TAG, "mDNS hostname set to: %s", hostname);
    } else {
        ESP_LOGE(TAG, "Unable to set mDNS hostname");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "mDNS settings updated");
    return ESP_OK;
}

wifi_mode_t network_get_wifi_mode(void)
{
    return current_settings.wifi_settings.wifi_mode;
}
