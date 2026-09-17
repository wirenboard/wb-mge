#include "esp_err.h"
#include <esp_http_server.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "auth.h"
#include "board_pins.h"
#include "json_utils.h"
#include "esp_log.h"
#include "indication.h"
#include "mb_role.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rs485_control.h"
#include "settings_update.h"
#include "update_rs485_mio_gpio_states.h"
#include "wb_test.h"

// The Modbus stack of the built role. This test takes the two TX lines and the two DE
// lines away from whoever holds them, and in BOTH the slave and the master role that is an
// esp-modbus instance per port holding an installed UART driver — so the test has to stop
// the stack and bring it back exactly the same way, under the same ownership lock, in
// either one.
//
// In the "none" role nobody owns those pins: no UART driver is ever installed, so the
// lock is trivially free, there is nothing to stop and nothing to start. The no-op
// definitions below are what say that, and they keep the body of the test identical across
// all three roles.
#if WB_MB_ROLE_SLAVE
    #include "mb_slave.h"
    #define MB_STACK_LOCK_TAKE(ms)      mb_slave_lock_take(ms)
    #define MB_STACK_LOCK_GIVE()        mb_slave_lock_give()
    #define MB_STACK_STOP()             mb_slave_stop()
    #define MB_STACK_START()            mb_slave_start()
#elif WB_MB_ROLE_MASTER
    #include "mb_master.h"
    #define MB_STACK_LOCK_TAKE(ms)      mb_master_lock_take(ms)
    #define MB_STACK_LOCK_GIVE()        mb_master_lock_give()
    #define MB_STACK_STOP()             mb_master_stop()
    #define MB_STACK_START()            mb_master_start()
#else
    #define MB_STACK_LOCK_TAKE(ms)      ((void)(ms), true)
    #define MB_STACK_LOCK_GIVE()        ((void)0)
    #define MB_STACK_STOP()             ((void)0)
    #define MB_STACK_START()            ((void)0)
#endif


// The clock-out test drives four pins directly: the TX line of both serial ports (the
// 100 kHz waveform, via the LEDC) and the DE/RE line of both ports as plain GPIOs — port
// 1's is RAISED (its transceiver transmits), port 2's is HELD LOW (its transceiver stays
// in receive, so that bus is not driven). The pins come from board_pins.h
// (SERIAL_{OUTPUT,IO}_PIN_{1,2}) — the GPIO numbers differ per board, and hardcoding the
// WB-MGE ones drove the wrong pins on WB-MGU (there GPIO4 is an input, the port-2 RX line).

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

// Transceiver driver-enable (DE/RE) pins. Port 1 is RAISED: driving DE HIGH is what makes
// the square wave actually reach the bus — with DE low the TX pin only toggles on the
// logic side, so the activity LED lights but nothing is emitted on the line.
//
// Port 2 is the exact opposite: its DE line is PARKED LOW for the whole test and is NEVER
// raised. Review comment #30 ("emit the 100 kHz on the second RS-485 too, i.e. raise
// GPIO15") was considered and DECLINED: the RS-485-2 pair is shared with the MIO
// transceiver U10 and wired out to the external RS-485-2 terminals, so driving it would
// put the factory meander on a bus we do not own, in front of whatever is wired to the
// terminals and alongside a live MIO controller (its reset is an expander pin, not a UART
// pin, so nothing on the UART side silences it). LED2 only needs the DI line, which we do
// drive.
//
// Because that decision stands, the RS-485-2 driver must be OFF for the whole test — and
// holding it off is OUR job, not the hardware's. A weak external pulldown (R4 on WB-MGE)
// cannot pull down a driven pad, and on WB-MGU that pin (GPIO13) has no pulldown at all, so
// leaving the pin alone could leave the port-2 driver ENABLED and put the meander from the
// DI line straight onto the bus. The test therefore takes the pin and drives it LOW itself,
// and keeps driving it LOW on the way out as well (see release_clock_out_hw) rather than
// releasing it to an internal pull-up. The one moment the pad is not driven by us is the
// capture itself: de_pin_latch_low_output() starts with gpio_reset_pin(), so between that
// call and the gpio_set_direction() a few register writes later the pad sits on its
// internal pull-up — microseconds, and only while we are taking the pin.
#define CLK_OUT_EN_PIN          SERIAL_IO_PIN_1   // DE of port 1 — raised while the test runs
#define CLK_OUT_DE_PARK_PIN     SERIAL_IO_PIN_2   // DE of port 2 — held LOW, never raised

#define CLK_OUT_JSON_FIELD      "clock_out"

// How long to let an in-flight settings apply drain before giving up on it. The Modbus
// branch of settings_update_task takes a few hundred ms; the network branches add the
// one-second response delay plus their own work. Five seconds covers both with room to
// spare and still answers the factory tester well inside its HTTP timeout.
#define SETTINGS_UPDATE_DRAIN_TIMEOUT_MS    5000
#define SETTINGS_UPDATE_DRAIN_POLL_MS       20

// How long to wait for the Modbus ownership lock once the drain above has reported the
// coast clear. The same budget, for the same reason: only a settings apply that got in
// between can be holding it, and its Modbus section is the few hundred ms above. A bounded
// wait rather than a wait-forever because this runs on the httpd task, which must not be
// parked indefinitely by anything.
#define MB_STACK_LOCK_TIMEOUT_MS            5000


// The guard described in wb_test.h. It spans the whole run — raised before the Modbus
// stack is stopped, lowered after it has been brought back — so it is not merely "the
// waveform is up"; it is "this test owns the RS-485 hardware, keep off". It is also the
// state GET /wb_test reports, because the two are the same fact.
//
// settings_update_task reads it from another FreeRTOS task, so the accesses go through
// the atomic builtins rather than being plain loads and stores.
static bool clock_out_en = false;

bool wb_test_clock_out_active(void)
{
    return __atomic_load_n(&clock_out_en, __ATOMIC_SEQ_CST);
}

static void clock_out_set_active(bool active)
{
    __atomic_store_n(&clock_out_en, active, __ATOMIC_SEQ_CST);
}

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
// the latch still holds whatever the previous owner left there. Enabling the output
// driver first would then briefly assert DE and put a glitch on the RS-485-1 line.
//
// The gpio_reset_pin() is how the pin is taken away from its current owner (a previous run
// of this test), and it costs a micro-window: the pad is left in GPIO_MODE_DISABLE with the
// internal pull-up on until the direction is set a few register writes later. So a DE line
// that we were already holding LOW dips driven-LOW -> weakly-HIGH -> driven-LOW when the
// test is re-entered. That is a handful of microseconds, versus the indefinite time the pad
// would spend pulled up if we reset it on the way out instead (which is why
// release_clock_out_hw() does not).
static void de_pin_latch_low_output(gpio_num_t pin)
{
    gpio_reset_pin(pin);
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}


// Tear the LEDC down and release three of the four pins the test owns: the TX line of both
// ports and the raised port-1 DE line. The parked port-2 DE line is deliberately NOT
// released — it stays a driven-LOW output (see below). Also used to roll back a
// half-configured LEDC when start_clock_out() fails midway: the ledc_* calls simply
// report an error for a channel/timer that was never set up, and gpio_reset_pin() is
// harmless on a pin this attempt never got as far as configuring. Both DE pins are latched
// LOW at the very top of start_clock_out(), before any LEDC call, so on every path through
// here the port-2 DE pin is already an output we drive.
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

    // Release the two TX lines and the port-1 DE line. Note that gpio_reset_pin() does not
    // leave a pin floating: it puts the pad in GPIO_MODE_DISABLE with the internal pull-up
    // ON, so a released pin is weakly pulled towards 1. For the port-1 DE line that is a weak
    // pull towards "driver enabled" — acceptable there and only there: that driver was
    // deliberately ON for the whole test, since the RS-485-1 pair is the one we are allowed
    // to drive.
    gpio_reset_pin(CLK_OUT_PIN);
    gpio_reset_pin(CLK_OUT_PIN_2);
    gpio_reset_pin(CLK_OUT_EN_PIN);

    // CLK_OUT_DE_PARK_PIN (port-2 DE) is deliberately left DRIVEN LOW — no gpio_reset_pin()
    // here. Resetting it would put the pad in GPIO_MODE_DISABLE with the internal pull-up
    // ON, i.e. weakly pulled towards 1 — the "driver enabled" level. The entire point of the
    // park is that the RS-485-2 pair stays silent (it is shared with the MIO transceiver and
    // wired out to the terminals); releasing the pin to a pull-up would re-open exactly the
    // window we just spent the test closing. On WB-MGE the external pulldown R4 would fight
    // that pull-up, but that backstop is board-specific and must not be relied on: on WB-MGU
    // SERIAL_IO_PIN_2 is GPIO13, the DE line of the WBE2 bus, with no pulldown at all.
    //
    // Holding the pin costs nothing: DE=0 is receive mode, i.e. the transceiver is not
    // driving the bus — the safe state. Re-entering the test still works: the park in
    // de_pin_latch_low_output() starts with gpio_reset_pin(), so it re-acquires the pin no
    // matter what state it is in. That re-acquire is not perfectly seamless: the reset
    // releases the pad to the internal pull-up for the few register writes until the
    // direction is set again, so on a second entry the line dips driven-LOW -> weakly-HIGH ->
    // driven-LOW (microseconds). We accept that: it is the price of the standard capture
    // idiom.
}


// Bring the 100 kHz waveform up on the TX line of both ports and enable the RS-485-1
// line driver. The port-1 DE pin is raised ONLY once every LEDC call has succeeded: if
// the waveform never started, enabling the driver would put the transceiver into transmit
// with a STATIC level on the RS-485-1 line while the API happily reported success. On
// any error the half-configured LEDC is released and the DE line stays LOW (receive
// mode); the caller aborts the test entry.
//
// Port 2 gets the waveform on its TX (DI) line only. Its DE line is driven LOW here and
// is never raised — that is what keeps the RS-485-2 pair silent (see CLK_OUT_DE_PARK_PIN
// above).
static esp_err_t start_clock_out(void)
{
    // Keep the RS-485-1 transceiver in receive mode until the waveform is running.
    de_pin_latch_low_output(CLK_OUT_EN_PIN);
    // Park the RS-485-2 transceiver in receive mode for the whole test: take the pin and hold
    // it LOW ourselves rather than trust whatever level the pad happens to rest at (the board
    // pulldown R4 on WB-MGE, nothing at all on WB-MGU, where SERIAL_IO_PIN_2 is GPIO13, or an
    // internal pull-up left behind by an earlier gpio_reset_pin() on that pin). It is never
    // set to 1 anywhere in this file — that is the invariant review #30 turned on.
    de_pin_latch_low_output(CLK_OUT_DE_PARK_PIN);

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
    // The RS-485-2 driver stays parked LOW — its bus is not ours to drive.
    gpio_set_level(CLK_OUT_EN_PIN, 1);

    ESP_LOGW(TAG, "100 kHz clock output on RS485-1/RS485-2 TX enabled (all indicator LEDs on)");
    return ESP_OK;
}


static void stop_clock_out(void)
{
    release_clock_out_hw();
    ESP_LOGW(TAG, "100 kHz clock output on RS485-1/RS485-2 TX disabled");
}


// Let an in-flight settings apply drain before this test takes the Modbus stack.
//
// What makes the test and a settings apply mutually exclusive is MB_STACK_LOCK_TAKE()
// below, not this wait: esp-modbus does not survive two tasks tearing the same instances
// down at once (mbc_slave_stop() answers "mb stack start event set error" and the second
// caller blocks inside the delete), and only a real lock can stop that. This wait is what
// turns "an apply is busy right now" into a clean 503 instead of an HTTP handler parked on
// a mutex for as long as the apply lasts.
//
// One case is refused on the spot rather than waited out. An apply that carries the web
// server calls http_server_release() -> httpd_stop(), which blocks until the httpd thread
// leaves its handler — and the httpd thread is the one running THIS handler. Waiting there
// cannot succeed: settings_update_in_progress() only goes false after httpd_stop() has
// returned, so the wait would burn its whole timeout, answer 503 anyway, and hold the web
// server's restart back by exactly that long. The check is repeated on every poll because
// such an apply can also be spawned while we wait — the config button's factory reset runs
// settings_update() on config_button_task.
//
// Returning false means the caller refuses the request rather than starting the test on top
// of an apply.
static bool wait_for_settings_update(void)
{
    for (int waited_ms = 0; ; waited_ms += SETTINGS_UPDATE_DRAIN_POLL_MS) {
        if (!settings_update_in_progress()) {
            return true;
        }
        if (settings_update_restarts_web_server()) {
            ESP_LOGE(TAG, "clock_out: refused, the settings update in flight is restarting "
                          "the web server and cannot finish while this handler waits");
            return false;
        }
        if (waited_ms >= SETTINGS_UPDATE_DRAIN_TIMEOUT_MS) {
            break;
        }
        if (waited_ms == 0) {
            ESP_LOGW(TAG, "clock_out: waiting for the settings update in flight to finish");
        }
        vTaskDelay(pdMS_TO_TICKS(SETTINGS_UPDATE_DRAIN_POLL_MS));
    }

    ESP_LOGE(TAG, "clock_out: the settings update did not finish within %d ms",
             SETTINGS_UPDATE_DRAIN_TIMEOUT_MS);
    return false;
}


// Take the Modbus ownership lock for one clock_out transition. Both transitions run
// the same preamble: drain whatever apply is in flight, then take the lock that keeps the
// next one out. Returns false when the caller must answer 503 instead.
static bool take_mb_stack_for_transition(void)
{
    if (!wait_for_settings_update()) {
        return false;
    }
    if (!MB_STACK_LOCK_TAKE(MB_STACK_LOCK_TIMEOUT_MS)) {
        ESP_LOGE(TAG, "clock_out: the Modbus ownership lock was not free within %d ms",
                 MB_STACK_LOCK_TIMEOUT_MS);
        return false;
    }
    return true;
}


// Enter the test. The caller holds the Modbus slave ownership lock, which is what makes
// "the guard is down" still true by the time the stop below runs.
static esp_err_t clock_out_enter(void)
{
    // The guard goes up FIRST, before a single pin changes hands. From here on a settings
    // apply running on settings_update_task knows to keep off the RS-485 hardware; raising
    // it any later would leave the stop below — and the window between it and the first
    // LEDC call — unguarded.
    clock_out_set_active(true);
    // Stop the Modbus stack first: whichever role was built, it owns both UARTs, and with
    // them the TX and DE pins this test is about to take for the LEDC waveform. Deleting the
    // serial instances releases the UART drivers and those pins.
    //
    // Nothing is persisted by this, so losing power mid-test cannot change the port
    // configuration: MB_STACK_START() on the way out re-reads every serial parameter (and,
    // in the slave role, the unit id and the TCP port) straight from NVS.
    MB_STACK_STOP();
    // The I/O bus is deliberately left alone. The MIO controller hangs off the RS-485-2
    // pair, but the test never drives that pair (the port-2 transceiver is held in receive
    // mode, see CLK_OUT_DE_PARK_PIN), so there is nothing for MIO to contend with and no
    // reason to reset it.
    esp_err_t clk_err = start_clock_out();
    if (clk_err != ESP_OK) {
        // The LEDC never came up, so start_clock_out() left both DE lines LOW and released
        // the pins it took. Reporting success here would leave the factory tester with a
        // device that claims to emit a clock but does not. Roll the entry back: bring the
        // slave up again from NVS, leaving the device exactly as it was before the request.
        ESP_LOGE(TAG, "clock_out aborted: the LEDC could not be set up");
        MB_STACK_START();
        clock_out_set_active(false);
        return ESP_ERR_INVALID_STATE;
    }
    // Factory test: light all LEDs simultaneously with the test signal.
    indication_set_test_all_leds(true);
    // Also lights the V-out LED (energises RS-485 bus V-out).
    esp_err_t vout_err = rs485_bus_vout_on_off(true);
    if (vout_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable V-out for clock_out test: %s", esp_err_to_name(vout_err));
    }
    return ESP_OK;
}


// Leave the test. The caller holds the Modbus ownership lock, so the start below cannot
// race a settings apply doing its own stop/start.
static void clock_out_exit(void)
{
    stop_clock_out();
    // stop_clock_out() has released the TX lines and the port-1 DE line, so the UARTs may
    // be brought up again. The port-2 DE line is still driven LOW by us — MB_STACK_START()
    // is what hands it back, via uart_set_pin().
    //
    // The test never touched NVS, so the configured parameters are still there: this
    // re-reads them and also picks up any settings written while the test was running,
    // including the 485_tx_dis_N flags.
    MB_STACK_START();
    // Factory test: return LEDs to normal indication and restore V-out state.
    // The I/O bus needs no restoring: the test never touched it.
    indication_set_test_all_leds(false);
    update_rs485_control();         // restore V-out to the configured KEY_485_VOUT state
    // The guard comes down LAST, once every pin and the V-out line are back under their
    // normal owners. It also re-opens the door for the settings a POST /settings deferred
    // while the test ran: MB_STACK_START() above has already re-read the serial parameters,
    // and update_rs485_control() the V-out and terminator state, so nothing was lost — only
    // postponed.
    clock_out_set_active(false);
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

    // The requested state is already the current one: nothing is stopped, started or taken
    // from anyone. Such a request must not be made to wait for a settings apply, nor be
    // refused with a 503 — a factory tester opens every session with one
    // ({"clock_out": false} against an idle device), and a repeated
    // {"clock_out": true} is the same no-op. Only the two branches that really move the
    // hardware do the drain-and-lock preamble.
    //
    // The guard is read here without the lock, and that is sound: the httpd task is the
    // only writer, and it is the task running this handler. A settings apply never changes
    // it — it only reads it, under the lock.
    bool want_active = (cmd_item->valueint != 0);
    if (want_active == wb_test_clock_out_active()) {
        return ESP_OK;
    }

    if (!take_mb_stack_for_transition()) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t res = ESP_OK;
    if (want_active) {
        res = clock_out_enter();
    } else {
        clock_out_exit();
    }
    MB_STACK_LOCK_GIVE();

    return res;
}


static void fill_response_json(cJSON *response_json)
{
    cJSON_AddBoolToObject(response_json, CLK_OUT_JSON_FIELD, wb_test_clock_out_active());
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
        } else if (res == ESP_ERR_TIMEOUT) {
            // A settings apply is still holding the Modbus slave — either it is still in
            // flight, or it is one that restarts the web server and therefore cannot finish
            // while this handler waits for it. Starting the test on top of it would have two
            // tasks tearing the same esp-modbus instances down at once, so the request is
            // refused — valid, but not servable right now.
            return json_utils_send_error_status(req, "503 Service Unavailable",
                "Cannot run the clock_out test: a settings update is still being applied");
        } else if (res == ESP_ERR_INVALID_STATE) {
            // The LEDC refused to produce the waveform, so the test never started — 503:
            // the request was valid, the device could not serve it.
            return json_utils_send_error_status(req, "503 Service Unavailable",
                "Cannot start clock_out test: the clock generator could not be set up");
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
