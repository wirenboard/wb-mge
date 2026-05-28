#include <string.h>
#include "wifi_apsta.h"
#include "config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"


#define MAX_STA_CONN                        5

#define WIFI_STA_CONNECT_READY_BIT          BIT0
#define WIFI_STA_CONNECTED_BIT              BIT1
#define WIFI_STA_DISCONNECTED_BIT           BIT2

#define WIFI_STA_CONN_TASK_STARTED_BIT      BIT8
#define WIFI_STA_CONN_TASK_FINISHED_BIT     BIT9
#define WIFI_STA_CONN_TASK_EXIT_REQ_BIT     BIT16

#define WIFI_STA_CONNECT_TASK_STACK_SIZE    4096
#define WIFI_STA_CONNECT_TASK_PRIORITY      1
#define WIFI_STA_RECONNECT_DELAY_MS         500


typedef struct {
    bool initialized;
    EventGroupHandle_t sta_event_group;
    SemaphoreHandle_t connect_scan_mutex;
    TaskHandle_t sta_conn_task_handle;
    esp_event_handler_instance_t ap_ext_evt_handler_inst;
    esp_event_handler_instance_t ap_int_evt_handler_inst;
    esp_event_handler_instance_t sta_ext_any_id_handler_inst;
    esp_event_handler_instance_t sta_ext_got_ip_handler_inst;
    esp_event_handler_instance_t sta_int_any_id_handler_inst;
    esp_event_handler_instance_t sta_int_got_ip_handler_inst;
    wifi_mode_t config_mode;
    wifi_mode_t real_mode;
    esp_netif_t* netif_ap;
    esp_netif_t* netif_sta;
} wifi_ctx_t;

static wifi_ctx_t wifi_ctx = {
    .initialized = false,
    .sta_event_group = NULL,
    .connect_scan_mutex = NULL,
    .sta_conn_task_handle = NULL,
    .ap_ext_evt_handler_inst = NULL,
    .ap_int_evt_handler_inst = NULL,
    .sta_ext_any_id_handler_inst = NULL,
    .sta_ext_got_ip_handler_inst = NULL,
    .sta_int_any_id_handler_inst = NULL,
    .sta_int_got_ip_handler_inst = NULL,
    .config_mode = WIFI_MODE_NULL,
    .real_mode = WIFI_MODE_NULL,
    .netif_ap = NULL,
    .netif_sta = NULL
};

static const char* TAG = "wifi_apsta";


static void sta_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                              void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START: {
                ESP_LOGD(TAG, "STA event: START");
                xEventGroupSetBits(wifi_ctx.sta_event_group, WIFI_STA_CONNECT_READY_BIT);
                break;
            }
            case WIFI_EVENT_STA_CONNECTED: {
                ESP_LOGD(TAG, "STA event: CONNECTED");
                xEventGroupSetBits(wifi_ctx.sta_event_group, WIFI_STA_CONNECTED_BIT);
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                ESP_LOGD(TAG, "STA event: DISCONNECTED");
                xEventGroupSetBits(wifi_ctx.sta_event_group, WIFI_STA_DISCONNECTED_BIT);
                break;
            }
            default:
                break;
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


static esp_err_t connect_scan_mutex_safe_unlock(void)
{
    if (wifi_ctx.connect_scan_mutex == NULL) {
        return ESP_FAIL;
    }

    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    TaskHandle_t mutex_holder_task = xSemaphoreGetMutexHolder(wifi_ctx.connect_scan_mutex);
    if (current_task == mutex_holder_task) {
        xSemaphoreGive(wifi_ctx.connect_scan_mutex);
    }
    return ESP_OK;
}


void wifi_sta_connect_task(void* pvParameter)
{
    xEventGroupSetBits(wifi_ctx.sta_event_group, WIFI_STA_CONN_TASK_STARTED_BIT);
    ESP_LOGD(TAG, "WiFi STA connect task started");

    bool connected = false;

    while (1) {
        EventBits_t bits_to_wait = WIFI_STA_CONNECT_READY_BIT | WIFI_STA_CONNECTED_BIT | WIFI_STA_DISCONNECTED_BIT | WIFI_STA_CONN_TASK_EXIT_REQ_BIT;
        EventBits_t bits = xEventGroupWaitBits(wifi_ctx.sta_event_group, bits_to_wait, pdTRUE, pdFALSE, portMAX_DELAY);
        bool sta_enabled = (wifi_ctx.config_mode == WIFI_MODE_STA) || (wifi_ctx.config_mode == WIFI_MODE_APSTA);
        if (bits & WIFI_STA_CONNECT_READY_BIT) {
            if (sta_enabled) {
                wifi_sta_connect_scan_lock();
                ESP_LOGI(TAG, "WiFi STA connecting to AP...");
                esp_err_t ret = esp_wifi_connect();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "WiFi STA failed to start connection to AP");
                    wifi_sta_connect_scan_unlock();
                }
            }
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

            if (sta_enabled) {
                vTaskDelay(pdMS_TO_TICKS(WIFI_STA_RECONNECT_DELAY_MS));

                wifi_sta_connect_scan_lock();
                ESP_LOGI(TAG, "WiFi STA trying to reconnect to AP...");
                esp_wifi_connect();
            }
        }
        if (bits & WIFI_STA_CONN_TASK_EXIT_REQ_BIT) {
            connect_scan_mutex_safe_unlock();
            break;
        }
    }

    ESP_LOGD(TAG, "WiFi STA connect task finished");
    xEventGroupSetBits(wifi_ctx.sta_event_group, WIFI_STA_CONN_TASK_FINISHED_BIT);
    wifi_ctx.sta_conn_task_handle = NULL;
    vTaskDelete(NULL);
}


wifi_mode_t get_real_mode(wifi_mode_t cfg_mode)
{
    wifi_mode_t real_mode = cfg_mode;
    if (real_mode == WIFI_MODE_AP) {
        // Use APSTA instead of AP mode to be able to scan WiFi networks
        real_mode = WIFI_MODE_APSTA;
    } else if (real_mode == WIFI_MODE_NULL) {
        // Use STA instead of NULL mode to be able to scan WiFi networks
        real_mode = WIFI_MODE_STA;
    }
    return real_mode;
}


static esp_err_t configure_ap(wifi_apsta_config_t* apsta_cfg, char* netif_hostname)
{
    esp_err_t err = ESP_OK;

    // Set DHCP hostname for WiFi AP
    if (netif_hostname != NULL) {
        err = esp_netif_set_hostname(wifi_ctx.netif_ap, netif_hostname);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "WiFi AP hostname set to: %s", netif_hostname);
        } else {
            ESP_LOGE(TAG, "Failed to set WiFi AP hostname: %s", esp_err_to_name(err));
        }
    }

    if (apsta_cfg->ap_ip_info != NULL) {
        esp_netif_dhcps_stop(wifi_ctx.netif_ap);
        err = esp_netif_set_ip_info(wifi_ctx.netif_ap, apsta_cfg->ap_ip_info);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to set WiFi AP IP address");
        err = esp_netif_dhcps_start(wifi_ctx.netif_ap);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to start WiFi AP DHCP server");
    }

    wifi_config_t wifi_config_ap = {
        .ap = {
            .channel = WIFI_CHAN_AP,
            .max_connection = MAX_STA_CONN,
            .authmode = apsta_cfg->wifi_auth_mode_ap,
            .pmf_cfg = {
                // WIFI_AUTH_WPA3_PSK mode requires PMF to be enabled
                .required = (apsta_cfg->wifi_auth_mode_ap == WIFI_AUTH_WPA3_PSK),
                .capable = true
            }
        }
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
    ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to set WiFi AP config");

    // Unregister previous external event handler
    if (wifi_ctx.ap_ext_evt_handler_inst != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_ctx.ap_ext_evt_handler_inst);
        wifi_ctx.ap_ext_evt_handler_inst = NULL;
    }

    // Register new external event handler
    if (apsta_cfg->ap_event_handler != NULL) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
            apsta_cfg->ap_event_handler, NULL, &wifi_ctx.ap_ext_evt_handler_inst);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to register WiFi AP external event handler");
    }

    // Register internal event handler only if not registered yet
    if (wifi_ctx.ap_int_evt_handler_inst == NULL) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
            &ap_event_handler, NULL, &wifi_ctx.ap_int_evt_handler_inst);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to register WiFi AP internal event handler");
    }

    return ESP_OK;
}


static esp_err_t configure_sta(wifi_apsta_config_t* apsta_cfg, char* netif_hostname)
{
    esp_err_t err = ESP_OK;

    // Set DHCP hostname for WiFi Station
    if (netif_hostname != NULL) {
        err = esp_netif_set_hostname(wifi_ctx.netif_sta, netif_hostname);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "WiFi STA DHCP hostname set to: %s", netif_hostname);
        } else {
            ESP_LOGE(TAG, "Failed to set WiFi STA hostname: %s", esp_err_to_name(err));
        }
    }

    if (apsta_cfg->sta_ip_info != NULL) {
        esp_netif_dhcpc_stop(wifi_ctx.netif_sta);
        err = esp_netif_set_ip_info(wifi_ctx.netif_sta, apsta_cfg->sta_ip_info);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set WiFi STA static IP: %s", esp_err_to_name(err));
        }
    } else {
        err = esp_netif_dhcpc_start(wifi_ctx.netif_sta);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start WiFi STA DHCP client: %s", esp_err_to_name(err));
        }
    }

    wifi_config_t wifi_config_sta = {
        .sta = {
            .threshold.authmode = apsta_cfg->wifi_auth_mode_sta,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            }
        }
    };

    memcpy(&wifi_config_sta.sta.ssid, apsta_cfg->sta_ssid, strnlen(apsta_cfg->sta_ssid, WIFI_SSID_MAX_LEN));

    if (wifi_config_sta.sta.threshold.authmode != WIFI_AUTH_OPEN) {
        memcpy(&wifi_config_sta.sta.password, apsta_cfg->sta_pass, strnlen(apsta_cfg->sta_pass, WIFI_PASS_MAX_LEN));
    } else {
        wifi_config_sta.sta.password[0] = 0;
        ESP_LOGW(TAG, "WiFi station uses OPEN auth type");
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config_sta);
    ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to set WiFi STA config");

    // Unregister previous external event handler
    if (wifi_ctx.sta_ext_got_ip_handler_inst != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_ctx.sta_ext_got_ip_handler_inst);
        wifi_ctx.sta_ext_got_ip_handler_inst = NULL;
    }
    if (wifi_ctx.sta_ext_any_id_handler_inst != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_ctx.sta_ext_any_id_handler_inst);
        wifi_ctx.sta_ext_any_id_handler_inst = NULL;
    }

    // Register new external event handler
    if (apsta_cfg->sta_event_handler != NULL) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
            apsta_cfg->sta_event_handler, NULL, &wifi_ctx.sta_ext_got_ip_handler_inst);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to register WiFi STA IP event handler");
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
            apsta_cfg->sta_event_handler, NULL, &wifi_ctx.sta_ext_any_id_handler_inst);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to register WiFi STA ANY event handler");
    }

    // Register internal event handler only if not registered yet
    if (wifi_ctx.sta_int_got_ip_handler_inst == NULL) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
            &sta_event_handler, NULL, &wifi_ctx.sta_int_got_ip_handler_inst);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "sta event handler register failed");
    }
    if (wifi_ctx.sta_int_any_id_handler_inst == NULL) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
            &sta_event_handler, NULL, &wifi_ctx.sta_int_any_id_handler_inst);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "sta event handler register failed");
    }

    return ESP_OK;
}


esp_err_t configure_ap_sta(wifi_apsta_config_t* apsta_cfg, char* netif_hostname)
{
    esp_err_t result = ESP_OK;

    if ((apsta_cfg->wifi_mode == WIFI_MODE_AP) || (apsta_cfg->wifi_mode == WIFI_MODE_APSTA)) {
        if (configure_ap(apsta_cfg, netif_hostname) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure WiFi AP");
            result = ESP_FAIL;
        }
    }

    if ((apsta_cfg->wifi_mode == WIFI_MODE_STA) || (apsta_cfg->wifi_mode == WIFI_MODE_APSTA)) {
        if (configure_sta(apsta_cfg, netif_hostname) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure WiFi STA");
            result = ESP_FAIL;
        }
    }

    return result;
}


esp_err_t start_sta_connect_task(void)
{
    BaseType_t result = xTaskCreate(wifi_sta_connect_task, "wifi_sta_connect_task", WIFI_STA_CONNECT_TASK_STACK_SIZE,
                                    NULL, WIFI_STA_CONNECT_TASK_PRIORITY, &wifi_ctx.sta_conn_task_handle);
    if (result != pdPASS) {
        wifi_ctx.sta_conn_task_handle = NULL;
        ESP_LOGE(TAG, "Failed to create WiFi STA connect task");
        return ESP_FAIL;
    }

    xEventGroupWaitBits(wifi_ctx.sta_event_group, WIFI_STA_CONN_TASK_STARTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    return ESP_OK;
}


void stop_sta_connect_task(void)
{
    ESP_LOGD(TAG, "Waiting for WiFi STA connect task finished...");
    xEventGroupSetBits(wifi_ctx.sta_event_group, WIFI_STA_CONN_TASK_EXIT_REQ_BIT);
    xEventGroupWaitBits(wifi_ctx.sta_event_group, WIFI_STA_CONN_TASK_FINISHED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    // Clear all event bits
    xEventGroupClearBits(wifi_ctx.sta_event_group, 0x00FFFFFF);
}


esp_err_t wifi_init_apsta(wifi_apsta_config_t* apsta_cfg, char* netif_hostname)
{
    if (wifi_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_NOT_ALLOWED;
    }

    if (apsta_cfg->wifi_mode > WIFI_MODE_APSTA) {
        ESP_LOGE(TAG, "Invalid wifi mode");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_ctx.connect_scan_mutex = xSemaphoreCreateMutex();
    if (wifi_ctx.connect_scan_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi connect/scan mutex");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_ctx.sta_event_group = xEventGroupCreate();
    if (wifi_ctx.sta_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi STA event group");
        return ESP_FAIL;
    }

    wifi_ctx.netif_ap = esp_netif_create_default_wifi_ap();
    if (wifi_ctx.netif_ap == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi AP network interface");
        return ESP_FAIL;
    }

    wifi_ctx.netif_sta = esp_netif_create_default_wifi_sta();
    if (wifi_ctx.netif_sta == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi STA network interface");
        return ESP_FAIL;
    }

    ESP_RETURN_ON_FALSE(esp_wifi_init(&cfg) == ESP_OK, ESP_FAIL, TAG, "Failed to init WiFi");

    wifi_mode_t real_mode = get_real_mode(apsta_cfg->wifi_mode);
    ESP_RETURN_ON_FALSE(esp_wifi_set_mode(real_mode) == ESP_OK, ESP_FAIL, TAG, "Failed to set WiFi mode");

    ESP_RETURN_ON_FALSE(configure_ap_sta(apsta_cfg, netif_hostname) == ESP_OK, ESP_FAIL, TAG, "Failed to configure WiFi AP/STA");
    ESP_RETURN_ON_FALSE(esp_wifi_start() == ESP_OK, ESP_FAIL, TAG, "Failed to start WiFi");

    wifi_ctx.config_mode = apsta_cfg->wifi_mode;
    wifi_ctx.real_mode = real_mode;

    if (start_sta_connect_task() != ESP_OK) {
        return ESP_FAIL;
    }

    wifi_ctx.initialized = true;

    return ESP_OK;
}


esp_err_t wifi_set_apsta_config(wifi_apsta_config_t* apsta_cfg, char* netif_hostname)
{
    if (!wifi_ctx.initialized) {
        ESP_LOGW(TAG, "WiFi is not initialized");
        return ESP_ERR_NOT_ALLOWED;
    }

    if (apsta_cfg->wifi_mode > WIFI_MODE_APSTA) {
        ESP_LOGE(TAG, "Invalid wifi mode");
        return ESP_ERR_INVALID_ARG;
    }

    if (esp_wifi_stop() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop WiFi");
        // Try to do next steps
    }

    stop_sta_connect_task();

    wifi_mode_t real_mode = get_real_mode(apsta_cfg->wifi_mode);
    if (real_mode != wifi_ctx.real_mode) {
        esp_err_t ret = esp_wifi_set_mode(real_mode);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to change WiFi mode");
            return ret;
        }
    }

    configure_ap_sta(apsta_cfg, netif_hostname);

    ESP_RETURN_ON_FALSE(esp_wifi_start() == ESP_OK, ESP_FAIL, TAG, "Failed to start WiFi");

    wifi_ctx.config_mode = apsta_cfg->wifi_mode;
    wifi_ctx.real_mode = real_mode;

    esp_err_t ret = start_sta_connect_task();

    return ret;
}


esp_err_t wifi_sta_connect_scan_lock(void)
{
    if (wifi_ctx.connect_scan_mutex == NULL) {
        return ESP_FAIL;
    }
    xSemaphoreTake(wifi_ctx.connect_scan_mutex, portMAX_DELAY);
    return ESP_OK;
}


esp_err_t wifi_sta_connect_scan_unlock(void)
{
    if (wifi_ctx.connect_scan_mutex == NULL) {
        return ESP_FAIL;
    }
    xSemaphoreGive(wifi_ctx.connect_scan_mutex);
    return ESP_OK;
}
