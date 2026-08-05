#include "gpio_dir_map.h"
#include "hal/gpio_types.h"       // GPIO_MODE_DEF_*

// Map an ESP-IDF gpio mode to the virtual model's direction state. Output takes
// precedence (INPUT_OUTPUT is firmware-driven), then input, else unconfigured.
vio_dir_state_t dir_from_mode(gpio_mode_t mode)
{
    if (mode & GPIO_MODE_DEF_OUTPUT) {
        return VIO_DIR_OUTPUT;
    }
    if (mode & GPIO_MODE_DEF_INPUT) {
        return VIO_DIR_INPUT;
    }
    return VIO_DIR_UNCONFIGURED; // GPIO_MODE_DISABLE
}
