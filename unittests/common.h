#pragma once

#include <stdio.h>

#define COLOR_RED           "\033[0;31m"
#define COLOR_YELLOW        "\033[0;93m"
#define COLOR_GREEN         "\033[0;32m"
#define COLOR_RESET         "\033[0m"

#define PRINT_E(TEXT)       COLOR_RED TEXT COLOR_RESET
#define PRINT_W(TEXT)       COLOR_YELLOW TEXT COLOR_RESET
#define PRINT_I(TEXT)       COLOR_GREEN TEXT COLOR_RESET

#define TEST_OK     0
#define TEST_ERR    -1

#define TEST_FAILED(...)                                                            \
    do {                                                                            \
        printf(PRINT_E("TEST FAILED (%s): %s:%d: "), __func__, __FILE__, __LINE__); \
        printf(__VA_ARGS__);                                                        \
        printf("\n");                                                               \
    } while (0)

#define TEST_PASSED()                              \
    do {                                           \
        printf(PRINT_I("%s: passed\n"), __func__); \
    } while (0)
