#include "slot_relay.h"

#if defined(MODEL_mgu_v1)

#include "board_pins.h"
#include "port_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SLOT_RELAY_PIN SERIAL_IO_PIN_2

// A spinlock, not pm_lock: a request can arrive through the bridge of this very port, and
// port_deinit_mode() joins that bridge's task while holding pm_lock.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_owned = false;
static bool s_on = false;

void slot_relay_acquire(void)
{
    gpio_reset_pin(SLOT_RELAY_PIN);
    gpio_set_level(SLOT_RELAY_PIN, 0);
    gpio_set_direction(SLOT_RELAY_PIN, GPIO_MODE_OUTPUT);

    taskENTER_CRITICAL(&s_lock);
    s_on = false;
    s_owned = true;
    taskEXIT_CRITICAL(&s_lock);
}

void slot_relay_release(void)
{
    taskENTER_CRITICAL(&s_lock);
    s_on = false;
    s_owned = false;
    taskEXIT_CRITICAL(&s_lock);
}

esp_err_t slot_relay_set(bool on)
{
    esp_err_t ret = ESP_ERR_INVALID_STATE;

    taskENTER_CRITICAL(&s_lock);
    if (s_owned && (!port_manager_ports_frozen())) {
        gpio_set_level(SLOT_RELAY_PIN, on);
        s_on = on;
        ret = ESP_OK;
    }
    taskEXIT_CRITICAL(&s_lock);

    return ret;
}

esp_err_t slot_relay_get(bool *on)
{
    taskENTER_CRITICAL(&s_lock);
    *on = s_on;
    taskEXIT_CRITICAL(&s_lock);

    return ESP_OK;
}

#else

void slot_relay_acquire(void)
{
}

void slot_relay_release(void)
{
}

esp_err_t slot_relay_set(bool on)
{
    (void)on;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t slot_relay_get(bool *on)
{
    (void)on;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
