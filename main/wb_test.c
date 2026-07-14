#include "esp_err.h"
#include <esp_http_server.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "auth.h"
#include "json_utils.h"
#include "bridge/port_manager.h"
#include "esp_log.h"
#include "indication.h"
#include "rs485_control.h"
#include "update_rs485_mio_gpio_states.h"


#define BRIDGE_PORT_INDEX       0
#define BRIDGE_PORT_INDEX_2     1   // RS-485-2 (UART2); freed so LEDC can reuse its TX pin (GPIO14)

#define CLK_OUT_PIN             GPIO_NUM_10
#define CLK_OUT_FREQ_HZ         100000
#define CLK_OUT_PWM_CHANNEL     LEDC_CHANNEL_0
#define CLK_OUT_PWM_TIMER       LEDC_TIMER_0

// Second 100 kHz output on the RS-485-2 UART2 TX line (GPIO14). Shares
// CLK_OUT_PWM_TIMER with the RS-485-1 output, so both ports carry the same
// waveform and both activity LEDs blink in lockstep.
#define CLK_OUT_PIN_2           GPIO_NUM_14
#define CLK_OUT_PWM_CHANNEL_2   LEDC_CHANNEL_1

// RS-485 transceiver driver-enable (DE/RE) pins. Both must be driven HIGH for the
// square wave to actually reach the RS-485 bus: with DE low the TX pin only toggles
// on the logic side, so the activity LED lights but nothing is emitted on the line.
// CLK_OUT_EN_PIN   = SERIAL_IO_PIN_1 (RS-485-1 DE/RE).
// CLK_OUT_EN_PIN_2 = SERIAL_IO_PIN_2 (RS-485-2 DE/RE, U4.DE). The RS-485-2 bus is
//   shared with the MIO transceiver U10, which stays idle here because both ports
//   are held DISABLED for the duration of the test.
#define CLK_OUT_EN_PIN          GPIO_NUM_4
#define CLK_OUT_EN_PIN_2        GPIO_NUM_15

#define CLK_OUT_JSON_FIELD      "clock_out"


static bool clock_out_en = false;

static ledc_timer_config_t timer_config = {
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .duty_resolution = 1,
    .timer_num = CLK_OUT_PWM_TIMER,
    .freq_hz = CLK_OUT_FREQ_HZ,
    .clk_cfg = LEDC_USE_APB_CLK,
    .deconfigure = false
};

static ledc_channel_config_t channel_config = {
    .gpio_num = CLK_OUT_PIN,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .channel = CLK_OUT_PWM_CHANNEL,
    .intr_type = LEDC_INTR_DISABLE,
    .timer_sel = CLK_OUT_PWM_TIMER,
    .duty = 1, // 50% output duty for 1-bit resolution
    .hpoint = 0,
    .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
    .flags.output_invert = 0
};

static ledc_channel_config_t channel_config_2 = {
    .gpio_num = CLK_OUT_PIN_2,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .channel = CLK_OUT_PWM_CHANNEL_2,
    .intr_type = LEDC_INTR_DISABLE,
    .timer_sel = CLK_OUT_PWM_TIMER,
    .duty = 1, // 50% output duty for 1-bit resolution (matches the RS-485-1 channel)
    .hpoint = 0,
    .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
    .flags.output_invert = 0
};

static const char* TAG = "wb_test";


// Put a DE/RE pin into a driven-LOW output state (transceiver in receive mode).
// The level is latched BEFORE the pin becomes an output: gpio_reset_pin() does not
// clear the output latch (GPIO_OUT_REG), so on the second and later runs of the test
// the latch still holds whatever the previous owner left there — the UART leaves it
// HIGH after driving half-duplex direction control. Enabling the output driver first
// would then briefly assert DE (on RS-485-2 that is a pulse onto the bus shared with
// the MIO transceiver). Same order as serial_set_tx_disabled() in serial.c.
static void de_pin_latch_low_output(gpio_num_t pin)
{
    gpio_reset_pin(pin);
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}


static void start_clock_out(void)
{
    // Keep both transceivers in receive mode until the waveform is running.
    de_pin_latch_low_output(CLK_OUT_EN_PIN);
    de_pin_latch_low_output(CLK_OUT_EN_PIN_2);

    ledc_timer_config_t tim_conf = timer_config;
    tim_conf.deconfigure = false;
    ledc_timer_config(&tim_conf);

    ledc_channel_config_t ch_conf = channel_config;
    ledc_channel_config(&ch_conf);

    // RS-485-2: drive UART2 TX (GPIO14) with the same 100 kHz timer.
    ledc_channel_config_t ch_conf2 = channel_config_2;
    ledc_channel_config(&ch_conf2);

    // Enable both line drivers so the square wave reaches both RS-485 buses.
    gpio_set_level(CLK_OUT_EN_PIN, 1);
    gpio_set_level(CLK_OUT_EN_PIN_2, 1);

    ESP_LOGW(TAG, "100 kHz clock output on RS485-1/RS485-2 TX enabled (all indicator LEDs on)");
}


static void stop_clock_out(void)
{
    // Disable both line drivers before tearing the waveform down.
    gpio_set_level(CLK_OUT_EN_PIN, 0);
    gpio_set_level(CLK_OUT_EN_PIN_2, 0);

    ledc_stop(channel_config.speed_mode, channel_config.channel, 0);
    ledc_stop(channel_config_2.speed_mode, channel_config_2.channel, 0);
    ledc_timer_pause(timer_config.speed_mode, timer_config.timer_num);

    ledc_timer_config_t tim_conf = timer_config;
    tim_conf.deconfigure = true;
    ledc_timer_config(&tim_conf);

    // Release all four pins so port_manager_apply_settings() can hand the TX and
    // DE lines back to the UART. The DE pins were actively driven LOW just above,
    // so nothing is asserted while they float back to their default input state;
    // start_clock_out() re-latches them LOW anyway rather than trusting the latch
    // left here, since the UART owns these pins in between two runs of the test.
    gpio_reset_pin(CLK_OUT_PIN);
    gpio_reset_pin(CLK_OUT_PIN_2);
    gpio_reset_pin(CLK_OUT_EN_PIN);
    gpio_reset_pin(CLK_OUT_EN_PIN_2);

    ESP_LOGW(TAG, "100 kHz clock output on RS485-1/RS485-2 TX disabled");
}


static esp_err_t process_request_json(cJSON *request_json)
{
    if (request_json == NULL) {
        return ESP_FAIL;
    }

    // Check if command field exists
    if (!cJSON_HasObjectItem(request_json, CLK_OUT_JSON_FIELD)) {
        ESP_LOGW(TAG, "Field '%s' not found in request", CLK_OUT_JSON_FIELD);
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *cmd_item = cJSON_GetObjectItem(request_json, CLK_OUT_JSON_FIELD);
    if (!cJSON_IsBool(cmd_item)) {
        ESP_LOGW(TAG, "Field '%s' value is not boolean", CLK_OUT_JSON_FIELD);
        return ESP_ERR_INVALID_ARG;
    }

    if (cmd_item->valueint) {
        if (!clock_out_en) {
            clock_out_en = true;
            // Freeze the ports first: from here on the runtime mode (DISABLED)
            // deliberately differs from the mode in NVS, and nothing but this test
            // may re-init the ports while the LEDC drives their TX/DE pins.
            port_manager_set_ports_frozen(true);
            // Disable both ports so the LEDC can take over their TX pins, but do NOT
            // persist the DISABLED mode: NVS must keep the user's configured mode so
            // that losing power during the test cannot wipe the port configuration.
            // The exit path below restores the ports straight from NVS.
            esp_err_t err = port_manager_set_mode_transient(BRIDGE_PORT_INDEX, PM_MODE_DISABLED);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to disable port for clock_out: %s", esp_err_to_name(err));
            }
            esp_err_t err2 = port_manager_set_mode_transient(BRIDGE_PORT_INDEX_2, PM_MODE_DISABLED);
            if (err2 != ESP_OK) {
                ESP_LOGE(TAG, "Failed to disable port 2 for clock_out: %s", esp_err_to_name(err2));
            }
            start_clock_out();
            // Factory test: light all LEDs simultaneously with the test signal.
            indication_set_test_all_leds(true);
            // Also lights the V-out LED (energises RS-485 bus V-out).
            esp_err_t vout_err = rs485_bus_vout_on_off(true);
            if (vout_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to enable V-out for clock_out test: %s", esp_err_to_name(vout_err));
            }
        }
    } else {
        if (clock_out_en) {
            clock_out_en = false;
            stop_clock_out();
            // The LEDC has released the TX/DE pins, so the ports may be brought up
            // again: release the freeze before apply_settings, which is a no-op
            // while the ports are frozen.
            port_manager_set_ports_frozen(false);
            // The test never touched NVS, so the configured mode is still there:
            // re-read it and re-initialise both ports from the persisted settings.
            // This also picks up any settings written while the test was running.
            esp_err_t err = port_manager_apply_settings(BRIDGE_PORT_INDEX);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to restore port mode after clock_out: %s", esp_err_to_name(err));
            }
            esp_err_t err2 = port_manager_apply_settings(BRIDGE_PORT_INDEX_2);
            if (err2 != ESP_OK) {
                ESP_LOGE(TAG, "Failed to restore port 2 mode after clock_out: %s", esp_err_to_name(err2));
            }
            // Factory test: return LEDs to normal indication and restore V-out state.
            indication_set_test_all_leds(false);
            update_rs485_control();         // restore V-out to the configured KEY_485_VOUT state
        }
    }

    return ESP_OK;
}


static void fill_response_json(cJSON *response_json)
{
    cJSON_AddBoolToObject(response_json, CLK_OUT_JSON_FIELD, clock_out_en);
}


esp_err_t wb_test_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WB Test GET request received");

    if (!auth_middleware_check(req)) {
        // Func will send 401 Unauthorized if auth fails
        return ESP_OK;
    }

    // Create success response
    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        return json_utils_send_error(req, "Failed to create response");
    }

    fill_response_json(response_json);
    json_utils_send_response(req, NULL, response_json);

    return ESP_OK;
}


esp_err_t wb_test_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WB Test POST request received");

    if (!auth_middleware_check(req)) {
        // Func will send 401 Unauthorized if auth fails
        return ESP_OK;
    }

    cJSON *request_json = json_utils_receive_json(req);
    if (request_json == NULL) {
        return json_utils_send_error(req, "Invalid request JSON");
    }

    esp_err_t res = process_request_json(request_json);
    if (res != ESP_OK) {
        json_utils_cleanup(request_json, NULL);
        if (res == ESP_ERR_NOT_FOUND) {
            return json_utils_send_error(req, "Field 'clock_out' not found in request");
        } else if (res == ESP_ERR_INVALID_ARG) {
            return json_utils_send_error(req, "Incorrect command field value");
        } else {
            return json_utils_send_error(req, "Failed to process request");
        }
    }

    // Create success response
    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        json_utils_cleanup(request_json, NULL);
        return json_utils_send_error(req, "Failed to create response");
    }

    cJSON_AddBoolToObject(response_json, "success", true);
    fill_response_json(response_json);

    json_utils_send_response(req, request_json, response_json);

    return ESP_OK;
}
