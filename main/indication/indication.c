#include "esp_io_expander_tca95xx_16bit.h"
#include "leds_control.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "ethernet.h"
#include "lwip/netif.h"
#include "lwip/stats.h"

//------------------------------------------------------------------------------

#define INDICATION_TASK_STACK_SIZE      2048
#define INDICATION_TASK_PRIORITY        1

#define STATUS_LED_BLINK_PERIOD_MS      1000

#define NETWORK_LED_OFF_TIME_MS         50
#define NETWORK_LED_ON_TIME_MS          50

//------------------------------------------------------------------------------

static const char* TAG = "indication";

//------------------------------------------------------------------------------

static struct netif* get_netif(const char* key)
{
    esp_netif_t* esp_netif = esp_netif_get_handle_from_ifkey(key);
    int index = esp_netif_get_netif_impl_index(esp_netif);
    struct netif* netif = NULL;
    if (index >= 0) {
        netif = netif_get_by_index(index);
    }
    if (netif) {
        ESP_LOGD(TAG, "Got netif with '%s' key, index: %d", key, index);
    } else {
        ESP_LOGW(TAG, "Unable to get netif with '%s' key, it seems that interface is disabled", key);
    }
    return netif;
}

//------------------------------------------------------------------------------

static void network_led_control(bool* led_state, unsigned long* time_stamp, int* state,
                                unsigned long sys_time, bool link, bool activity)
{
    if (!link) {
        *led_state = 0;
        *time_stamp = sys_time;
        *state = 0;
        return;
    }

    switch (*state) {
        case 0: {
            *led_state = 1;
            if (((sys_time - *time_stamp) >= NETWORK_LED_ON_TIME_MS) && activity) {
                *led_state = 0;
                *time_stamp = sys_time;
                *state = 1;
            }
            break;
        }
        case 1: {
            *led_state = 0;
            if ((sys_time - *time_stamp) >= NETWORK_LED_OFF_TIME_MS) {
                *led_state = 1;
                *time_stamp += NETWORK_LED_OFF_TIME_MS;
                *state = 0;
            }
            break;
        }
    }
}

//------------------------------------------------------------------------------

static void status_led_control(unsigned long sys_time)
{
    static const unsigned long t_on = STATUS_LED_BLINK_PERIOD_MS / 2;
    static const unsigned long t_off = STATUS_LED_BLINK_PERIOD_MS - t_on;
    static bool init = 1;
    static unsigned long time_stamp = 0;
    static bool led_state = 0;

    if (init) {
        time_stamp = sys_time;
        led_state = 0;
        init = 0;
    }

    unsigned long delta_time = sys_time - time_stamp;

    if (!led_state) {
        if (delta_time > t_on) {
            led_state = 1;
            time_stamp += delta_time;
        }
    } else {
        if (delta_time > t_off) {
            led_state = 0;
            time_stamp += delta_time;
        }
    }

    leds_control_set_status_led(led_state);
}

static void ethernet_led_control(unsigned long sys_time)
{
    static bool init = 1;
    static struct netif* netif = NULL;
    static unsigned long time_stamp = 0;
    static bool led_state = 0;
    static int state = 0;
    static u32_t rx_counter = 0;

    if (init) {
        netif = get_netif("ETH_DEF");
        time_stamp = sys_time;
        led_state = 0;
        state = 0;
        rx_counter = 0;
        init = 0;
    }

    if (netif) {
        bool link = netif_is_link_up(netif);
        bool activity = 0;
        if (rx_counter != netif->mib2_counters.ifinoctets) {
            activity = 1;
            rx_counter = netif->mib2_counters.ifinoctets;
        }
        network_led_control(&led_state, &time_stamp, &state, sys_time, link, activity);
    } else {
        led_state = 0;
    }

    leds_control_set_eth_led(led_state);
}

static void wifi_led_control(unsigned long sys_time)
{
    static bool init = 1;
    static struct netif* ap_netif = NULL;
    static struct netif* sta_netif = NULL;
    static unsigned long time_stamp = 0;
    static bool led_state = 0;
    static int state = 0;
    static u32_t ap_counter = 0;
    static u32_t sta_counter = 0;

    if (init) {
        ap_netif = get_netif("WIFI_AP_DEF");
        sta_netif = get_netif("WIFI_STA_DEF");
        time_stamp = sys_time;
        led_state = 0;
        state = 0;
        ap_counter = 0;
        sta_counter = 0;
        init = 0;
    }

    bool activity = 0;
    if (ap_netif && (ap_counter != ap_netif->mib2_counters.ifinoctets)) {
        ap_counter = ap_netif->mib2_counters.ifinoctets;
        activity = 1;
    }
    if (sta_netif && (sta_counter != sta_netif->mib2_counters.ifinoctets)) {
        sta_counter = sta_netif->mib2_counters.ifinoctets;
        activity = 1;
    }

    if (ap_netif || sta_netif) {
        network_led_control(&led_state, &time_stamp, &state, sys_time, 1, activity);
    } else {
        led_state = 0;
    }

    leds_control_set_wifi_led(led_state);
}

//------------------------------------------------------------------------------

static void indication_task(void *arg)
{
    ESP_LOGD(TAG, "Started indication_task()");

    while (1)
    {
        unsigned long sys_time = pdTICKS_TO_MS(xTaskGetTickCount());

        status_led_control(sys_time);
        ethernet_led_control(sys_time);
        wifi_led_control(sys_time);

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

//------------------------------------------------------------------------------

esp_err_t indication_init(esp_io_expander_handle_t io_expander_handle)
{
    if (!io_expander_handle) {
        ESP_LOGE(TAG, "IO expander handle is NULL");
        return ESP_FAIL;
    }

    leds_control_init(io_expander_handle);
    xTaskCreate(indication_task, "indication_task", INDICATION_TASK_STACK_SIZE, NULL, INDICATION_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Indication initialized");

    return ESP_OK;
}

//------------------------------------------------------------------------------
