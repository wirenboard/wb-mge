#pragma once

#include "freertos/FreeRTOS.h"

/* serial.h only needs the EventGroupHandle_t type for its struct fields; no
 * event-group operations are exercised by the mb_device unit test. */
typedef void *EventGroupHandle_t;
