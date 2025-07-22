#include "wifi_apsta.h"

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
#define WIFI_WAITING_EVENTS_STACK_SIZE      (1024 * 6)
#define WIFI_WAITING_EVENTS_PRIORITY        1
#define WIFI_CONNECTED_BIT                  BIT0
#define WIFI_FAIL_BIT                       BIT1

static const char* TAG = "wifi_apsta";

static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_event_handler_instance_t instance_any_id = NULL;
static esp_event_handler_instance_t instance_got_ip = NULL;
static int s_retry_num = 0;

static void sta_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                              void* event_data)
{
    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        esp_wifi_connect();
    } else if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        if (s_retry_num < STA_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void ap_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                             void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

void wifi_waiting_events(void* pvParameter)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, (WIFI_CONNECTED_BIT | WIFI_FAIL_BIT),
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        // ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", wifi_config_sta.sta.ssid,
        // wifi_config_sta.sta.password);
    } else if (bits & WIFI_FAIL_BIT) {
        // ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", wifi_config_sta.sta.ssid,
        // wifi_config_sta.sta.password);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
    vTaskDelete(NULL);
}

esp_err_t wifi_init_apsta(wifi_apsta_config_t* apsta_cfg, char* netif_hostname)
{
    if (apsta_cfg->wifi_mode == WIFI_MODE_NULL) {
        return ESP_OK;
    }
    if (apsta_cfg->wifi_mode > WIFI_MODE_APSTA) {
        ESP_LOGE(TAG, "Invalid wifi mode");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    s_wifi_event_group = xEventGroupCreate();

    ESP_RETURN_ON_FALSE(esp_wifi_init(&cfg) == ESP_OK, ESP_FAIL, TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_FALSE(esp_netif_init() == ESP_OK, ESP_FAIL, TAG, "esp_netif_init failed");
    ESP_RETURN_ON_FALSE(esp_wifi_set_mode(apsta_cfg->wifi_mode) == ESP_OK, ESP_FAIL, TAG,
                        "esp_wifi_set_mode failed");

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
                    .authmode = apsta_cfg->wifi_auth_mode_sta,
                },
        };
        if (strnlen(apsta_cfg->ap_pass, WIFI_PASS_MAX_LEN) == 0) {
            wifi_config_ap.ap.authmode = apsta_cfg->wifi_auth_mode_ap;
        }
        memcpy(&wifi_config_ap.ap.ssid, apsta_cfg->ap_ssid, strnlen(apsta_cfg->ap_ssid, WIFI_SSID_MAX_LEN));
        wifi_config_ap.ap.ssid_len = strnlen(apsta_cfg->ap_ssid, WIFI_SSID_MAX_LEN);
        memcpy(&wifi_config_ap.ap.password, apsta_cfg->ap_pass, strnlen(apsta_cfg->ap_pass, WIFI_PASS_MAX_LEN));
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

        wifi_config_t wifi_config_sta = {
            .sta =
                {
                    .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                    .pmf_cfg =
                        {
                            .capable = true,
                            .required = false,
                        },
                },
        };
        memcpy(&wifi_config_sta.sta.ssid, apsta_cfg->sta_ssid, strnlen(apsta_cfg->sta_ssid, WIFI_SSID_MAX_LEN));
        memcpy(&wifi_config_sta.sta.password, apsta_cfg->sta_pass, strnlen(apsta_cfg->sta_pass, WIFI_PASS_MAX_LEN));
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
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &sta_event_handler,
                                                  NULL, &instance_any_id);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "sta event handler register failed");
    }

    ESP_RETURN_ON_FALSE(esp_wifi_start() == ESP_OK, ESP_FAIL, TAG, "esp_wifi_start failed");

    xTaskCreate(&wifi_waiting_events, "wifi_init_apsta", WIFI_WAITING_EVENTS_STACK_SIZE, NULL,
                WIFI_WAITING_EVENTS_PRIORITY, NULL);  // TODO: check stack size

    return ESP_OK;
}
