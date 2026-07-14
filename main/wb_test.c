#include "esp_err.h"
#include <esp_http_server.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "auth.h"
#include "board_pins.h"
#include "json_utils.h"
#include "bridge/port_manager.h"
#include "esp_log.h"
#include "indication.h"
#include "mio_control.h"
#include "rs485_control.h"
#include "update_rs485_mio_gpio_states.h"


// The clock-out test drives the TX lines of both serial ports and the DE/RE line of
// port 1 directly. The pins come from board_pins.h (SERIAL_{OUTPUT,IO}_PIN_{1,2}) — the
// GPIO numbers differ per board, and hardcoding the WB-MGE ones drove the wrong pins on
// WB-MGU (there GPIO4 is an input, the port-2 RX line).
#define BRIDGE_PORT_INDEX       0   // port 1; freed so LEDC can reuse its TX pin
#define BRIDGE_PORT_INDEX_2     1   // port 2; freed so LEDC can reuse its TX pin

#define CLK_OUT_PIN             SERIAL_OUTPUT_PIN_1
#define CLK_OUT_FREQ_HZ         100000
#define CLK_OUT_PWM_CHANNEL     LEDC_CHANNEL_0
#define CLK_OUT_PWM_TIMER       LEDC_TIMER_0

// Second 100 kHz output, on the port-2 TX line — the logic-side DI input of the RS-485-2
// transceiver. Shares CLK_OUT_PWM_TIMER with the port-1 output, so both ports carry the
// same waveform and both activity LEDs blink in lockstep: on WB-MGE the RS-485-2 activity
// LED (LED2) is tapped from that DI line via R36 and lights regardless of DE.
#define CLK_OUT_PIN_2           SERIAL_OUTPUT_PIN_2
#define CLK_OUT_PWM_CHANNEL_2   LEDC_CHANNEL_1

// Transceiver driver-enable (DE/RE) pin — port 1 ONLY. Driving it HIGH is what makes the
// square wave actually reach the bus: with DE low the TX pin only toggles on the logic
// side, so the activity LED lights but nothing is emitted on the line.
//
// The RS-485-2 transceiver driver is intentionally left DISABLED, and SERIAL_IO_PIN_2 is
// intentionally not configured here at all: on WB-MGE U4.DE (GPIO15) is held low by the
// hardware pulldown R4, so no signal is emitted onto the RS-485-2 bus — which is shared
// with the MIO transceiver U10 and wired out to the external RS-485-2 terminals. Driving
// that pair would put the factory meander on a bus we do not own, in front of whatever is
// wired to the terminals and alongside a live MIO controller (its reset is an expander
// pin, not a UART pin, so disabling port 2 does not silence it). Review comment #30
// ("emit the 100 kHz on the second RS-485 too, i.e. raise GPIO15") was considered and
// DECLINED for exactly that reason: LED2 only needs the DI line, which we do drive.
// Leaving the pin unconfigured leaves it to R4, which is its real owner.
#define CLK_OUT_EN_PIN          SERIAL_IO_PIN_1

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
// would then briefly assert DE and put a glitch on the RS-485-1 line. Same order as
// serial_set_tx_disabled() in serial.c.
static void de_pin_latch_low_output(gpio_num_t pin)
{
    gpio_reset_pin(pin);
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}


// Tear the LEDC down and release the three pins the test owns (the TX line of both
// ports plus the port-1 DE line). Also used to roll back a half-configured LEDC when
// start_clock_out() fails midway: the ledc_* calls simply report an error for a
// channel/timer that was never set up.
static void release_clock_out_hw(void)
{
    // Disable the RS-485-1 line driver before tearing the waveform down.
    gpio_set_level(CLK_OUT_EN_PIN, 0);

    ledc_stop(channel_config.speed_mode, channel_config.channel, 0);
    ledc_stop(channel_config_2.speed_mode, channel_config_2.channel, 0);
    ledc_timer_pause(timer_config.speed_mode, timer_config.timer_num);

    ledc_timer_config_t tim_conf = timer_config;
    tim_conf.deconfigure = true;
    ledc_timer_config(&tim_conf);

    // Release all three pins so port_manager_apply_settings() can hand the TX and DE
    // lines back to the UART. Note that gpio_reset_pin() does not leave a pin floating:
    // it puts the pad in GPIO_MODE_DISABLE with the internal pull-up ON, so the released
    // port-1 DE pin is weakly pulled towards 1 — the "driver enabled" level — and no
    // external pulldown is documented for that line on any board, so its driver may be
    // weakly enabled until the UART re-attaches. What bounds the exposure is the window
    // itself — it is short (apply_settings re-inits the UART right after) and the released
    // TX pin is pulled to the idle level the UART holds between frames, so a briefly-enabled
    // driver idles the line rather than corrupting traffic. On the way back in,
    // start_clock_out() re-latches the DE pin LOW before driving it rather than trusting
    // whatever the previous owner left in the output latch.
    //
    // The port-2 DE line is not in this list because the test never took it: it is owned
    // by the hardware pulldown (R4 on WB-MGE) throughout, so nothing has to be given back.
    gpio_reset_pin(CLK_OUT_PIN);
    gpio_reset_pin(CLK_OUT_PIN_2);
    gpio_reset_pin(CLK_OUT_EN_PIN);
}


// Bring the 100 kHz waveform up on the TX line of both ports and enable the RS-485-1
// line driver. The DE pin is raised ONLY once every LEDC call has succeeded: if the
// waveform never started, enabling the driver would put the transceiver into transmit
// with a STATIC level on the RS-485-1 line while the API happily reported success. On
// any error the half-configured LEDC is released and the DE line stays LOW (receive
// mode); the caller aborts the test entry.
//
// Port 2 gets the waveform on its TX (DI) line only — its driver stays disabled, so the
// RS-485-2 pair stays silent (see CLK_OUT_EN_PIN above).
static esp_err_t start_clock_out(void)
{
    // Keep the RS-485-1 transceiver in receive mode until the waveform is running.
    de_pin_latch_low_output(CLK_OUT_EN_PIN);

    ledc_timer_config_t tim_conf = timer_config;
    tim_conf.deconfigure = false;
    esp_err_t err = ledc_timer_config(&tim_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clock_out: ledc_timer_config failed: %s", esp_err_to_name(err));
        release_clock_out_hw();
        return err;
    }

    ledc_channel_config_t ch_conf = channel_config;
    err = ledc_channel_config(&ch_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clock_out: ledc_channel_config for RS485-1 TX failed: %s", esp_err_to_name(err));
        release_clock_out_hw();
        return err;
    }

    // Port 2: drive its TX (DI) line with the same 100 kHz timer, for LED2 only.
    ledc_channel_config_t ch_conf2 = channel_config_2;
    err = ledc_channel_config(&ch_conf2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clock_out: ledc_channel_config for RS485-2 TX failed: %s", esp_err_to_name(err));
        release_clock_out_hw();
        return err;
    }

    // The waveform is running: enable the RS-485-1 line driver so it reaches that bus.
    // The RS-485-2 driver stays off — its bus is not ours to drive.
    gpio_set_level(CLK_OUT_EN_PIN, 1);

    ESP_LOGW(TAG, "100 kHz clock output on RS485-1/RS485-2 TX enabled (all indicator LEDs on)");
    return ESP_OK;
}


static void stop_clock_out(void)
{
    release_clock_out_hw();
    ESP_LOGW(TAG, "100 kHz clock output on RS485-1/RS485-2 TX disabled");
}


// Roll back a failed clock_out entry: the LEDC was never started, so the ports can be
// unfrozen and brought straight back up from NVS (which also undoes any port this
// attempt did manage to disable transiently), and the I/O bus is put back to whatever
// io_bus_enabled says. Leaves the device exactly as it was before the request.
static void abort_clock_out_entry(void)
{
    port_manager_set_ports_frozen(false);
    port_manager_apply_settings(BRIDGE_PORT_INDEX);
    port_manager_apply_settings(BRIDGE_PORT_INDEX_2);
    update_io_bus_control();
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
            if (err != ESP_OK || err2 != ESP_OK) {
                // A port that could not be disabled was rolled back to its previous
                // working mode by port_manager, i.e. its UART still owns the TX/DE
                // pins. Starting the LEDC on top of that would have two drivers on the
                // same pins, so abort the test entirely: unfreeze, restore both ports
                // from NVS (undoing the one that did get disabled) and fail the request.
                ESP_LOGE(TAG, "clock_out aborted: the RS-485 ports could not be disabled");
                abort_clock_out_entry();
                return ESP_ERR_INVALID_STATE;
            }
            // Hold the MIO controller in reset: it hangs off the RS-485-2 pair, and
            // disabling port 2 does nothing to it (its reset is an expander pin, not a
            // UART pin). Restored from io_bus_enabled on exit.
            esp_err_t io_bus_err = mio_control_io_bus_onoff(false);
            if (io_bus_err != ESP_OK) {
                // Treat it like a port that could not be disabled and abort the entry.
                ESP_LOGE(TAG, "clock_out aborted: the I/O bus could not be disabled: %s",
                         esp_err_to_name(io_bus_err));
                abort_clock_out_entry();
                return ESP_ERR_INVALID_STATE;
            }
            // Both ports are down and the pins are free: start the waveform.
            esp_err_t clk_err = start_clock_out();
            if (clk_err != ESP_OK) {
                // The LEDC never came up, so start_clock_out() left the DE line LOW and
                // released the pins. Reporting success here would leave the factory tester
                // with a device that claims to emit a clock but does not. Abort the entry:
                // unfreeze, restore both ports from NVS and put the I/O bus back.
                ESP_LOGE(TAG, "clock_out aborted: the LEDC could not be set up");
                abort_clock_out_entry();
                return ESP_ERR_INVALID_STATE;
            }
            // The test is now on.
            clock_out_en = true;
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
            update_io_bus_control();        // restore MIO to the configured KEY_IO_BUS_ENABLED state
                                            // (re-applies the setting, so an I/O bus that was
                                            // already off before the test stays off)
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
        } else if (res == ESP_ERR_INVALID_STATE) {
            // The RS-485 ports or the I/O bus could not be freed, or the LEDC refused to
            // produce the waveform, so the test never started and the entry was rolled
            // back — 503: the request was valid, the device could not serve it.
            return json_utils_send_error_status(req, "503 Service Unavailable",
                "Cannot start clock_out test: the RS-485 ports, the I/O bus or the clock generator could not be set up");
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
