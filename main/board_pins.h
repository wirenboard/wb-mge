#pragma once

/*
 * Board-dependent RS485 UART GPIO map, selected at build time by device
 * signature (Makefile MODEL_LIST / TARGET). Each board header defines the same
 * SERIAL_{INPUT,OUTPUT,IO}_PIN_{1,2} symbols with board-specific pin numbers.
 */

#include "driver/gpio.h"

#if defined(MODEL_mgu_v1)
    #include "boards/mgu_v1.h"
#elif defined(MODEL_mge_v3)
    #include "boards/mge_v3.h"
#elif defined(__unittest_env__)
    /* Host unit tests compile bridge.c without a MODEL_* signature; use the
       WB-MGE pin set so pin-dependent tests keep their expected values. */
    #include "boards/mge_v3.h"
#else
    #error "Unknown device signature: no RS485 board pin map"
#endif
