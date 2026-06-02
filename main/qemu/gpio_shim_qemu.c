// Transparent interception of the real ESP-IDF GPIO/UART API for the QEMU build.
//
// The same firmware code configures and drives both real hardware pins and the
// emulated ones: there is NO QEMU-specific GPIO code in the firmware modules.
// Instead, the QEMU build links with -Wl,--wrap=NAME for each intercepted
// function (see main/CMakeLists.txt, QEMU branch). The linker redirects every
// call site to __wrap_NAME here, while __real_NAME still reaches the original
// ESP-IDF implementation.
//
// Each wrapper does two things:
//   1) mirrors the call into the virtual native GPIO model (so the host can
//      observe direction/level and the model can enforce direction rules), and
//   2) ALWAYS forwards to __real_NAME so non-tracked pins and the SoC/QEMU
//      state behave normally.
//
// Direction is DERIVED from the ESP-IDF gpio mode bits, never duplicated:
//   output-capable = (mode & GPIO_MODE_DEF_OUTPUT) -> OUTPUT
//   else input-capable = (mode & GPIO_MODE_DEF_INPUT) -> INPUT
//   else (DISABLE) -> UNCONFIGURED.

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "hal/gpio_types.h"

#include "virtual_io_qemu.h"
#include "gpio_dir_map.h"

// Originals provided by the linker (--wrap). Same signatures as the IDF funcs.
esp_err_t __real_gpio_config(const gpio_config_t *pGPIOConfig);
esp_err_t __real_gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t __real_gpio_reset_pin(gpio_num_t gpio_num);
esp_err_t __real_gpio_set_level(gpio_num_t gpio_num, uint32_t level);
int       __real_gpio_get_level(gpio_num_t gpio_num);
esp_err_t __real_uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num,
                              int rts_io_num, int cts_io_num);

esp_err_t __wrap_gpio_config(const gpio_config_t *pGPIOConfig)
{
    if (pGPIOConfig != NULL) {
        vio_dir_state_t dir = dir_from_mode(pGPIOConfig->mode);
        uint64_t mask = pGPIOConfig->pin_bit_mask;
        for (int pin = 0; pin < GPIO_NUM_MAX; pin++) {
            if (mask & (1ULL << pin)) {
                vio_native_set_direction(pin, dir);
            }
        }
    }
    return __real_gpio_config(pGPIOConfig);
}

esp_err_t __wrap_gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    vio_native_set_direction((int)gpio_num, dir_from_mode(mode));
    return __real_gpio_set_direction(gpio_num, mode);
}

esp_err_t __wrap_gpio_reset_pin(gpio_num_t gpio_num)
{
    // IDF gpio_reset_pin() routes the pad to the GPIO matrix and enables input.
    vio_native_set_direction((int)gpio_num, VIO_DIR_INPUT);
    return __real_gpio_reset_pin(gpio_num);
}

// gpio_set_level is the FIRMWARE write path. IRAM_ATTR keeps only THIS wrapper's
// entry/dispatch in IRAM; it does NOT make the call ISR/flash-fault safe end to
// end, because __real_gpio_set_level (and the model's log/mutex handling) live in
// flash and would still fault if the cache were disabled. In practice that is
// fine: the tracked pins (DE lines, button) are written only from task context,
// and non-tracked pins fall through to __real_gpio_set_level the same as without
// this shim — so behavior matches the unwrapped firmware. The IRAM_ATTR is kept
// merely so this thin wrapper does not add a flash fetch on the hot dispatch.
IRAM_ATTR esp_err_t __wrap_gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    if (!vio_native_is_tracked((int)gpio_num)) {
        return __real_gpio_set_level(gpio_num, level);
    }
    // Tracked pin: enforce "firmware may write only OUTPUT pins" and mirror the
    // level into the model (emits G on change, or a V violation if illegal).
    vio_native_fw_set_level((int)gpio_num, (int)level);
    // Forward to the real call too (harmless on the QEMU GPIO stub).
    return __real_gpio_set_level(gpio_num, level);
}

int __wrap_gpio_get_level(gpio_num_t gpio_num)
{
    if (vio_native_is_tracked((int)gpio_num)) {
        // Serve from the model: the QEMU GPIO stub would just return 0.
        return vio_native_get_level((int)gpio_num);
    }
    return __real_gpio_get_level(gpio_num);
}

esp_err_t __wrap_uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num,
                              int rts_io_num, int cts_io_num)
{
    // Scope the native-bus model to the RS-485 DE/direction line only. That line
    // is the RTS pin: when UART drives RTS it is a firmware-driven OUTPUT. We
    // deliberately ignore tx/rx/cts here — those carry the byte stream / flow
    // control, which is not part of the direction model the host observes.
    if (rts_io_num != UART_PIN_NO_CHANGE) {
        vio_native_set_direction(rts_io_num, VIO_DIR_OUTPUT);
        // Attaching RTS hands the DE line back to the UART; its idle state is
        // TX-enabled = HIGH (real RS-485 DE idles high). QEMU does not emulate
        // per-frame RTS toggling, so mirror that idle level here so the host
        // observes the TX-enabled state after a tx_disabled=false re-enable.
        vio_native_fw_set_level(rts_io_num, 1);
    }
    return __real_uart_set_pin(uart_num, tx_io_num, rx_io_num, rts_io_num, cts_io_num);
}
