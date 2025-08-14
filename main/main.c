#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

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
#include "config_button.h"
#include "system_voltage.h"

#include "esp_io_expander_tca95xx_16bit.h"
#include "driver/gpio.h"

#include "rs485_control.h"
#include "mio_control.h"
#include "leds_control.h"
#include "update_rs485_mio_gpio_states.h"

static const char *TAG = "main";

#define LEDS_TOGGLE_PERIOD_MS       500
#define FACTORY_RESET_HOLD_TIME_MS  5000

#define IO_EXPANDER_SDA_PIN         GPIO_NUM_32
#define IO_EXPANDER_SCL_PIN         GPIO_NUM_33
#define IO_EXPANDER_I2C_ADDRESS     ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000

static i2c_master_bus_handle_t i2c_handle = NULL;
const i2c_master_bus_config_t bus_config = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = IO_EXPANDER_SDA_PIN,
    .scl_io_num = IO_EXPANDER_SCL_PIN,
    .clk_source = I2C_CLK_SRC_DEFAULT,
};
static esp_io_expander_handle_t io_expander = NULL;

static void factory_reset(void)
{
    ESP_LOGI(TAG, "Factory reset initiated!");

    // Stop blinking to indicate factory reset in progress
    // TODO: Set specific LED pattern for factory reset

    // Reset all settings to defaults
    ESP_LOGI(TAG, "Resetting all settings to factory defaults...");

    // Get all setting keys and reset them to default values
    size_t count = setting_items_get_count();
    for (size_t i = 0; i < count; i++) {
        const char *key = setting_items_get_key_at(i);
        if (key) {
            // Reset the setting to its default value
            setting_items_set_default(key);
        } else {
            ESP_LOGW(TAG, "Setting key at index %d is NULL", i);
        }
    }

    ESP_LOGI(TAG, "Factory reset completed! Settings will revert to defaults.");
    ESP_LOGI(TAG, "Device will continue running with default configuration.");
}

// Button callback for factory reset
static void config_button_callback(uint32_t press_count, uint32_t press_duration_ms)
{
    ESP_LOGI(TAG, "Button pressed %lu times, held for %lu ms", press_count, press_duration_ms);

    if (press_duration_ms >= FACTORY_RESET_HOLD_TIME_MS) {
        ESP_LOGW(TAG, "Factory reset triggered by 5-second button hold!");
        factory_reset();
    }
}

static void gpio_expander_init(void)
{
    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_io_expander_new_i2c_tca95xx_16bit(i2c_handle, IO_EXPANDER_I2C_ADDRESS, &io_expander);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GPIO expander: %s", esp_err_to_name(ret));
        return;
    }

    esp_io_expander_print_state(io_expander);
    ESP_LOGI(TAG, "GPIO expander initialized successfully");
}

// task to toggle P04/P05/P07 every 500 ms // TODO: according to requirements https://wirenboard.youtrack.cloud/issue/FW-933
static void blink_task(void *arg)
{
    while (1) {
        leds_control_set_eth_led(true);
        leds_control_set_wifi_led(true);
        leds_control_set_unknown_led(true);
        vTaskDelay(pdMS_TO_TICKS(LEDS_TOGGLE_PERIOD_MS));
        leds_control_set_eth_led(false);
        leds_control_set_wifi_led(false);
        leds_control_set_unknown_led(false);
        vTaskDelay(pdMS_TO_TICKS(LEDS_TOGGLE_PERIOD_MS));
    }
}

// Выводит все настройки в лог.
// TODO: В релизе удалить
static inline void print_setting_items(void)
{
    char value[SETTING_ITEM_MAX_STR_LEN] = {0};

    ESP_LOGI(TAG, "=== Current Settings ===");

    size_t count = setting_items_get_count();
    for (size_t i = 0; i < count; i++) {
        const char *key = setting_items_get_key_at(i);
        if (key) {
            // Skip printing any setting that contains 'pass' for security
            if ((key != NULL) && (strstr(key, "pass") != NULL)) {
                ESP_LOGI(TAG, "%s: [HIDDEN]", key);
                continue;
            }

            if (setting_items_read(key, value) == ESP_OK) {
                ESP_LOGI(TAG, "%s: %s", key, value);
            } else {
                ESP_LOGW(TAG, "%s: [not found]", key);
            }
        }
    }

    ESP_LOGI(TAG, "=== Settings printed (passwords hidden for security) ===");
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

static wifi_auth_mode_t str_to_wifi_auth_mode(const char *str) {
    if (strcmp(str, WIFI_AUTH_WPA2_PSK_STR) == 0) {
        return WIFI_AUTH_WPA2_PSK;
    } else if (strcmp(str, WIFI_AUTH_WPA3_PSK_STR) == 0) {
        return WIFI_AUTH_WPA3_PSK;
    } else {
        return WIFI_AUTH_OPEN;
    }
}

// Helper function to convert string IP to uint32_t
static uint32_t str_to_ip(const char *ip_str) {
    uint32_t ip = 0;
    if (ip_str && strnlen(ip_str, SETTING_ITEM_MAX_STR_LEN) > 0) {
        inet_pton(AF_INET, ip_str, &ip);
    }
    return ip;
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(setting_items_init());

    // Generate unique hostname
    char generated_hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    int ret = snprintf(generated_hostname, SETTING_ITEM_MAX_STR_LEN, "%s-%02X%02X%02X", BASE_HOSTNAME, mac[3],
             mac[4], mac[5]);
    if (ret >= SETTING_ITEM_MAX_STR_LEN) {
        ESP_LOGW(TAG, "Generated hostname was truncated");
    }

    // Set hostname if not already set
    char hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (setting_items_read(KEY_HOSTNAME, hostname) != ESP_OK) {
        // Set default generated hostname
        ESP_ERROR_CHECK(setting_items_save(KEY_HOSTNAME, generated_hostname));
        strncpy(hostname, generated_hostname, SETTING_ITEM_MAX_STR_LEN - 1);
        hostname[SETTING_ITEM_MAX_STR_LEN - 1] = '\0';
    }
    ESP_LOGI(TAG, "Hostname: %s", hostname);

    // Initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", hostname);

    // Configure WiFi using convenient wrapper functions
    wifi_apsta_config_t apsta_cfg = {0};
    esp_netif_ip_info_t ap_ip_info;

    char temp_value[SETTING_ITEM_MAX_STR_LEN] = {0};

    // Read AP IP configuration using string functions (for IP addresses)
    if (setting_items_read(KEY_AP_IP_STATIC, temp_value) == ESP_OK) {
        ap_ip_info.ip.addr = str_to_ip(temp_value);
    }
    if (setting_items_read(KEY_AP_MASK_STATIC, temp_value) == ESP_OK) {
        ap_ip_info.netmask.addr = str_to_ip(temp_value);
    }
    if (setting_items_read(KEY_AP_GW_STATIC, temp_value) == ESP_OK) {
        ap_ip_info.gw.addr = str_to_ip(temp_value);
    }
    apsta_cfg.ap_ip_info = &ap_ip_info;

    // Read WiFi credentials
    if (setting_items_read(KEY_AP_SSID, temp_value) == ESP_OK) {
        strncpy(apsta_cfg.ap_ssid, temp_value, sizeof(apsta_cfg.ap_ssid) - 1);
        apsta_cfg.ap_ssid[sizeof(apsta_cfg.ap_ssid) - 1] = '\0';
    }
    if (setting_items_read(KEY_AP_PASS, temp_value) == ESP_OK) {
        strncpy(apsta_cfg.ap_pass, temp_value, sizeof(apsta_cfg.ap_pass) - 1);
        apsta_cfg.ap_pass[sizeof(apsta_cfg.ap_pass) - 1] = '\0';
    }
    if (setting_items_read(KEY_STA_SSID, temp_value) == ESP_OK) {
        strncpy(apsta_cfg.sta_ssid, temp_value, sizeof(apsta_cfg.sta_ssid) - 1);
        apsta_cfg.sta_ssid[sizeof(apsta_cfg.sta_ssid) - 1] = '\0';
    }
    if (setting_items_read(KEY_STA_PASS, temp_value) == ESP_OK) {
        strncpy(apsta_cfg.sta_pass, temp_value, sizeof(apsta_cfg.sta_pass) - 1);
        apsta_cfg.sta_pass[sizeof(apsta_cfg.sta_pass) - 1] = '\0';
    }

    // Read WiFi mode using string function (enum conversion needed)
    if (setting_items_read(KEY_WIFI_MODE, temp_value) == ESP_OK) {
        if (strcmp(temp_value, WIFI_MODE_AP_STR) == 0) {
            apsta_cfg.wifi_mode = WIFI_MODE_AP;
        } else if (strcmp(temp_value, WIFI_MODE_STA_STR) == 0) {
            apsta_cfg.wifi_mode = WIFI_MODE_STA;
        } else if (strcmp(temp_value, WIFI_MODE_APSTA_STR) == 0) {
            apsta_cfg.wifi_mode = WIFI_MODE_APSTA;
        } else {
            apsta_cfg.wifi_mode = WIFI_MODE_NULL;
        }
    }

    // Read WiFi auth modes using string functions
    if (setting_items_read(KEY_WIFI_AUTH_AP, temp_value) == ESP_OK) {
        apsta_cfg.wifi_auth_mode_ap = str_to_wifi_auth_mode(temp_value);
    }
    if (setting_items_read(KEY_WIFI_AUTH_STA, temp_value) == ESP_OK) {
        apsta_cfg.wifi_auth_mode_sta = str_to_wifi_auth_mode(temp_value);
    }

    apsta_cfg.sta_event_handler = &wifi_sta_connect_event_handler;
    apsta_cfg.ap_event_handler = &wifi_ap_connect_event_handler;
    ESP_ERROR_CHECK(wifi_init_apsta(&apsta_cfg, generated_hostname));

    // Read and log WiFi STA and AP MAC addresses
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

    // Configure Ethernet
    bool eth_dhcpc = setting_items_read_bool(KEY_ETH_DHCPC);
    esp_netif_ip_info_t *eth_ip_info = NULL;
    esp_netif_ip_info_t static_ip_info = {0};

    // Read Ethernet static IP configuration
    if (setting_items_read(KEY_ETH_IP_STATIC, temp_value) == ESP_OK) {
        static_ip_info.ip.addr = str_to_ip(temp_value);
    }
    if (setting_items_read(KEY_ETH_MASK_STATIC, temp_value) == ESP_OK) {
        static_ip_info.netmask.addr = str_to_ip(temp_value);
    }
    if (setting_items_read(KEY_ETH_GW_STATIC, temp_value) == ESP_OK) {
        static_ip_info.gw.addr = str_to_ip(temp_value);
    }

    if (!eth_dhcpc) {
        eth_ip_info = &static_ip_info;
    }
    ESP_ERROR_CHECK(ethernet_init(&eth_connect_event_handler, eth_ip_info, generated_hostname));

    // Get Ethernet MAC address after initialization
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

    ESP_ERROR_CHECK(http_server_init());

    sys_info_init();
    print_setting_items();

    gpio_expander_init();
    rs485_control_init(io_expander);
    leds_control_init(io_expander);
    mio_control_init(io_expander);

    config_button_init(config_button_callback);
    system_voltage_init();

    update_rs485_control();
    update_io_bus_control();

    // init and start blink task to indicate that we are in bootloader mode
    xTaskCreate(blink_task, "blink_task", 2048, NULL, 1, NULL);

    ESP_LOGI("main", "Firmware version: %s", FIRMWARE_VERSION);

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
