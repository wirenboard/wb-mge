#pragma once

/* Minimal self-contained FreeRTOS type mock for the mb_device unit test.
 * Provides only the scalar types used transitively by serial.h and the
 * stack-usage helpers referenced by mb_device.c. */

#include <stdint.h>

#define pdFALSE   ((BaseType_t)0)
#define pdTRUE    ((BaseType_t)1)
#define pdPASS    (pdTRUE)
#define pdFAIL    (pdFALSE)

#define portMAX_DELAY   ((TickType_t)0xffffffffUL)

typedef uint32_t TickType_t;
typedef int      BaseType_t;
typedef unsigned UBaseType_t;

/* Tick rate, as the real FreeRTOSConfig.h derives it — modbus_helpers.c does
 * tick arithmetic (modbus_rtu_response_timeout_ticks) and is linked into this
 * test as an auxiliary source. */
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ  500u
#endif

/* Stack word type — chosen so that sizeof(StackType_t) == 1, which keeps the
 * mb_device.c "high-water-mark words -> bytes" conversion a 1:1 mapping that
 * the test can reason about exactly. */
typedef uint8_t StackType_t;
