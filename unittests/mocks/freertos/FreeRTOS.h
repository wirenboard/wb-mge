#pragma once

#include <stdint.h>

#define pdPASS            1
#define pdFAIL            0
#define portMAX_DELAY     ( TickType_t ) 0xffff

typedef uint32_t TickType_t;

typedef int BaseType_t;
typedef unsigned UBaseType_t;

typedef void *TaskHandle_t;
typedef void *EventGroupHandle_t;
