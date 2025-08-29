#pragma once

#include <stdio.h>

#if __has_include("console_log_config.h")
    #include "console_log_config.h"
#endif

//------------------------------------------------------------------------------

#define CONS_COLOR_BRIGHT_BLUE      "\033[94m"
#define CONS_COLOR_BLUE             "\033[34m"
#define CONS_COLOR_LIGHT_BLUE       "\033[36m"
#define CONS_COLOR_GREEN            "\033[32m"
#define CONS_COLOR_YELLOW           "\033[33m"
#define CONS_COLOR_ORANGE           "\033[38;5;208m"
#define CONS_COLOR_RED              "\033[31m"
#define CONS_COLOR_MAGENTA          "\033[35m"
#define CONS_COLOR_DEFAULT          "\033[0m"

#define CONS_COLOR_TRACE            CONS_COLOR_BRIGHT_BLUE
#define CONS_COLOR_DEBUG            CONS_COLOR_LIGHT_BLUE
#define CONS_COLOR_INFO             CONS_COLOR_GREEN
#define CONS_COLOR_WARN             CONS_COLOR_YELLOW
#define CONS_COLOR_ERROR            CONS_COLOR_RED
#define CONS_COLOR_FATAL            CONS_COLOR_MAGENTA

#define LOG_LEVEL_TRACE             6
#define LOG_LEVEL_DEBUG             5
#define LOG_LEVEL_INFO              4
#define LOG_LEVEL_WARN              3
#define LOG_LEVEL_ERROR             2
#define LOG_LEVEL_FATAL             1
#define LOG_LEVEL_DISABLED          0

//------------------------------------------------------------------------------

#ifndef LOG_LEVEL
    #define LOG_LEVEL               LOG_LEVEL_WARN
#endif

#ifndef LOG_ENABLE_COLORS
    #define LOG_ENABLE_COLORS       1
#endif

//------------------------------------------------------------------------------

#ifndef LOG_ENABLE_TRACE
    #define LOG_ENABLE_TRACE        (LOG_LEVEL >= LOG_LEVEL_TRACE)
#endif

#ifndef LOG_ENABLE_DEBUG
    #define LOG_ENABLE_DEBUG        (LOG_LEVEL >= LOG_LEVEL_DEBUG)
#endif

#ifndef LOG_ENABLE_INFO
    #define LOG_ENABLE_INFO         (LOG_LEVEL >= LOG_LEVEL_INFO)
#endif

#ifndef LOG_ENABLE_WARN
    #define LOG_ENABLE_WARN         (LOG_LEVEL >= LOG_LEVEL_WARN)
#endif

#ifndef LOG_ENABLE_ERROR
    #define LOG_ENABLE_ERROR        (LOG_LEVEL >= LOG_LEVEL_ERROR)
#endif

#ifndef LOG_ENABLE_FATAL
    #define LOG_ENABLE_FATAL        (LOG_LEVEL >= LOG_LEVEL_FATAL)
#endif

//------------------------------------------------------------------------------

#if (LOG_ENABLE_COLORS > 0)
    #define LOG_COLORED_STR(color, str)     color str CONS_COLOR_DEFAULT
#else
    #define LOG_COLORED_STR(color, str)     str
#endif

#define LOG_PRINT_MACRO(mode, fmt, ...)     printf(LOG_COLORED_STR(CONS_COLOR_##mode, "%-8s") fmt "\n", #mode, ##__VA_ARGS__)

#define LOG_PRINT_IF_ENABLED(mode, ...)         \
    do {                                        \
        if (LOG_ENABLE_##mode) {                \
            LOG_PRINT_MACRO(mode, __VA_ARGS__); \
        }                                       \
    } while (0)

//------------------------------------------------------------------------------

#define LOG_TRACE(fmt, ...)                     LOG_PRINT_IF_ENABLED(TRACE, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)                     LOG_PRINT_IF_ENABLED(DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)                      LOG_PRINT_IF_ENABLED(INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)                      LOG_PRINT_IF_ENABLED(WARN, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)                     LOG_PRINT_IF_ENABLED(ERROR, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...)                     LOG_PRINT_IF_ENABLED(FATAL, fmt, ##__VA_ARGS__)

#define LOG_MESSAGE(fmt, ...)                   printf(fmt "\n", ##__VA_ARGS__)
#define LOG_COLORED_MESSAGE(color, fmt, ...)    printf(LOG_COLORED_STR(color, fmt) "\n", ##__VA_ARGS__)

//------------------------------------------------------------------------------
