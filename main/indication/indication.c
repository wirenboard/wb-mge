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

#define NETWORK_LED_OFF_TIME_MS         50
#define NETWORK_LED_ON_TIME_MS          50

#define COUNT_UNLIMITED                 0xFFFFFFFF

//------------------------------------------------------------------------------

typedef struct {
    unsigned long time_on_ms;
    unsigned long time_off_ms;
    unsigned long count_blink_on_ms;
    unsigned long count_blink_off_ms;
    unsigned blink_counter;
} status_led_ctx_t;

//------------------------------------------------------------------------------

static const char* TAG = "indication";

static status_led_ctx_t status_led_ctx = {0};
static bool indication_initialized = 0;

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


static void blinking_led_control(bool* led_state, unsigned long* time_stamp, unsigned* counter,
                                unsigned long sys_time, unsigned long t_on, unsigned long t_off)
{
    unsigned long delta_time = sys_time - *time_stamp;

    if (!*counter || !t_on) {
        *led_state = 0;
        *time_stamp = sys_time;
        return;
    }

    if (!*led_state) {
        if (delta_time > t_on) {
            *led_state = 1;
            *time_stamp += delta_time;
        }
    } else {
        if (delta_time > t_off) {
            *led_state = 0;
            *time_stamp += delta_time;
            if (*counter != COUNT_UNLIMITED) {
                (*counter)--;
            }
        }
    }
}

//------------------------------------------------------------------------------

static void status_led_control(unsigned long sys_time)
{
    static unsigned unlimited_count = COUNT_UNLIMITED;
    static bool init = 1;
    static unsigned long time_stamp = 0;
    static bool led_state = 0;

    if (init) {
        time_stamp = sys_time;
        led_state = 0;
        init = 0;
    }

    if (status_led_ctx.blink_counter) { // Especial count blinking
        blinking_led_control(&led_state, &time_stamp, &status_led_ctx.blink_counter, sys_time,
                            status_led_ctx.count_blink_on_ms, status_led_ctx.count_blink_off_ms);
    } else { // Regular blinking
        blinking_led_control(&led_state, &time_stamp, &unlimited_count, sys_time,
                            status_led_ctx.time_on_ms, status_led_ctx.time_off_ms);
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
    if (indication_initialized) {
        return ESP_OK;  // Already initialized
    }

    if (!io_expander_handle) {
        ESP_LOGE(TAG, "IO expander handle is NULL");
        return ESP_FAIL;
    }

    leds_control_init(io_expander_handle);

    // Status led context (init only minimum necessary fields)
    status_led_ctx.time_on_ms = 0;
    status_led_ctx.count_blink_on_ms = 0;
    status_led_ctx.blink_counter = 0;

    xTaskCreate(indication_task, "indication_task", INDICATION_TASK_STACK_SIZE, NULL, INDICATION_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Indication initialized");
    indication_initialized = 1;

    return ESP_OK;
}

//------------------------------------------------------------------------------

void indication_status_led_blink(unsigned period_ms)
{
    status_led_ctx.time_on_ms = period_ms / 2;
    status_led_ctx.time_off_ms = period_ms - status_led_ctx.time_on_ms;
}

//------------------------------------------------------------------------------

void indication_status_led_count_blink(unsigned period_ms, unsigned count)
{
    status_led_ctx.blink_counter = 0;
    status_led_ctx.count_blink_on_ms = period_ms / 2;
    status_led_ctx.count_blink_off_ms = period_ms - status_led_ctx.count_blink_on_ms;
    status_led_ctx.blink_counter = count;
}

//------------------------------------------------------------------------------
