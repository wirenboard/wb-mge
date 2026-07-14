#pragma once

// Minimal mock of the ESP32 SoC capabilities used by virtual_io_qemu.c.
// Mirrors components/soc/esp32/include/soc/soc_caps.h:
//   valid GPIOs are 0..39 except 24 and 28..31; GPIO >= 34 are input-only.
#define SOC_GPIO_VALID_GPIO_MASK \
    (0xFFFFFFFFFFULL & ~((1ULL << 24) | (1ULL << 28) | (1ULL << 29) | (1ULL << 30) | (1ULL << 31)))
#define SOC_GPIO_VALID_OUTPUT_GPIO_MASK \
    (SOC_GPIO_VALID_GPIO_MASK & ~((1ULL << 34) | (1ULL << 35) | (1ULL << 36) | (1ULL << 37) | (1ULL << 38) | (1ULL << 39)))
