#include "mb_master.h"

#include <stdio.h>
#include <string.h>

#include "array_size.h"
#include "board_pins.h"
#include "config.h"
// The GPIO/UART calls below are the plain IDF ones in both builds. In QEMU the linker
// redirects them to the shim in qemu/gpio_shim_qemu.c through --wrap, so the same code
// drives the real pins on hardware and the virtual IO bus under emulation.
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_modbus_common.h"
#include "esp_modbus_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "setting_items.h"

static const char *TAG = "mb_master";

// Modbus function codes. esp-modbus keeps its own MB_FUNC_* enum in mb_proto.h, which is a
// PRIVATE include dir of the component (mb_objects/include), so the three codes this
// module sends are spelled out here rather than reached for through a header the component
// does not export.
#define MB_FC_READ_HOLDING      0x03
#define MB_FC_READ_INPUT        0x04
#define MB_FC_WRITE_HOLDING     0x06

// The Modbus limit on FC03/FC04: 125 registers in one response frame. A larger request is
// refused here rather than sent and rejected by the far end.
#define MB_MASTER_MAX_READ_REGS 125

// Unit id 0 is the Modbus broadcast address: no device answers it, by definition. A WRITE to
// it is legitimate and is left alone, but a READ is not, and esp-modbus does not say so -
// mbm_fn_read_holding_reg()/mbm_fn_read_inp_reg() take their `is_broadcast` branch, return
// MB_EX_NONE WITHOUT ever calling the register callback, and mbc_master_send_request() hands
// back ESP_OK over a caller buffer nothing has written. So the two read functions refuse it
// here instead. The upper end needs no check of ours: the library rejects snd_addr > 247
// (MB_ADDRESS_MAX) with MB_EINVAL, which reaches the caller as ESP_ERR_INVALID_ARG anyway.
#define MB_ADDR_BROADCAST       0

// How long a request waits for the slave to answer before it comes back as
// ESP_ERR_TIMEOUT. Passed per instance (ser_opts.response_tout_ms) rather than left at 0,
// which makes esp-modbus fall back to CONFIG_FMB_MASTER_TIMEOUT_MS_RESPOND - 5 seconds by
// default, long enough that a single absent device stalls a 1 Hz poll loop.
//
// It must stay below CONFIG_FMB_MASTER_MAX_API_BLOCKING_TIME_MS (6000 by default), or
// mbc_master_start() overrides it with a warning.
#define MB_MASTER_RESPONSE_TIMEOUT_MS   500

// Everything about one RS-485 port that does not come from NVS. Same UART numbers and same
// board pins as the slave role uses (UART0 is the console); the DE/RE line is driven by the
// UART as RTS in half-duplex mode.
typedef struct {
    uart_port_t uart;
    int         tx_pin;
    int         rx_pin;
    int         dir_pin;
    const char *tx_dis_key;
} mb_serial_port_t;

static const mb_serial_port_t mb_serial_ports[RS485_PORTS_COUNT] = {
    {UART_NUM_1, SERIAL_OUTPUT_PIN_1, SERIAL_INPUT_PIN_1, SERIAL_IO_PIN_1, KEY_485_TX_DISABLED_1},
    {UART_NUM_2, SERIAL_OUTPUT_PIN_2, SERIAL_INPUT_PIN_2, SERIAL_IO_PIN_2, KEY_485_TX_DISABLED_2},
};

// The serial parameters of one port, as read from NVS.
typedef struct {
    int                baudrate;
    uart_word_length_t databits;
    uart_parity_t      parity;
    uart_stop_bits_t   stopbits;
} mb_serial_config_t;

// esp-modbus refuses to START a master whose descriptor table is empty -
// mbc_serial_master_start() checks mbm_param_descriptor_size >= 1 and answers
// ESP_ERR_INVALID_ARG ("mb descriptor table size is incorrect") - even though
// mbc_master_send_request(), the only entry point this module uses, never reads that table.
// This one-row table exists solely to satisfy that precondition; nothing looks it up, no
// characteristic id is ever passed to mbc_master_get_parameter(), and adding a register to
// this firmware's master side means calling one of the three functions in mb_master.h, NOT
// appending a row here.
//
// The three fields mbc_serial_master_set_descriptor() validates are cid (must equal the row
// index), param_key (must be non-NULL) and mb_size (must be > 0).
static const mb_parameter_descriptor_t mb_master_descriptor[] = {
    {
        .cid            = 0,
        .param_key      = "unused",
        .param_units    = "",
        .mb_slave_addr  = 1,
        .mb_param_type  = MB_PARAM_HOLDING,
        .mb_reg_start   = 0,
        .mb_size        = 1,
        .param_offset   = 0,
        .param_type     = PARAM_TYPE_U16,
        .param_size     = PARAM_SIZE_U16,
        .access         = PAR_PERMS_READ,
    },
};

// The configuration the RUNNING instances were started with.
// mb_master_check_settings_changed() compares NVS against this.
static mb_serial_config_t mb_running_serial[RS485_PORTS_COUNT];

static void *mb_handles[RS485_PORTS_COUNT];

// What is up right now, and what this device has ASKED for. Same pair, with the same
// meaning, as the slave role's flags: nothing ever clears mb_serial_wanted, so a port this
// device has once opened is one it is supposed to keep open - which is what makes a failed
// start recoverable through a later settings write instead of dead until the next reboot.
static bool mb_serial_running;
static bool mb_serial_wanted;

// Serialises the stop/start pairs and the decisions that lead into them; see the block
// comment in mb_master.h. Statically allocated, so mb_master_lock_init() cannot fail.
static SemaphoreHandle_t mb_lock;
static StaticSemaphore_t mb_lock_buffer;

// One request mutex per port, so two application tasks calling mb_master_read_holding() on
// the same bus queue up instead of interleaving. It is also what makes a teardown safe: a
// task blocked inside mbc_master_send_request() is holding the very instance
// mb_master_stop() is about to delete, so the stop takes both of these first and thereby
// waits the in-flight requests out.
//
// Lock ORDER, and the reason there is no deadlock: the request path takes ONLY the port
// mutex and never the ownership lock; the stop/start path takes the ownership lock first
// (in its callers) and the port mutexes second. Nobody ever takes them the other way round.
static SemaphoreHandle_t mb_req_mutex[RS485_PORTS_COUNT];
static StaticSemaphore_t mb_req_mutex_buffer[RS485_PORTS_COUNT];

// Mirrors the DE-pin kill-switch state of each port, so mb_master_set_tx_disabled() can
// skip the pin work when nothing changes and mb_master_start() can re-apply it.
static bool mb_tx_disabled[RS485_PORTS_COUNT];


// ── Settings ─────────────────────────────────────────────────────────────────────────

/* Map a string value to its corresponding integer constant using a lookup table.
 * Returns default_val when the string does not match any entry. */
static int lookup_str_to_int(const char *str, const char * const keys[], const int vals[],
                             int count, int default_val)
{
    for (int i = 0; i < count; i++) {
        if (strncmp(str, keys[i], SETTING_ITEM_MAX_STR_LEN) == 0) {
            return vals[i];
        }
    }
    return default_val;
}

// Read one port's serial parameters from the per-port NVS keys (baudrate_N, parity_N,
// stopbits_N, databits_N) - the SAME keys, with the same string -> enum mappings, that the
// slave role reads. A device reflashed from one role to the other keeps its configured line
// parameters.
//
// This function, and the three *_str() helpers below it, are deliberate copies of their
// counterparts in main/mb_slave/mb_slave.c rather than a shared module. The two roles are
// never compiled together, so nothing is gained at link time, and the slave file is the one
// the 85-test e2e suite exercises: leaving it untouched is worth more here than removing
// the duplication.
static esp_err_t read_serial_port_config(int index, mb_serial_config_t *config)
{
    char key_buf[SETTING_ITEM_MAX_STR_LEN];
    char value_str[SETTING_ITEM_MAX_STR_LEN];

    snprintf(key_buf, sizeof(key_buf), "baudrate_%d", index + 1);
    config->baudrate = setting_items_read_int(key_buf);
    if (!config->baudrate) {
        ESP_LOGE(TAG, "Failed to read baudrate for port %d", index + 1);
        return ESP_FAIL;
    }

    static const char * const parity_keys[] = {
        UART_PARITY_DISABLE_STR, UART_PARITY_EVEN_STR, UART_PARITY_ODD_STR
    };
    static const int parity_vals[] = {
        UART_PARITY_DISABLE, UART_PARITY_EVEN, UART_PARITY_ODD
    };
    snprintf(key_buf, sizeof(key_buf), "parity_%d", index + 1);
    ESP_RETURN_ON_ERROR(setting_items_read(key_buf, value_str), TAG,
                        "Failed to read parity for port %d", index + 1);
    config->parity = (uart_parity_t)lookup_str_to_int(
        value_str, parity_keys, parity_vals, ARRAY_SIZE(parity_keys), UART_PARITY_DISABLE
    );

    static const char * const stopbits_keys[] = {
        UART_STOP_BITS_1_STR, UART_STOP_BITS_1_5_STR, UART_STOP_BITS_2_STR
    };
    static const int stopbits_vals[] = {
        UART_STOP_BITS_1, UART_STOP_BITS_1_5, UART_STOP_BITS_2
    };
    snprintf(key_buf, sizeof(key_buf), "stopbits_%d", index + 1);
    ESP_RETURN_ON_ERROR(setting_items_read(key_buf, value_str), TAG,
                        "Failed to read stopbits for port %d", index + 1);
    config->stopbits = (uart_stop_bits_t)lookup_str_to_int(
        value_str, stopbits_keys, stopbits_vals, ARRAY_SIZE(stopbits_keys), UART_STOP_BITS_2
    );

    static const char * const databits_keys[] = {
        UART_DATA_5_BITS_STR, UART_DATA_6_BITS_STR, UART_DATA_7_BITS_STR, UART_DATA_8_BITS_STR
    };
    static const int databits_vals[] = {
        UART_DATA_5_BITS, UART_DATA_6_BITS, UART_DATA_7_BITS, UART_DATA_8_BITS
    };
    snprintf(key_buf, sizeof(key_buf), "databits_%d", index + 1);
    ESP_RETURN_ON_ERROR(setting_items_read(key_buf, value_str), TAG,
                        "Failed to read databits for port %d", index + 1);
    config->databits = (uart_word_length_t)lookup_str_to_int(
        value_str, databits_keys, databits_vals, ARRAY_SIZE(databits_keys), UART_DATA_8_BITS
    );

    return ESP_OK;
}

bool mb_master_check_settings_changed(void)
{
    // Nothing has ever been asked for: app_main has not brought the ports up, so there is
    // nothing a changed setting could describe.
    if (!mb_serial_wanted) {
        return false;
    }

    // Anything that should be up and is not is itself a pending change, and this is what
    // makes a failed start recoverable through a normal settings write: the stop/start pair
    // the caller runs on a reported change re-reads NVS and tries again.
    if (!mb_serial_running) {
        return true;
    }

    for (unsigned i = 0; i < RS485_PORTS_COUNT; i++) {
        // Only the ports that actually came up: mb_running_serial[] describes those and no
        // others, so comparing a port whose handle is NULL would report a change on every
        // single settings write - a Wi-Fi password included - and interrupt the traffic on
        // the port that IS working.
        if (mb_handles[i] == NULL) {
            continue;
        }
        mb_serial_config_t config;
        if (read_serial_port_config((int)i, &config) != ESP_OK) {
            continue;
        }
        if (memcmp(&config, &mb_running_serial[i], sizeof(config)) != 0) {
            return true;
        }
    }

    return false;
}


// ── RS-485 direction control ─────────────────────────────────────────────────────────

esp_err_t mb_master_set_tx_disabled(unsigned index, bool disabled)
{
    if (index >= RS485_PORTS_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    const mb_serial_port_t *port = &mb_serial_ports[index];

    // Nothing to gate while the ports are down: there is no UART holding the pins, and the
    // pins may well belong to someone else - the factory clock_out test stops the Modbus
    // stack precisely so it can take both DE lines and both TX lines for its LEDC waveform.
    // mb_master_start_serial() re-applies the NVS setting when it hands the pins back to the
    // UARTs, so a value written while the stack was down is not lost, only deferred.
    if (!mb_serial_running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (disabled == mb_tx_disabled[index]) {
        return ESP_OK; // no state change needed
    }

    if (disabled) {
        // Detach dir_pin from UART control and force LOW (RS-485 TX disabled).
        // Set the safe level first, then switch to output, so the pin never drives an
        // undefined level in the window between direction and level.
        gpio_reset_pin(port->dir_pin);
        gpio_set_level(port->dir_pin, 0);
        gpio_set_direction(port->dir_pin, GPIO_MODE_OUTPUT);
        mb_tx_disabled[index] = true;
        ESP_LOGI(TAG, "UART[%d] TX physically disabled (dir_pin=%d forced LOW)",
                 port->uart, port->dir_pin);
        return ESP_OK;
    }

    // Re-attach dir_pin to UART for automatic half-duplex direction control. TX and RX are
    // passed explicitly instead of UART_PIN_NO_CHANGE for the reason spelled out at length
    // in main/mb_slave/mb_slave.c: on the IDF versions carrying the uart_set_pin release
    // regression, UART_PIN_NO_CHANGE leaves TX output-disabled and RX detached for good.
    esp_err_t err = uart_set_pin(port->uart, port->tx_pin, port->rx_pin, port->dir_pin,
                                 UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        // Keep tx_disabled = true and return early: the routing was not restored, so TX
        // really is still off, and the flag has to keep describing the hardware.
        ESP_LOGE(TAG, "UART[%d] failed to restore pin routing: %s",
                 port->uart, esp_err_to_name(err));
        return err;
    }
    mb_tx_disabled[index] = false;
    ESP_LOGI(TAG, "UART[%d] TX re-enabled (dir_pin=%d restored to UART)",
             port->uart, port->dir_pin);
    return ESP_OK;
}


// ── Start / stop ─────────────────────────────────────────────────────────────────────

// The three helpers below turn the IDF enums back into the values the user configured. The
// numeric enum values are not the human ones - UART_DATA_8_BITS is 3, UART_STOP_BITS_2 is
// 3 - so printing them raw produces lines like "3 data bits, stop bits 3".
static const char *databits_str(uart_word_length_t databits)
{
    switch (databits) {
    case UART_DATA_5_BITS: return UART_DATA_5_BITS_STR;
    case UART_DATA_6_BITS: return UART_DATA_6_BITS_STR;
    case UART_DATA_7_BITS: return UART_DATA_7_BITS_STR;
    case UART_DATA_8_BITS: return UART_DATA_8_BITS_STR;
    default:               return "?";
    }
}

static const char *parity_str(uart_parity_t parity)
{
    switch (parity) {
    case UART_PARITY_DISABLE: return UART_PARITY_DISABLE_STR;
    case UART_PARITY_EVEN:    return UART_PARITY_EVEN_STR;
    case UART_PARITY_ODD:     return UART_PARITY_ODD_STR;
    default:                  return "?";
    }
}

static const char *stopbits_str(uart_stop_bits_t stopbits)
{
    switch (stopbits) {
    case UART_STOP_BITS_1:   return UART_STOP_BITS_1_STR;
    case UART_STOP_BITS_1_5: return UART_STOP_BITS_1_5_STR;
    case UART_STOP_BITS_2:   return UART_STOP_BITS_2_STR;
    default:                 return "?";
    }
}

// Create the module's mutexes. Idempotent, and deliberately not re-creating an existing
// one: a second create would hand back a new handle and strand whatever task holds the old
// one. Static allocation cannot fail, so there is no half-initialised state.
static void mb_master_mutexes_init(void)
{
    if (mb_lock == NULL) {
        mb_lock = xSemaphoreCreateMutexStatic(&mb_lock_buffer);
    }
    for (unsigned i = 0; i < RS485_PORTS_COUNT; i++) {
        if (mb_req_mutex[i] == NULL) {
            mb_req_mutex[i] = xSemaphoreCreateMutexStatic(&mb_req_mutex_buffer[i]);
        }
    }
}

static esp_err_t mb_start_master_port(unsigned index)
{
    const mb_serial_port_t *port = &mb_serial_ports[index];

    mb_serial_config_t config;
    ESP_RETURN_ON_ERROR(read_serial_port_config((int)index, &config), TAG,
                        "Port %u: cannot read the serial configuration", index + 1);

    mb_communication_info_t comm = {
        .ser_opts.mode             = MB_RTU,
        .ser_opts.port             = port->uart,
        // "Modbus slave address field (dummy for master)" - the unit id of every request
        // is carried by mb_param_request_t.slave_addr instead, which is why this role has
        // no mb_slave_id setting at all.
        .ser_opts.uid              = 0,
        .ser_opts.response_tout_ms = MB_MASTER_RESPONSE_TIMEOUT_MS,
        .ser_opts.baudrate         = (uint32_t)config.baudrate,
        .ser_opts.data_bits        = config.databits,
        .ser_opts.stop_bits        = config.stopbits,
        .ser_opts.parity           = config.parity,
    };

    // The instance is built on a LOCAL handle and published into mb_handles[index] only as
    // the very last action, once it is started and usable. That ordering is what makes the
    // request path's "handle under the mutex" rule true in BOTH directions.
    //
    // Publishing at create time instead - which is what &mb_handles[index] here would do -
    // opens a window that a real path walks through: a stack restart from
    // settings_update_task, or a clock_out exit, runs create -> uart_set_pin ->
    // uart_set_mode -> set_descriptor -> start while the application's poll loop keeps
    // calling in. This function does not hold mb_req_mutex[index] (it cannot: the mutex is
    // the request path's, and a start must not wait behind a request), so a request landing
    // in that window would find a non-NULL handle for a created-but-NOT-started instance.
    // mbc_ser_master_task is still parked on MB_EVENT_STACK_STARTED at that point, so the
    // request is never transmitted and comes back as ESP_ERR_TIMEOUT - a device that IS on
    // the bus reported as silent, which is exactly the distinction the poll loop in
    // main/mb_master/user_app.c relies on.
    //
    // The publish itself is a single aligned pointer store, so a reader under the mutex sees
    // either NULL (this port is not running yet - ESP_ERR_INVALID_STATE, which is true) or a
    // fully started instance. Never a half-built one.
    void     *handle = NULL;
    esp_err_t ret    = ESP_OK;

    // This is what installs the UART driver (mbm_rtu_create -> uart_param_config +
    // uart_driver_install), so nothing may touch the port's pins before it returns.
    ESP_RETURN_ON_ERROR(mbc_master_create_serial(&comm, &handle), TAG,
                        "Port %u: mbc_master_create_serial failed", index + 1);

    // esp-modbus configures the line parameters but NOT the pins or the transceiver
    // direction mode - that is the application's job, and it has to happen after the create
    // above and before the start below.
    ESP_GOTO_ON_ERROR(uart_set_pin(port->uart, port->tx_pin, port->rx_pin, port->dir_pin,
                                   UART_PIN_NO_CHANGE), fail, TAG,
                      "Port %u: uart_set_pin failed", index + 1);

#if QEMU_BUILD
    /* QEMU does not implement RS485 half-duplex mode: uart_set_mode() with
     * UART_MODE_RS485_HALF_DUPLEX sets the RS485_EN bit, causing uart_wait_tx_done() to
     * assert on the TX_DONE interrupt state (uart.c:1348). Use plain UART mode in QEMU -
     * the chardev TCP socket is a simple byte stream without RTS/CTS or RS485 direction
     * control. */
    ESP_GOTO_ON_ERROR(uart_set_mode(port->uart, UART_MODE_UART), fail, TAG,
                      "Port %u: uart_set_mode failed", index + 1);
#else
    ESP_GOTO_ON_ERROR(uart_set_mode(port->uart, UART_MODE_RS485_HALF_DUPLEX), fail, TAG,
                      "Port %u: uart_set_mode failed", index + 1);
#endif

    // Only here to get past mbc_master_start()'s empty-table check; see the comment on
    // mb_master_descriptor.
    ESP_GOTO_ON_ERROR(mbc_master_set_descriptor(handle, mb_master_descriptor,
                                                ARRAY_SIZE(mb_master_descriptor)), fail, TAG,
                      "Port %u: mbc_master_set_descriptor failed", index + 1);

    ESP_GOTO_ON_ERROR(mbc_master_start(handle), fail, TAG,
                      "Port %u: mbc_master_start failed", index + 1);

    mb_running_serial[index] = config;
    // Published last, and only now: from this store on, a request on this port reaches a
    // started instance.
    mb_handles[index] = handle;
    ESP_LOGI(TAG, "RTU master on UART[%d] up: %d baud, %s data bits, parity %s, %s stop bits, "
                  "%d ms response timeout",
             port->uart, config.baudrate, databits_str(config.databits),
             parity_str(config.parity), stopbits_str(config.stopbits),
             MB_MASTER_RESPONSE_TIMEOUT_MS);
    return ESP_OK;

fail:
    // The handle was never published, so no caller can be holding it and no request can be
    // in flight on it: the delete needs no request mutex. mb_handles[index] is left exactly
    // as it was (NULL), which is what marks the port as not running.
    mbc_master_delete(handle);
    return ret;
}

// Tear one instance down and forget its handle. Deleting it is also what releases the UART
// driver, and with it the TX/RX/DE pins - which is what lets the factory clock_out test take
// those pins for its LEDC waveform.
//
// The caller must hold this port's request mutex: mbc_master_delete() frees the instance a
// task blocked in mbc_master_send_request() is still using.
//
// NO mbc_master_stop() before the delete, and that is not an omission - it is the opposite
// of the slave path, deliberately. mbc_serial_master_delete() opens with a blocking
// xEventGroupWaitBits() on MB_EVENT_STACK_STARTED with a CONFIG_FMB_MASTER_TIMEOUT_MS_RESPOND
// (5 s) timeout, and only then stops the stack itself if it finds it running. Stopping first
// CLEARS that bit, so the wait inside delete has nothing to wake it and burns its full five
// seconds - measured: a stop of both ports took 10.0 s, which turned every Modbus settings
// apply and every clock_out transition into a ten-second stall (the factory test allows five
// before answering 503). Left to itself, delete sees the bit set, stops the stack and returns
// at once.
static void mb_delete_instance(unsigned index)
{
    if (mb_handles[index] == NULL) {
        return;
    }
    mbc_master_delete(mb_handles[index]);
    mb_handles[index] = NULL;
}

esp_err_t mb_master_start_serial(void)
{
    // Asked for from here on, whatever this attempt does. app_main calls this once, before
    // any network exists, and carries on whatever it returns; every later start goes through
    // mb_master_start().
    mb_serial_wanted = true;

    if (mb_serial_running) {
        return ESP_OK;
    }

    // Cheap and idempotent, and it makes this entry point usable on its own: app_main calls
    // mb_master_lock_init() first, but the request mutexes must exist before any instance
    // does, whichever call created them.
    mb_master_mutexes_init();

    // Each port is brought up on its own: one that refuses to start must not take the other
    // down with it, and a device driving one RS-485 bus is worth more than one driving
    // neither.
    unsigned started = 0;
    for (unsigned i = 0; i < RS485_PORTS_COUNT; i++) {
        if (mb_start_master_port(i) == ESP_OK) {
            started++;
        }
        // Nothing to clean up on the failure path: mb_start_master_port() publishes its
        // handle only after a successful start and deletes it itself otherwise, so a failed
        // port leaves mb_handles[i] NULL - which is what marks it as not running.
        // mb_running_serial[i] describes only the ports that came up, so
        // mb_master_check_settings_changed() skips this one instead of reporting a change on
        // every settings write forever.
    }

    if (started == 0) {
        ESP_LOGE(TAG, "No RS-485 port came up: this device polls Modbus on neither bus");
        return ESP_FAIL;
    }
    if (started < RS485_PORTS_COUNT) {
        ESP_LOGW(TAG, "Only %u of %u RS-485 ports came up", started, (unsigned)RS485_PORTS_COUNT);
    }

    mb_serial_running = true;

    // The kill-switch is re-applied last, on top of the pin routing mb_start_master_port()
    // just handed to the UART, and after mb_serial_running is set because the setter refuses
    // to touch the pins while the ports are down. mb_tx_disabled[] is cleared first because
    // that routing IS the enabled state: without the reset, a port that was disabled before
    // the restart would look already-disabled to the setter and keep the UART's pin.
    for (unsigned i = 0; i < RS485_PORTS_COUNT; i++) {
        mb_tx_disabled[i] = false;
        mb_master_set_tx_disabled(i, setting_items_read_bool(mb_serial_ports[i].tx_dis_key));
    }

    return ESP_OK;
}

esp_err_t mb_master_start(void)
{
    // Restores what this device has ASKED for, and only that - the mirror of
    // mb_slave_start(). A port that was asked for and is down was not "running" for any stop
    // to have found, so restoring only what was running would skip it forever.
    if (!mb_serial_wanted) {
        return ESP_OK;
    }
    return mb_master_start_serial();
}

esp_err_t mb_master_stop(void)
{
    if (!mb_serial_running) {
        return ESP_OK;
    }

    // Take every port's request mutex BEFORE deleting anything: an application task blocked
    // inside mbc_master_send_request() is using the instance about to be freed. Waiting
    // forever is bounded in practice - a request returns within the response timeout the
    // instance was created with - and cannot deadlock, because the request path never takes
    // the ownership lock this caller is holding.
    for (unsigned i = 0; i < RS485_PORTS_COUNT; i++) {
        if (mb_req_mutex[i] != NULL) {
            xSemaphoreTake(mb_req_mutex[i], portMAX_DELAY);
        }
    }

    for (unsigned i = 0; i < RS485_PORTS_COUNT; i++) {
        mb_delete_instance(i);
    }
    mb_serial_running = false;

    for (unsigned i = 0; i < RS485_PORTS_COUNT; i++) {
        if (mb_req_mutex[i] != NULL) {
            xSemaphoreGive(mb_req_mutex[i]);
        }
    }

    ESP_LOGI(TAG, "Modbus master stopped on both RS-485 ports");
    return ESP_OK;
}


// ── The ownership lock ───────────────────────────────────────────────────────────────

void mb_master_lock_init(void)
{
    mb_master_mutexes_init();
}

bool mb_master_lock_take(uint32_t timeout_ms)
{
    if (mb_lock == NULL) {
        // Only reachable if app_main never called mb_master_lock_init(). Refusing is the
        // safe answer: the caller declines to touch the stack rather than running the one
        // teardown this lock exists to serialise with no serialisation at all.
        ESP_LOGE(TAG, "Modbus master lock used before mb_master_lock_init()");
        return false;
    }
    TickType_t ticks = (timeout_ms == MB_MASTER_LOCK_WAIT_FOREVER)
                       ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTake(mb_lock, ticks) == pdTRUE);
}

void mb_master_lock_give(void)
{
    if (mb_lock == NULL) {
        return;
    }
    xSemaphoreGive(mb_lock);
}


// ── The request API ──────────────────────────────────────────────────────────────────

// One exchange on one port, under that port's request mutex. The handle is re-read INSIDE
// the mutex: a stop that ran while this caller was queuing has set it to NULL, and sending
// on the freed instance is exactly what the mutex is here to prevent.
//
// A non-NULL handle here always means a STARTED instance, because mb_start_master_port()
// publishes it as its last action and mb_master_stop() clears it before deleting anything.
// So the three answers this can give - a real exchange, ESP_ERR_INVALID_STATE, or
// ESP_ERR_TIMEOUT from the bus - never blur into each other.
static esp_err_t mb_master_request(uint8_t port, mb_param_request_t *request, void *data)
{
    if (port >= RS485_PORTS_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mb_req_mutex[port] == NULL) {
        ESP_LOGE(TAG, "Request on port %u before the Modbus master was initialised", port + 1);
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(mb_req_mutex[port], portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err;
    if (mb_handles[port] == NULL) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        err = mbc_master_send_request(mb_handles[port], request, data);
    }

    xSemaphoreGive(mb_req_mutex[port]);
    return err;
}

esp_err_t mb_master_read_holding(uint8_t port, uint8_t slave_id, uint16_t addr,
                                 uint16_t count, uint16_t *out)
{
    if ((out == NULL) || (count == 0) || (count > MB_MASTER_MAX_READ_REGS) ||
        (slave_id == MB_ADDR_BROADCAST)) {
        return ESP_ERR_INVALID_ARG;
    }
    mb_param_request_t request = {
        .slave_addr = slave_id,
        .command    = MB_FC_READ_HOLDING,
        .reg_start  = addr,
        .reg_size   = count,
    };
    return mb_master_request(port, &request, out);
}

esp_err_t mb_master_read_input(uint8_t port, uint8_t slave_id, uint16_t addr,
                               uint16_t count, uint16_t *out)
{
    if ((out == NULL) || (count == 0) || (count > MB_MASTER_MAX_READ_REGS) ||
        (slave_id == MB_ADDR_BROADCAST)) {
        return ESP_ERR_INVALID_ARG;
    }
    mb_param_request_t request = {
        .slave_addr = slave_id,
        .command    = MB_FC_READ_INPUT,
        .reg_start  = addr,
        .reg_size   = count,
    };
    return mb_master_request(port, &request, out);
}

esp_err_t mb_master_write_holding(uint8_t port, uint8_t slave_id, uint16_t addr,
                                  uint16_t value)
{
    // FC06 carries the value in the request frame itself: esp-modbus reads it as
    // *(uint16_t *)data_ptr, in native byte order, and swaps it onto the wire.
    uint16_t value_buf = value;
    mb_param_request_t request = {
        .slave_addr = slave_id,
        .command    = MB_FC_WRITE_HOLDING,
        .reg_start  = addr,
        .reg_size   = 1,
    };
    return mb_master_request(port, &request, &value_buf);
}
