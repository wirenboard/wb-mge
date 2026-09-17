#include "setting_items.h"
#include "esp_log.h"

#include <stdbool.h>

#include "mb_role.h"
#include "rs485_control.h"
#include "mio_control.h"

// The 485_tx_dis_N kill-switch reaches the hardware through whichever Modbus role owns the
// UARTs — and it matters in both: a slave that must not answer on a bus, and a master that
// must not poll one. In the "none" role no UART is installed at all, so there is no DE pin
// under UART control to take away and the setting has nothing to apply to.
#if WB_MB_ROLE_SLAVE
    #include "mb_slave.h"
    #define MB_STACK_SET_TX_DISABLED(index, disabled)   mb_slave_set_tx_disabled(index, disabled)
#elif WB_MB_ROLE_MASTER
    #include "mb_master.h"
    #define MB_STACK_SET_TX_DISABLED(index, disabled)   mb_master_set_tx_disabled(index, disabled)
#else
    // Only the "none" role reaches for the pins directly, so these two headers are included
    // only there: the host unit-test build compiles this file with no IDF driver layer
    // behind it, and it is a slave-role build, so it never sees them.
    #include "board_pins.h"
    #include "driver/gpio.h"
#endif

static const char *TAG = "update_rs485_mio_gpio_states";

// Updates the state of pull-ups, power and RS485 terminators according to current settings.
// On hardware this drives the TCA9535 GPIO expander; in QEMU it drives the virtual
// expander shadow (see virtual_io_qemu.c), so it runs in both builds.
void update_rs485_control(void)
{
    bool pullup_1_enabled = setting_items_read_bool(KEY_485_FAIL_SAFE_1);
    bool pullup_2_enabled = setting_items_read_bool(KEY_485_FAIL_SAFE_2);
    bool term_1_enabled = setting_items_read_bool(KEY_485_TERM_1);
    bool term_2_enabled = setting_items_read_bool(KEY_485_TERM_2);
    bool vout_enabled = setting_items_read_bool(KEY_485_VOUT);

    rs485_pupd_on_off(RS485_1, pullup_1_enabled);
    rs485_pupd_on_off(RS485_2, pullup_2_enabled);
    rs485_term_on_off(RS485_1, term_1_enabled);
    rs485_term_on_off(RS485_2, term_2_enabled);
    rs485_bus_vout_on_off(vout_enabled);

    ESP_LOGI(TAG, "RS485 control updated");
}

// Updates the IO bus (MIO) enable state according to current settings.
// On hardware this drives the TCA9535 GPIO expander; in QEMU it drives the virtual
// expander shadow (see virtual_io_qemu.c), so it runs in both builds.
void update_io_bus_control(void)
{
    bool io_bus_enabled = setting_items_read_bool(KEY_IO_BUS_ENABLED);

    mio_control_io_bus_onoff(io_bus_enabled);
    ESP_LOGI(TAG, "IO bus control updated: %s", io_bus_enabled ? "enabled" : "disabled");
}

// Applies the tx_disabled setting to the running serial ports for both RS-485 ports.
// It does not touch the GPIO expander, but it is not a purely software flag either:
// down in mb_slave_set_tx_disabled() it takes the port's dir_pin (the SoC-side DE/RE line,
// SERIAL_IO_PIN_1/2) away from the UART and drives it LOW; the re-enable direction hands
// back not only that dir_pin but also the port's TX and RX pins (SERIAL_OUTPUT_PIN_1/2,
// SERIAL_INPUT_PIN_1/2), which uart_set_pin() has to re-apply explicitly.
//
// Those pins have another owner part of the time — the factory clock_out test holds both
// DE lines as plain GPIOs for its whole run (port 1 raised so its transceiver transmits,
// port 2 held LOW so its transceiver stays silent) and both TX pins through the LEDC that
// generates the waveform. Callers do NOT have to check for that themselves, which is why
// settings_update() calls this unconditionally: mb_slave_set_tx_disabled() refuses with
// ESP_ERR_INVALID_STATE while the serial half is down, and the serial half is down for the
// duration of that test — stopping it is what released the pins to the test.
//
// The test is not the only window in which the serial half is down: it is also down inside
// settings_update_task's own mb_slave_stop()/mb_slave_start() pair, and after a start that
// failed. That takes nothing away from the argument, because the refusal keys on the state
// and not on who caused it, and because the deferral is the same either way: a value
// written in any such window is not lost, mb_slave_start() re-reads the NVS setting when it
// hands the pins back to the UARTs.
void update_serial_tx_disabled(void)
{
#if (!WB_MB_ROLE_NONE)
    MB_STACK_SET_TX_DISABLED(0, setting_items_read_bool(KEY_485_TX_DISABLED_1));
    MB_STACK_SET_TX_DISABLED(1, setting_items_read_bool(KEY_485_TX_DISABLED_2));
#endif
}

// Leave both RS-485 transceivers in receive, once, at boot. Called from app_main before the
// Modbus stack is brought up.
//
// A no-op in the slave and master roles, and deliberately so: there the DE/RE lines belong
// to the UART from mb_*_start_serial() onwards, which drives them by hardware for
// half-duplex direction control and re-applies the 485_tx_dis_N kill-switch on top. Parking
// them here would only be undone microseconds later.
//
// In the "none" role nothing else in the firmware ever touches them. No UART is installed,
// update_serial_tx_disabled() above has nothing to call, and wb_test.c takes the pins only
// for the duration of the factory test - so without this the lines would sit in their reset
// state for the whole life of the device. That is not a safe default: a reset pad is an
// input on an internal pull-up, the WB-MGE pulldown (R4) cannot pull a driven pad down and
// WB-MGU's port-2 DE line (GPIO13) has no external pulldown at all. A transceiver that comes
// up enabled drives a shared half-duplex bus continuously and blocks every other
// transmitter on it. It is the same reason the clock_out test parks its port-2 DE line LOW
// rather than releasing it (see main/wb_test.c, CLK_OUT_DE_PARK_PIN).
//
// Driven LOW rather than set from 485_tx_dis_N: with no UART behind them there is no
// "enabled" state to select, so the setting has nothing to choose between. The saved value
// is not lost - it is simply not applicable in this role, and a device reflashed into the
// slave or master role picks it up again on the next boot.
//
// Same idiom as mb_*_set_tx_disabled(): level before direction, so the pad never drives an
// undefined level in the window between the two.
void park_serial_direction_pins(void)
{
#if WB_MB_ROLE_NONE
    static const int dir_pins[] = {SERIAL_IO_PIN_1, SERIAL_IO_PIN_2};

    for (unsigned i = 0; i < (sizeof(dir_pins) / sizeof(dir_pins[0])); i++) {
        gpio_reset_pin(dir_pins[i]);
        gpio_set_level(dir_pins[i], 0);
        gpio_set_direction(dir_pins[i], GPIO_MODE_OUTPUT);
    }
    ESP_LOGI(TAG, "RS485 direction pins (%d, %d) parked LOW: no Modbus role owns them",
             SERIAL_IO_PIN_1, SERIAL_IO_PIN_2);
#endif
}
