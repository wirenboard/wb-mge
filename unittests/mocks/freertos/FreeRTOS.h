#pragma once

#include <stdint.h>

#define pdFALSE                                  ( ( BaseType_t ) 0 )
#define pdTRUE                                   ( ( BaseType_t ) 1 )

#define pdPASS                                   ( pdTRUE )
#define pdFAIL                                   ( pdFALSE )

#define portMAX_DELAY                            ( TickType_t ) 0xffffffffUL

typedef uint32_t TickType_t;

typedef int BaseType_t;
typedef unsigned UBaseType_t;
