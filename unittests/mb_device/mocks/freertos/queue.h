#pragma once

#include "freertos/FreeRTOS.h"

/* serial.h only needs the QueueHandle_t type for its struct fields; no queue
 * operations are exercised by the mb_device unit test. */
typedef void *QueueHandle_t;
