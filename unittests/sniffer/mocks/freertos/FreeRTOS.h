#pragma once

/* Pull in the common FreeRTOS mock definitions */
#include_next "freertos/FreeRTOS.h"

/* Additional stubs required by sniffer.c */

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED  0

/* taskENTER_CRITICAL / taskEXIT_CRITICAL — no-ops in unit tests */
#define taskENTER_CRITICAL(mux)    (void)(mux)
#define taskEXIT_CRITICAL(mux)     (void)(mux)


