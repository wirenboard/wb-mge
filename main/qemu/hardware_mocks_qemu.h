#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Hardware mock functions for QEMU builds.
// Only voltage_monitor stays excluded and mocked; all other hardware-logic
// modules run for real against the virtual IO bus (see virtual_io_qemu.c).

// System voltage
float voltage_monitor_get_sys_voltage(void);

#ifdef __cplusplus
}
#endif
