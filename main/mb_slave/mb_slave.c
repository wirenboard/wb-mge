#include "mb_slave.h"

#include <assert.h>
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
#include "esp_modbus_slave.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mb_registers.h"
#include "setting_items.h"

static const char *TAG = "mb_slave";

// Instance indices into mb_handles[]. The two serial ones double as indices into
// mb_serial_ports[], so the order of the two tables must match.
#define MB_INST_RTU_1   0
#define MB_INST_RTU_2   1
#define MB_INST_TCP     2
#define MB_INST_COUNT   3

// Everything about one RS-485 port that does not come from NVS. The UART numbers are 1
// and 2 (UART0 is the console); the pins come from board_pins.h and differ per board.
typedef struct {
    uart_port_t    uart;
    int            tx_pin;
    int            rx_pin;
    int            dir_pin;    // DE/RE, driven by the UART as RTS in half-duplex mode
    mb_event_src_t src;
    const char    *tx_dis_key;
} mb_serial_port_t;

// The serial indices double as indices into mb_serial_ports[] and mb_running_serial[], so
// every serial instance has to sit below the TCP one. Break that and a serial loop would
// walk over the TCP handle.
static_assert(RS485_PORTS_COUNT == MB_INST_TCP,
              "serial instance indices must precede the TCP one");

static const mb_serial_port_t mb_serial_ports[RS485_PORTS_COUNT] = {
    {UART_NUM_1, SERIAL_OUTPUT_PIN_1, SERIAL_INPUT_PIN_1, SERIAL_IO_PIN_1,
     MB_SRC_RTU_PORT1, KEY_485_TX_DISABLED_1},
    {UART_NUM_2, SERIAL_OUTPUT_PIN_2, SERIAL_INPUT_PIN_2, SERIAL_IO_PIN_2,
     MB_SRC_RTU_PORT2, KEY_485_TX_DISABLED_2},
};

// The serial parameters of one port, as read from NVS.
typedef struct {
    int                baudrate;
    uart_word_length_t databits;
    uart_parity_t      parity;
    uart_stop_bits_t   stopbits;
} mb_serial_config_t;

// The configuration the RUNNING instances were started with. mb_slave_check_settings_changed()
// compares NVS against this, the same shape http_server_check_settings_changed() uses.
static mb_serial_config_t mb_running_serial[RS485_PORTS_COUNT];
static uint8_t            mb_running_uid;
static uint16_t           mb_running_tcp_port;

static void *mb_handles[MB_INST_COUNT];

// The two halves run independently: the RTU pair comes up early in app_main, before any
// network exists, and the TCP instance comes up only once one does. A device on an RS-485
// bus with no Ethernet cable and no Wi-Fi runs with mb_tcp_running permanently false.
static bool mb_serial_running;
static bool mb_tcp_running;

// True once the half in question has been ASKED for, whether or not that start succeeded.
// These are the device's record of INTENT: mb_*_running says what is up right now,
// mb_*_wanted says what is supposed to be. Nothing ever clears them — a half this device
// has once opened is a half it is supposed to keep open — which is precisely what makes
// them survive any number of later stop/start cycles.
//
// They are what tells "this device has no network, so there is no TCP half to speak of"
// apart from "the TCP half should be up and is not", and the same for the serial half on a
// board whose ports never came up. The second case is reported as a settings change below,
// so a failed start is retried by the next settings write instead of staying dead until the
// next reboot — app_main calls mb_slave_start_serial() and mb_slave_start_tcp() exactly
// once each.
//
// The stop latches below cannot carry this: they describe what ONE stop found running and
// are overwritten by the next one, so a half that is already down when the next stop runs
// latches false and would never be started again.
static bool mb_serial_wanted;
static bool mb_tcp_wanted;

// What the last mb_slave_stop() found running. These exist for ONE purpose: the log line
// at the end of mb_slave_stop(), which reports what was actually torn down.
//
// They decide nothing. What comes back after a stop is decided by the intent flags above
// and by them alone — do not reintroduce these into that decision. They used to carry it,
// and that is precisely the defect this module has now had fixed twice: a latch records
// what ONE stop happened to find, so a half that is already down at that moment (its last
// start having failed) latches false, and every later start skips it for good. A settings
// write cannot repair what it cannot see, and nothing short of a power cycle brings it
// back.
//
// Nor are they a safe "equivalent" of the intent flags to fall back on. The implication
// runs one way only: mb_*_running is set only after the matching mb_*_wanted, and nothing
// ever clears mb_*_wanted, so was_running IMPLIES wanted and never the reverse. That is
// also why mb_slave_start() tests the intent flags alone: `was_running || wanted` would be
// the same condition written twice, and the redundancy invites exactly the "simplification"
// that would put the latch back in charge.
static bool mb_serial_was_running;
static bool mb_tcp_was_running;

// Serialises the stop/start pairs and the decisions that lead into them; see the block
// comment in mb_slave.h. Statically allocated, so mb_slave_lock_init() cannot fail and
// there is no "the lock could not be created, carry on unguarded" path to reason about.
static SemaphoreHandle_t mb_lock;
static StaticSemaphore_t mb_lock_buffer;

// Mirrors the DE-pin kill-switch state of each port, so mb_slave_set_tx_disabled() can
// skip the pin work when nothing changes and mb_slave_start() can re-apply it.
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
// stopbits_N, databits_N). The keys and the string -> enum mappings are the ones the
// settings API and the web UI have always used, so a device keeps its configured line
// parameters across this firmware change.
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

static uint8_t read_slave_id(void)
{
    return (uint8_t)setting_items_read_int(KEY_MB_SLAVE_ID);
}

static uint16_t read_tcp_port(void)
{
    return (uint16_t)setting_items_read_int(KEY_MB_TCP_PORT);
}

bool mb_slave_check_settings_changed(void)
{
    // Nothing on this device has ever been asked for: app_main has not brought the serial
    // half up and no network has ever asked for a TCP instance. A stored TCP port then
    // describes nothing that exists, and reporting it as a change would restart the RS-485
    // ports for no reason.
    if (!mb_serial_wanted && !mb_tcp_wanted) {
        return false;
    }

    // Anything that should be up and is not is itself a pending change, and this is what
    // makes a failed start recoverable through a normal settings write: the stop/start
    // pair the caller runs on a reported change re-reads NVS and tries again. Without it a
    // TCP listener that refused to bind, or a stop whose paired start failed, would stay
    // dead until the next reboot — mb_slave_start_tcp() and mb_slave_start_serial() are
    // each called exactly once from app_main.
    //
    // Both tests are on the intent flags, never on the stop latches. A latch is rewritten
    // by every stop that finds anything at all running, so on a device whose TCP half is up
    // and whose serial half failed to restart, the next settings write would latch "serial
    // was not running" and this repair path would go quiet for good — two dead RS-485 buses
    // until someone pulls the power.
    if (mb_tcp_wanted && !mb_tcp_running) {
        return true;
    }
    if (mb_serial_wanted && !mb_serial_running) {
        return true;
    }

    if (read_slave_id() != mb_running_uid) {
        return true;
    }

    if (mb_tcp_running && (read_tcp_port() != mb_running_tcp_port)) {
        return true;
    }

    if (mb_serial_running) {
        for (unsigned i = 0; i < RS485_PORTS_COUNT; i++) {
            // Only the ports that actually came up: mb_running_serial[] describes those
            // and no others, so comparing a port whose handle is NULL would report a
            // change on every single settings write — a Wi-Fi password included — and
            // interrupt the traffic on the port that IS working.
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
    }

    return false;
}


// ── The register hook ────────────────────────────────────────────────────────────────
//
// esp-modbus declares mbc_reg_holding_slave_cb() / mbc_reg_input_slave_cb() weak in
// esp_modbus_slave.h and installs exactly those pointers into every controller it creates
// (mbc_serial_slave.c and mbc_tcp_slave.c both fill .reg_holding_cb / .reg_input_cb with
// them). Defining them here overrides all three instances at once with one definition.
//
// Because the hook replaces the library's own implementation, the descriptor machinery
// behind it — mbc_slave_set_descriptor() and the areas it registers — is dead code for us
// and is never called. The register map lives in mb_registers.c instead.
//
// Two details that are easy to get wrong and are load-bearing here:
//   - `address` arrives 1-BASED. The library's own default callbacks start with
//     `address--` before looking anything up, so a request for holding register 0 reaches
//     this function as address == 1.
//   - `reg_buffer` is big-endian, two bytes per register, in request order.

// Resolve the instance the stack is calling us for back to one of our own handles.
// mb_controller_common_t is the public head of every controller object
// (esp_modbus_common.h), and its mb_base is the pointer passed in here.
static int mb_inst_index(mb_base_t *inst)
{
    for (int i = 0; i < MB_INST_COUNT; i++) {
        if ((mb_handles[i] != NULL) &&
            (((mb_controller_common_t *)mb_handles[i])->mb_base == inst)) {
            return i;
        }
    }
    return -1;
}

static mb_event_src_t mb_src_of_inst(int inst_index)
{
    switch (inst_index) {
    case MB_INST_RTU_1: return MB_SRC_RTU_PORT1;
    case MB_INST_RTU_2: return MB_SRC_RTU_PORT2;
    default:            return MB_SRC_TCP;
    }
}

mb_err_enum_t mbc_reg_input_slave_cb(mb_base_t *inst, uint8_t *reg_buffer,
                                     uint16_t address, uint16_t n_regs)
{
    (void)inst;
    if (reg_buffer == NULL) {
        return MB_EINVAL;
    }

    uint16_t base = address - 1;

    // Validate the whole range first: Modbus requires that a request naming even one
    // unknown address is refused with ILLEGAL DATA ADDRESS and transfers nothing.
    for (uint16_t i = 0; i < n_regs; i++) {
        if (mb_reg_find(MB_REG_INPUT, base + i) == NULL) {
            return MB_ENOREG;
        }
    }

    for (uint16_t i = 0; i < n_regs; i++) {
        uint16_t value = 0;
        mb_reg_get(MB_REG_INPUT, base + i, &value);
        reg_buffer[i * 2]       = (uint8_t)(value >> 8);
        reg_buffer[(i * 2) + 1] = (uint8_t)(value & 0xFF);
    }

    return MB_ENOERR;
}

mb_err_enum_t mbc_reg_holding_slave_cb(mb_base_t *inst, uint8_t *reg_buffer,
                                       uint16_t address, uint16_t n_regs,
                                       mb_reg_mode_enum_t mode)
{
    if (reg_buffer == NULL) {
        return MB_EINVAL;
    }

    uint16_t base = address - 1;

    // Same all-or-nothing validation as above, plus the writable flag: a write to a
    // holding register the table marks read-only is an ILLEGAL DATA ADDRESS too, so the
    // whole request is refused before any register changes.
    for (uint16_t i = 0; i < n_regs; i++) {
        const mb_reg_desc_t *desc = mb_reg_find(MB_REG_HOLDING, base + i);
        if (desc == NULL) {
            return MB_ENOREG;
        }
        if ((mode == MB_REG_WRITE) && !desc->writable) {
            return MB_ENOREG;
        }
    }

    if (mode == MB_REG_READ) {
        for (uint16_t i = 0; i < n_regs; i++) {
            uint16_t value = 0;
            mb_reg_get(MB_REG_HOLDING, base + i, &value);
            reg_buffer[i * 2]       = (uint8_t)(value >> 8);
            reg_buffer[(i * 2) + 1] = (uint8_t)(value & 0xFF);
        }
        return MB_ENOERR;
    }

    int inst_index = mb_inst_index(inst);
    if (inst_index < 0) {
        // The stack called us for an instance we did not create. Nothing can be said
        // about where the write came from, so refuse rather than attribute it wrongly.
        ESP_LOGE(TAG, "Register write from an unknown Modbus instance");
        return MB_EILLSTATE;
    }
    mb_event_src_t src = mb_src_of_inst(inst_index);

    for (uint16_t i = 0; i < n_regs; i++) {
        uint16_t value = (uint16_t)((reg_buffer[i * 2] << 8) | reg_buffer[(i * 2) + 1]);
        // One event per register, so an FC16 over four registers emits four events.
        mb_reg_write_from_master(src, MB_REG_HOLDING, base + i, value);
    }

    return MB_ENOERR;
}


// ── RS-485 direction control ─────────────────────────────────────────────────────────

esp_err_t mb_slave_set_tx_disabled(unsigned index, bool disabled)
{
    if (index >= RS485_PORTS_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    const mb_serial_port_t *port = &mb_serial_ports[index];

    // Nothing to gate while the serial half is down: there is no UART holding the pins,
    // and the pins may well belong to someone else — the factory clock_out test stops the
    // slave precisely so it can take both DE lines and both TX lines for its LEDC
    // waveform. Touching them here would drop its port-1 driver mid-waveform.
    // mb_slave_start_serial() re-applies the NVS setting when it hands the pins back to
    // the UARTs, so a value written while the slave was down is not lost, only deferred.
    // The TCP half owns no pins, so its state is irrelevant here.
    if (!mb_serial_running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (disabled == mb_tx_disabled[index]) {
        return ESP_OK; // no state change needed
    }

    if (disabled) {
        // Detach dir_pin from UART control and force LOW (RS-485 TX disabled).
        // In QEMU the wrap shim mirrors these IDF calls onto the virtual native
        // GPIO so the host can observe the software-driven tx_disabled state.
        gpio_reset_pin(port->dir_pin);
        // Set the safe level first, then switch to output, so the pin never
        // drives an undefined level in the window between direction and level.
        gpio_set_level(port->dir_pin, 0);
        gpio_set_direction(port->dir_pin, GPIO_MODE_OUTPUT);
        mb_tx_disabled[index] = true;
        ESP_LOGI(TAG, "UART[%d] TX physically disabled (dir_pin=%d forced LOW)",
                 port->uart, port->dir_pin);
        return ESP_OK;
    }

    // Re-attach dir_pin to UART for automatic half-duplex direction control.
    // The wrap shim mirrors this back to OUTPUT on the virtual native GPIO.
    // TX and RX are passed explicitly instead of UART_PIN_NO_CHANGE, because
    // uart_set_pin() starts by releasing ALL previously configured pins — it disables the
    // TX/RTS pad outputs and re-routes the RX/CTS matrix inputs to constants — and only
    // then reconfigures the signals whose argument is a real pin number. With
    // UART_PIN_NO_CHANGE the bus would stay silent: TX left with its output disabled and
    // RX detached, unrecoverably, since the driver has already forgotten the pin numbers.
    //
    // That unconditional release is an upstream regression, present in esp-idf v5.4.2,
    // v5.4.3, v5.5 and v5.5.1: added by commit 007a497483 ("feat(uart): add pin release
    // process to uart driver") and fixed by 85f0da63fc ("fix(uart): fix release pin logic
    // if switching only one pin"), so it is gone again in v5.4.4+ and v5.5.2+; v5.4.1 and
    // older have no release step at all. Passing the real pin numbers is correct on every
    // one of those versions, so this call does not depend on which IDF the build picks
    // up. To tell whether a given build sits inside the broken window, check the toolchain
    // that is actually installed rather than a version number written down here: both
    // EIM_IDF_VERSION in the Makefile and the Dockerfile's base image now pin an exact
    // tag, but the pins are bumped independently and neither one is what a local machine
    // or a CI agent necessarily has in IDF_PATH. Any build that goes through make does
    // check exactly that: scripts/idf_env.sh reads the version out of the IDF actually in
    // IDF_PATH and fails the build when it differs from EIM_IDF_VERSION — unless
    // IDF_VERSION_CHECK=0 is set, which skips that comparison.
    esp_err_t err = uart_set_pin(port->uart, port->tx_pin, port->rx_pin, port->dir_pin,
                                 UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        // Keep tx_disabled = true and return early: the routing was not restored, so
        // TX really is still off, and the flag has to keep describing the hardware.
        // Argument validation runs before the pin release, so a rejected call leaves the
        // hardware as it was and the disabled state still fits it.
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

// The three helpers below turn the IDF enums back into the values the user configured.
// The numeric enum values are not the human ones — UART_DATA_8_BITS is 3, UART_STOP_BITS_2
// is 3, UART_PARITY_DISABLE is 0 — so printing them raw produced lines like "3 data bits,
// parity 0, stop bits 3". A DIY user reads that line to check their port settings, so the
// same strings the settings API uses are printed instead.
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

static esp_err_t mb_start_serial_port(unsigned index, uint8_t uid)
{
    const mb_serial_port_t *port = &mb_serial_ports[index];

    mb_serial_config_t config;
    ESP_RETURN_ON_ERROR(read_serial_port_config((int)index, &config), TAG,
                        "Port %u: cannot read the serial configuration", index + 1);

    mb_communication_info_t comm = {
        .ser_opts.mode      = MB_RTU,
        .ser_opts.port      = port->uart,
        .ser_opts.uid       = uid,
        .ser_opts.baudrate  = (uint32_t)config.baudrate,
        .ser_opts.data_bits = config.databits,
        .ser_opts.stop_bits = config.stopbits,
        .ser_opts.parity    = config.parity,
    };

    // This is what installs the UART driver (mb_port_ser_create -> uart_param_config +
    // uart_driver_install), so nothing may touch the port's pins before it returns.
    ESP_RETURN_ON_ERROR(mbc_slave_create_serial(&comm, &mb_handles[index]), TAG,
                        "Port %u: mbc_slave_create_serial failed", index + 1);

    // esp-modbus configures the line parameters but NOT the pins or the transceiver
    // direction mode — that is the application's job, and it has to happen after the
    // create above and before the start below.
    ESP_RETURN_ON_ERROR(uart_set_pin(port->uart, port->tx_pin, port->rx_pin, port->dir_pin,
                                     UART_PIN_NO_CHANGE), TAG,
                        "Port %u: uart_set_pin failed", index + 1);

#if QEMU_BUILD
    /* QEMU does not implement RS485 half-duplex mode: uart_set_mode() with
     * UART_MODE_RS485_HALF_DUPLEX sets the RS485_EN bit, causing
     * uart_wait_tx_done() to assert on the TX_DONE interrupt state (uart.c:1348).
     * Use plain UART mode in QEMU — the chardev TCP socket is a simple byte stream
     * without RTS/CTS or RS485 direction control. */
    ESP_RETURN_ON_ERROR(uart_set_mode(port->uart, UART_MODE_UART), TAG,
                        "Port %u: uart_set_mode failed", index + 1);
#else
    ESP_RETURN_ON_ERROR(uart_set_mode(port->uart, UART_MODE_RS485_HALF_DUPLEX), TAG,
                        "Port %u: uart_set_mode failed", index + 1);
#endif

    ESP_RETURN_ON_ERROR(mbc_slave_start(mb_handles[index]), TAG,
                        "Port %u: mbc_slave_start failed", index + 1);

    mb_running_serial[index] = config;
    ESP_LOGI(TAG, "RTU slave on UART[%d] up: uid %u, %d baud, %s data bits, parity %s, %s stop bits",
             port->uart, uid, config.baudrate, databits_str(config.databits),
             parity_str(config.parity), stopbits_str(config.stopbits));
    return ESP_OK;
}

// Tear one instance down and forget its handle. Deleting a serial instance is also what
// releases its UART driver, and with it the TX/RX/DE pins — which is what lets the factory
// clock_out test take those pins for its LEDC waveform.
static void mb_delete_instance(int index)
{
    if (mb_handles[index] == NULL) {
        return;
    }
    mbc_slave_stop(mb_handles[index]);
    mbc_slave_delete(mb_handles[index]);
    mb_handles[index] = NULL;
}

esp_err_t mb_slave_start_serial(void)
{
    // Asked for from here on, whatever this attempt does — the mirror of mb_tcp_wanted in
    // mb_slave_start_tcp(), and for the same reason. app_main calls this once, before any
    // network exists, and carries on whatever it returns; every later start goes through
    // mb_slave_start(). Without this flag the only record that the RS-485 ports are meant
    // to be open would be mb_serial_was_running, which the next stop overwrites with
    // whatever it happens to find — false, if this attempt failed.
    mb_serial_wanted = true;

    if (mb_serial_running) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(mb_registers_init(), TAG, "Register framework init failed");

    // One unit id serves both RS-485 ports and the TCP instance: this is one device with
    // one register map, reachable three ways, not three devices. Both halves read the same
    // setting, so whichever starts first records it.
    uint8_t uid = read_slave_id();
    mb_running_uid = uid;

    // Each port is brought up on its own: one that refuses to start must not take the
    // other down with it, and a device answering on one RS-485 bus is worth more than one
    // answering on neither.
    unsigned started = 0;
    for (unsigned i = 0; i < RS485_PORTS_COUNT; i++) {
        if (mb_start_serial_port(i, uid) == ESP_OK) {
            started++;
        } else {
            // Leaves mb_handles[i] NULL, and that is what marks the port as not running:
            // mb_running_serial[i] describes only the ports that came up, so
            // mb_slave_check_settings_changed() skips this one instead of reporting a
            // change on every settings write forever.
            mb_delete_instance((int)i);
        }
    }

    // "Serial half running" has to mean at least one instance is answering. Claiming it
    // with none up would hide a total failure behind an ESP_OK and let the kill-switch
    // loop below drive pins no UART owns.
    if (started == 0) {
        ESP_LOGE(TAG, "No RS-485 port came up: this device answers Modbus RTU on neither bus");
        return ESP_FAIL;
    }
    if (started < RS485_PORTS_COUNT) {
        ESP_LOGW(TAG, "Only %u of %u RS-485 ports came up", started, (unsigned)RS485_PORTS_COUNT);
    }

    mb_serial_running = true;

    // The kill-switch is re-applied last, on top of the pin routing mb_start_serial_port()
    // just handed to the UART, and after mb_serial_running is set because the setter
    // refuses to touch the pins while the serial half is down. mb_tx_disabled[] is cleared
    // first because that routing IS the enabled state: without the reset, a port that was
    // disabled before the restart would look already-disabled to the setter and keep the
    // UART's pin.
    for (unsigned i = 0; i < RS485_PORTS_COUNT; i++) {
        mb_tx_disabled[i] = false;
        mb_slave_set_tx_disabled(i, setting_items_read_bool(mb_serial_ports[i].tx_dis_key));
    }

    return ESP_OK;
}

esp_err_t mb_slave_start_tcp(void)
{
    // Asked for from here on, whatever this attempt does. app_main calls this once, when
    // the first link comes up, and breaks out of its wait loop regardless of the result —
    // so without this the only record of "a TCP instance is supposed to exist" would be
    // mb_tcp_running, which a failed start leaves false and nothing ever sets again.
    mb_tcp_wanted = true;

    if (mb_tcp_running) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(mb_registers_init(), TAG, "Register framework init failed");

    uint8_t  uid      = read_slave_id();
    uint16_t tcp_port = read_tcp_port();
    mb_running_uid = uid;

    mb_communication_info_t comm = {
        .tcp_opts.mode          = MB_TCP,
        .tcp_opts.port          = tcp_port,
        .tcp_opts.uid           = uid,
        .tcp_opts.addr_type     = MB_IPV4,
        .tcp_opts.ip_addr_table = NULL,     // listen on every address
        // NULL on purpose. esp-modbus reads this only for its mDNS integration and for an
        // IPv6 scope id on the outbound-connect path, which is master-only — a slave
        // accepts, never connects. The mDNS integration is disabled for this project
        // (CONFIG_FMB_MDNS_INTEGRATION_ENABLE=n in sdkconfig.defaults, with the reasons),
        // and that is also what lets this be NULL at all: with it enabled,
        // mbs_port_tcp_create() refuses a NULL netif outright.
        .tcp_opts.ip_netif_ptr  = NULL,
    };

    esp_err_t err = mbc_slave_create_tcp(&comm, &mb_handles[MB_INST_TCP]);
    if (err == ESP_OK) {
        err = mbc_slave_start(mb_handles[MB_INST_TCP]);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Modbus TCP slave failed to start on port %u: %s",
                 tcp_port, esp_err_to_name(err));
        mb_delete_instance(MB_INST_TCP);
        return err;
    }

    mb_tcp_running = true;
    mb_running_tcp_port = tcp_port;
    ESP_LOGI(TAG, "Modbus TCP slave up: uid %u, port %u", uid, tcp_port);
    return ESP_OK;
}

esp_err_t mb_slave_start(void)
{
    // Restores every half this device has ASKED for, and only those — which is what makes a
    // stop/start cycle correct on a device that never got a network: nothing ever asked for
    // a TCP instance there, so none comes up here either. app_main brings each half up
    // through its own entry point above; this one is for the callers that restart what the
    // device is supposed to be running — settings_update.c and the factory clock_out test.
    //
    // The test is on the intent flags, NOT on what the preceding stop found running. A half
    // that was asked for and is down was not "running" for any stop to have found, so
    // restoring only what was running would skip it forever — twice now the cause of two
    // permanently dead RS-485 buses. This is the retry mb_slave_check_settings_changed()
    // reports a change for, and it is what makes a failed start recoverable rather than
    // terminal.
    esp_err_t ret = ESP_OK;

    if (mb_serial_wanted) {
        esp_err_t err = mb_slave_start_serial();
        if (err != ESP_OK) {
            ret = err;
        }
    }
    if (mb_tcp_wanted) {
        esp_err_t err = mb_slave_start_tcp();
        if (err != ESP_OK) {
            ret = err;
        }
    }

    return ret;
}

esp_err_t mb_slave_stop(void)
{
    // Nothing is running: nothing to tear down, and nothing worth reporting. Keeping the
    // previous stop's record is all this early return does now — what comes back is decided
    // by the intent flags, not by these latches, and the callers are kept apart by
    // mb_slave_lock_take() (see mb_slave.h), not by this.
    if (!mb_serial_running && !mb_tcp_running) {
        return ESP_OK;
    }

    // Read before anything is torn down, purely so the log line below can say what this
    // stop found. Nothing reads them afterwards.
    mb_serial_was_running = mb_serial_running;
    mb_tcp_was_running    = mb_tcp_running;

    for (int i = 0; i < MB_INST_COUNT; i++) {
        mb_delete_instance(i);
    }

    mb_serial_running = false;
    mb_tcp_running = false;
    ESP_LOGI(TAG, "Modbus slave stopped (serial: %s, TCP: %s)",
             mb_serial_was_running ? "was up" : "was down",
             mb_tcp_was_running ? "was up" : "was down");
    return ESP_OK;
}


// ── The ownership lock ───────────────────────────────────────────────────────────────

void mb_slave_lock_init(void)
{
    // Idempotent, and deliberately not re-creating an existing mutex: a second create
    // would hand back a new handle and strand whatever task is holding the old one.
    if (mb_lock != NULL) {
        return;
    }
    // Static allocation: xSemaphoreCreateMutexStatic() returns the buffer it was handed and
    // cannot fail, so there is no half-initialised state and no error for app_main to
    // decide what to do with.
    mb_lock = xSemaphoreCreateMutexStatic(&mb_lock_buffer);
}

bool mb_slave_lock_take(uint32_t timeout_ms)
{
    if (mb_lock == NULL) {
        // Only reachable if app_main never called mb_slave_lock_init(). Refusing is the
        // safe answer: the caller declines to touch the stack rather than running the one
        // teardown this lock exists to serialise with no serialisation at all.
        ESP_LOGE(TAG, "Modbus slave lock used before mb_slave_lock_init()");
        return false;
    }
    TickType_t ticks = (timeout_ms == MB_SLAVE_LOCK_WAIT_FOREVER)
                       ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTake(mb_lock, ticks) == pdTRUE);
}

void mb_slave_lock_give(void)
{
    if (mb_lock == NULL) {
        return;
    }
    xSemaphoreGive(mb_lock);
}
