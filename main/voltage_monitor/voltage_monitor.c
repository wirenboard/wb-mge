#include "voltage_monitor.h"
#include "system_voltage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"


#define VM_TASK_STACK_SIZE                  4096
#define VM_TASK_PRIORITY                    13

#define VM_TASK_PERIOD_MS                   2       // Sample rate period
#define VM_EXP_FILTER_RC_TIME_MS            10      // Exponential filter characteristic time

#define SYS_VOLTAGE_MIN_OK                  8.0f
#define SYS_VOLTAGE_MIN_FAIL                7.5f

#define SYS_VOLTAGE_MAX_OK                  28.0f
#define SYS_VOLTAGE_MAX_FAIL                29.0f

#define SYS_VOLTAGE_PROT_RELEASE_DELAY_MS   2000    // Delay before protection release

// More safety to prevent 0 time argument for vTaskDelay()
#define MAX(a, b)                           ((a) > (b) ? (a) : (b))
#define VM_TASK_ACTUAL_PERIOD_MS            MAX(pdTICKS_TO_MS(1), VM_TASK_PERIOD_MS)

#define EXP_FILTER_ALPHA_COEF               ((float)VM_TASK_ACTUAL_PERIOD_MS / (float)(VM_EXP_FILTER_RC_TIME_MS + VM_TASK_ACTUAL_PERIOD_MS))


typedef struct {
    float sys_voltage;
    bool sys_voltage_is_ok;
    voltage_monitor_callback_t sys_voltage_callback_fn;
} vm_ctx_t;

static vm_ctx_t vm_ctx = {0};

static TaskHandle_t task_handle = NULL;
static SemaphoreHandle_t vm_ctx_mutex = NULL;

static const char* TAG = "voltage_monitor";


static float exp_filter(float cur_value, float new_value)
{
    float value = cur_value + EXP_FILTER_ALPHA_COEF * (new_value - cur_value);
    return value;
}


static bool check_sys_voltage_bounds(float sys_voltage, bool ok_state)
{
    if (ok_state) {
        if ((sys_voltage < SYS_VOLTAGE_MIN_FAIL) || (sys_voltage > SYS_VOLTAGE_MAX_FAIL)) {
            ok_state = false;
        }
    } else {
        if ((sys_voltage >= SYS_VOLTAGE_MIN_OK) && (sys_voltage <= SYS_VOLTAGE_MAX_OK)) {
            ok_state = true;
        }
    }

    return ok_state;
}


static bool sys_voltage_prot_engine(bool bounds_ok)
{
    static bool initialized = false;
    static bool prot_state = false;
    static bool release_wait = false;
    static TickType_t time_stamp = 0;

    if (!initialized) {
        prot_state = bounds_ok;
        release_wait = false;
        time_stamp = xTaskGetTickCount();
        initialized = true;
    }

    if (!bounds_ok) {
        prot_state = false;
        release_wait = false;
        return prot_state;
    }

    if (!prot_state) {
        if (!release_wait) {
            time_stamp = xTaskGetTickCount();
            release_wait = true;
        } else {
            if ((xTaskGetTickCount() - time_stamp) >= pdMS_TO_TICKS(SYS_VOLTAGE_PROT_RELEASE_DELAY_MS)) {
                prot_state = true;
                release_wait = false;
            }
        }
    }

    return prot_state;
}


static void voltage_monitor_task(void *arg)
{
    while (1) {
        float new_value = system_voltage_read();

        xSemaphoreTake(vm_ctx_mutex, portMAX_DELAY);
        vm_ctx.sys_voltage = exp_filter(vm_ctx.sys_voltage, new_value);
        bool bounds_ok = check_sys_voltage_bounds(vm_ctx.sys_voltage, vm_ctx.sys_voltage_is_ok);
        bool old_sys_voltage_is_ok = vm_ctx.sys_voltage_is_ok;
        vm_ctx.sys_voltage_is_ok = sys_voltage_prot_engine(bounds_ok);
        xSemaphoreGive(vm_ctx_mutex);

        if (old_sys_voltage_is_ok != vm_ctx.sys_voltage_is_ok) {
            if (vm_ctx.sys_voltage_is_ok) {
                ESP_LOGD(TAG, "System voltage protection FAIL -> OK, voltage: %.2f V", vm_ctx.sys_voltage);
            } else {
                ESP_LOGD(TAG, "System voltage protection OK -> FAIL, voltage: %.2f V", vm_ctx.sys_voltage);
            }
            if (vm_ctx.sys_voltage_callback_fn != NULL) {
                vm_ctx.sys_voltage_callback_fn(vm_ctx.sys_voltage, vm_ctx.sys_voltage_is_ok);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(VM_TASK_ACTUAL_PERIOD_MS));
    }
}


esp_err_t voltage_monitor_init(voltage_monitor_callback_t callback_fn)
{
    if (task_handle != NULL) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    esp_err_t res = system_voltage_init();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize system voltage metering unit");
        return ESP_FAIL;
    }

    vm_ctx_mutex = xSemaphoreCreateMutex();
    if (vm_ctx_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create system voltage mutex");
        return ESP_FAIL;
    }

    // Get initial voltage value
    xSemaphoreTake(vm_ctx_mutex, portMAX_DELAY);
    vm_ctx.sys_voltage = system_voltage_read();
    vm_ctx.sys_voltage_is_ok = check_sys_voltage_bounds(vm_ctx.sys_voltage, false);
    xSemaphoreGive(vm_ctx_mutex);

    vm_ctx.sys_voltage_callback_fn = callback_fn;

    if (!vm_ctx.sys_voltage_is_ok) {
        if (vm_ctx.sys_voltage_callback_fn != NULL) {
            vm_ctx.sys_voltage_callback_fn(vm_ctx.sys_voltage, vm_ctx.sys_voltage_is_ok);
        }
    }

    BaseType_t result = xTaskCreate(voltage_monitor_task, "voltage_monitor_task", VM_TASK_STACK_SIZE, NULL, VM_TASK_PRIORITY, &task_handle);
    if (result != pdPASS) {
        vSemaphoreDelete(vm_ctx_mutex);
        vm_ctx_mutex = NULL;
        ESP_LOGE(TAG, "Failed to create voltage monitoring task");
        return ESP_FAIL;
    }

    return ESP_OK;
}


float voltage_monitor_get_sys_voltage(void)
{
    if (vm_ctx_mutex == NULL) {
        ESP_LOGW(TAG, "Not initialized");
        return 0.0f;
    }

    xSemaphoreTake(vm_ctx_mutex, portMAX_DELAY);
    float value = vm_ctx.sys_voltage;
    xSemaphoreGive(vm_ctx_mutex);

    return value;
}


bool voltage_monitor_sys_voltage_is_ok(void)
{
    if (vm_ctx_mutex == NULL) {
        ESP_LOGW(TAG, "Not initialized");
        return false;
    }

    xSemaphoreTake(vm_ctx_mutex, portMAX_DELAY);
    bool is_ok = vm_ctx.sys_voltage_is_ok;
    xSemaphoreGive(vm_ctx_mutex);

    return is_ok;
}
