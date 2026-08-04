#pragma once

#include <stdint.h>

#define pdFALSE                                  ( ( BaseType_t ) 0 )
#define pdTRUE                                   ( ( BaseType_t ) 1 )

#define pdPASS                                   ( pdTRUE )
#define pdFAIL                                   ( pdFALSE )

#define portMAX_DELAY                            ( TickType_t ) 0xffffffffUL

/* Idle task priority — used by modules that create helper tasks. */
#define tskIDLE_PRIORITY                         ( ( UBaseType_t ) 0U )

typedef uint32_t TickType_t;

typedef int BaseType_t;
typedef unsigned UBaseType_t;

/* Caller-provided storage for a statically allocated semaphore/mutex. The real one is an
 * opaque struct sized to match FreeRTOS's internal Queue_t; here only its address matters,
 * because the mock hands that address back as the handle (see semphr.c). Declared in
 * FreeRTOS.h rather than semphr.h to mirror where the real FreeRTOS puts it. */
typedef struct {
    void     *dummy_ptr[4];
    uint32_t  dummy_u32[4];
} StaticSemaphore_t;

/* FreeRTOS tick rate — keep in sync with task.c mock */
#ifndef CONFIG_FREERTOS_HZ
#define CONFIG_FREERTOS_HZ      500u
#endif

/* The real FreeRTOSConfig.h derives configTICK_RATE_HZ from CONFIG_FREERTOS_HZ;
 * mirror that here so code doing tick arithmetic compiles unchanged in tests.
 * A suite may still override it on the command line (-D). */
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ      CONFIG_FREERTOS_HZ
#endif

#define pdTICKS_TO_MS(ticks)    ( (uint32_t)(ticks) * 1000u / CONFIG_FREERTOS_HZ )
