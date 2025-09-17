#include "wifi_apsta.h"
#include "config.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"


#define MAX_STA_CONN                        5
#define STA_ESP_MAXIMUM_RETRY               10

#define WIFI_STA_CONNECT_READY_BIT          BIT0
#define WIFI_STA_CONNECTED_BIT              BIT1
#define WIFI_STA_DISCONNECTED_BIT           BIT2

#define WIFI_STA_CONNECT_TASK_STACK_SIZE    4096
#define WIFI_STA_CONNECT_TASK_PRIORITY      1
#define WIFI_STA_RECONNECT_DELAY_MS         500


static const char* TAG = "wifi_apsta";

static EventGroupHandle_t wifi_sta_event_group = NULL;
static esp_event_handler_instance_t instance_any_id = NULL;
static esp_event_handler_instance_t instance_got_ip = NULL;
static SemaphoreHandle_t connect_scan_mutex = NULL;


static void sta_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                              void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START: {
                xEventGroupSetBits(wifi_sta_event_group, WIFI_STA_CONNECT_READY_BIT);
                break;
            }
            case WIFI_EVENT_STA_CONNECTED: {
                xEventGroupSetBits(wifi_sta_event_group, WIFI_STA_CONNECTED_BIT);
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                xEventGroupSetBits(wifi_sta_event_group, WIFI_STA_DISCONNECTED_BIT);
                break;
            }
        }
    } else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "WiFi STA got IP: " IPSTR ", mask: " IPSTR ", gateway: " IPSTR,
                IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask), IP2STR(&event->ip_info.gw));
    }
}


static void ap_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                             void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
        ESP_LOGI(TAG, "WiFi AP: station " MACSTR " joined, AID: %d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
        ESP_LOGI(TAG, "WiFi AP: station " MACSTR " leaved, AID: %d", MAC2STR(event->mac), event->aid);
    }
}


void wifi_sta_connect_task(void* pvParameter)
{
    bool connected = false;

    while (1) {
        EventBits_t bits_to_wait = WIFI_STA_CONNECT_READY_BIT | WIFI_STA_CONNECTED_BIT | WIFI_STA_DISCONNECTED_BIT;
        EventBits_t bits = xEventGroupWaitBits(wifi_sta_event_group, bits_to_wait, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & WIFI_STA_CONNECT_READY_BIT) {
            wifi_sta_connect_scan_lock();
            ESP_LOGI(TAG, "WiFi STA connecting to AP...");
            esp_wifi_connect();
        }
        if (bits & WIFI_STA_CONNECTED_BIT) {
            wifi_sta_connect_scan_unlock();
            connected = true;
            ESP_LOGI(TAG, "WiFi STA connected to AP");
        }
        if (bits & WIFI_STA_DISCONNECTED_BIT) {
            if (!connected) {
                ESP_LOGW(TAG, "WiFi STA unable to connect to AP");
                wifi_sta_connect_scan_unlock();
            } else {
                ESP_LOGW(TAG, "WiFi STA disconnected from AP");
            }
            connected = false;

            vTaskDelay(pdMS_TO_TICKS(WIFI_STA_RECONNECT_DELAY_MS));

            wifi_sta_connect_scan_lock();
            ESP_LOGI(TAG, "WiFi STA trying to reconnect to AP...");
            esp_wifi_connect();
        }
    }
}


esp_err_t wifi_init_apsta(wifi_apsta_config_t* apsta_cfg, char* netif_hostname)
{
    if (apsta_cfg->wifi_mode > WIFI_MODE_APSTA) {
        ESP_LOGE(TAG, "Invalid wifi mode");
        return ESP_ERR_INVALID_ARG;
    }

    connect_scan_mutex = xSemaphoreCreateMutex();
    if (connect_scan_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi connect/scan mutex");
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_sta_event_group = xEventGroupCreate();
    if (wifi_sta_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi STA event group");
        vSemaphoreDelete(connect_scan_mutex);
        return ESP_FAIL;
    }

    ESP_RETURN_ON_FALSE(esp_wifi_init(&cfg) == ESP_OK, ESP_FAIL, TAG, "esp_wifi_init failed");
    if (apsta_cfg->wifi_mode != WIFI_MODE_NULL) {
        ESP_RETURN_ON_FALSE(esp_netif_init() == ESP_OK, ESP_FAIL, TAG, "esp_netif_init failed");
    }

    wifi_mode_t real_mode = apsta_cfg->wifi_mode;
    if (real_mode == WIFI_MODE_AP) {
        // Use APSTA instead of AP mode to be able to scan WiFi networks
        real_mode = WIFI_MODE_APSTA;
    } else if (real_mode == WIFI_MODE_NULL) {
        // Use STA instead of NULL mode to be able to scan WiFi networks
        real_mode = WIFI_MODE_STA;
    }
    ESP_RETURN_ON_FALSE(esp_wifi_set_mode(real_mode) == ESP_OK, ESP_FAIL, TAG, "esp_wifi_set_mode failed");

    if ((apsta_cfg->wifi_mode == WIFI_MODE_AP) || (apsta_cfg->wifi_mode == WIFI_MODE_APSTA)) {
        esp_netif_t* esp_netif_ap = esp_netif_create_default_wifi_ap();
        if (esp_netif_ap == NULL) {
            ESP_LOGE(TAG, "esp_netif_create_default_wifi_ap failed");
            return ESP_FAIL;
        }
        if (apsta_cfg->ap_ip_info != NULL) {
            esp_netif_dhcps_stop(esp_netif_ap);
            err = esp_netif_set_ip_info(esp_netif_ap, apsta_cfg->ap_ip_info);
            ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "esp_netif_set_ip_info failed");
            err = esp_netif_dhcps_start(esp_netif_ap);
            ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "esp_netif_dhcps_start failed");
        }
        wifi_config_t wifi_config_ap = {
            .ap =
                {
                    .channel = WIFI_CHAN_AP,
                    .max_connection = MAX_STA_CONN,
                    .authmode = apsta_cfg->wifi_auth_mode_ap,
                },
        };
        if (strnlen(apsta_cfg->ap_pass, WIFI_PASS_MAX_LEN) == 0) {
            wifi_config_ap.ap.authmode = WIFI_AUTH_OPEN;
        }
        memcpy(&wifi_config_ap.ap.ssid, apsta_cfg->ap_ssid, strnlen(apsta_cfg->ap_ssid, WIFI_SSID_MAX_LEN));
        wifi_config_ap.ap.ssid_len = strnlen(apsta_cfg->ap_ssid, WIFI_SSID_MAX_LEN);
        if (wifi_config_ap.ap.authmode != WIFI_AUTH_OPEN) {
            memcpy(&wifi_config_ap.ap.password, apsta_cfg->ap_pass, strnlen(apsta_cfg->ap_pass, WIFI_PASS_MAX_LEN));
        } else {
            wifi_config_ap.ap.password[0] = 0;
            ESP_LOGW(TAG, "WiFi access point uses OPEN auth type");
        }
        err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "esp_wifi_set_config failed");
        if (apsta_cfg->ap_event_handler != NULL) {
            err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                      apsta_cfg->ap_event_handler, NULL, NULL);
            ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "ap event handler register failed");
        }
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ap_event_handler,
                                                  NULL, NULL);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG,
                            "esp_event_handler_instance_register failed");
    }

    if ((apsta_cfg->wifi_mode == WIFI_MODE_STA) || (apsta_cfg->wifi_mode == WIFI_MODE_APSTA)) {
        esp_netif_t* esp_netif_sta = esp_netif_create_default_wifi_sta();

        // Set DHCP hostname for WiFi Station
        if (netif_hostname != NULL && esp_netif_sta != NULL) {
            err = esp_netif_set_hostname(esp_netif_sta, netif_hostname);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "WiFi STA DHCP hostname set to: %s", netif_hostname);
            } else {
                ESP_LOGE(TAG, "Failed to set WiFi STA hostname: %s", esp_err_to_name(err));
            }
        }

        if (apsta_cfg->sta_ip_info != NULL) {
            esp_netif_dhcpc_stop(esp_netif_sta);
            err = esp_netif_set_ip_info(esp_netif_sta, apsta_cfg->sta_ip_info);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set WiFi STA static IP: %s", esp_err_to_name(err));
            }
        } else {
            err = esp_netif_dhcpc_start(esp_netif_sta);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WiFi STA DHCP client: %s", esp_err_to_name(err));
            }
        }

        wifi_config_t wifi_config_sta = {
            .sta =
                {
                    .threshold.authmode = apsta_cfg->wifi_auth_mode_sta,
                    .pmf_cfg =
                        {
                            .capable = true,
                            .required = false,
                        },
                },
        };

        memcpy(&wifi_config_sta.sta.ssid, apsta_cfg->sta_ssid, strnlen(apsta_cfg->sta_ssid, WIFI_SSID_MAX_LEN));

        if (wifi_config_sta.sta.threshold.authmode != WIFI_AUTH_OPEN) {
            memcpy(&wifi_config_sta.sta.password, apsta_cfg->sta_pass, strnlen(apsta_cfg->sta_pass, WIFI_PASS_MAX_LEN));
        } else {
            wifi_config_sta.sta.password[0] = 0;
            ESP_LOGW(TAG, "WiFi station uses OPEN auth type");
        }

        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config_sta);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "esp_wifi_set_config failed");

        if (apsta_cfg->sta_event_handler != NULL) {
            err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                      apsta_cfg->sta_event_handler, NULL,
                                                      &instance_got_ip);
            ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "sta event handler register failed");
            err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    apsta_cfg->sta_event_handler,
                                                    NULL, &instance_any_id);
            ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "sta event handler register failed");
        }
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &sta_event_handler,
                                                  NULL, &instance_got_ip);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "sta event handler register failed");
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &sta_event_handler,
                                                  NULL, &instance_any_id);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "sta event handler register failed");
    }

    ESP_RETURN_ON_FALSE(esp_wifi_start() == ESP_OK, ESP_FAIL, TAG, "esp_wifi_start failed");

    xTaskCreate(wifi_sta_connect_task, "wifi_sta_connect_task", WIFI_STA_CONNECT_TASK_STACK_SIZE,
                NULL, WIFI_STA_CONNECT_TASK_PRIORITY, NULL);

    return ESP_OK;
}


esp_err_t wifi_sta_connect_scan_lock(void)
{
    if (connect_scan_mutex == NULL) {
        return ESP_FAIL;
    }
    xSemaphoreTake(connect_scan_mutex, portMAX_DELAY);
    return ESP_OK;
}


esp_err_t wifi_sta_connect_scan_unlock(void)
{
    if (connect_scan_mutex == NULL) {
        return ESP_FAIL;
    }
    xSemaphoreGive(connect_scan_mutex);
    return ESP_OK;
}
