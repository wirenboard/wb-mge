#pragma once

#include "esp_err.h"

#include <stdint.h>
#include <stddef.h>

typedef void *i2c_master_bus_handle_t;

typedef int i2c_port_num_t;

typedef enum {
    GPIO_NUM_NC = -1,   /*!< Use to signal not connected to S/W */
    GPIO_NUM_0 = 0,     /*!< GPIO0, input and output */
    GPIO_NUM_1 = 1,     /*!< GPIO1, input and output */
    GPIO_NUM_2 = 2,     /*!< GPIO2, input and output */
    GPIO_NUM_3 = 3,     /*!< GPIO3, input and output */
    GPIO_NUM_4 = 4,     /*!< GPIO4, input and output */
    GPIO_NUM_5 = 5,     /*!< GPIO5, input and output */
    GPIO_NUM_6 = 6,     /*!< GPIO6, input and output */
    GPIO_NUM_7 = 7,     /*!< GPIO7, input and output */
    GPIO_NUM_8 = 8,     /*!< GPIO8, input and output */
    GPIO_NUM_9 = 9,     /*!< GPIO9, input and output */
    GPIO_NUM_10 = 10,   /*!< GPIO10, input and output */
    GPIO_NUM_11 = 11,   /*!< GPIO11, input and output */
    GPIO_NUM_12 = 12,   /*!< GPIO12, input and output */
    GPIO_NUM_13 = 13,   /*!< GPIO13, input and output */
    GPIO_NUM_14 = 14,   /*!< GPIO14, input and output */
    GPIO_NUM_15 = 15,   /*!< GPIO15, input and output */
    GPIO_NUM_16 = 16,   /*!< GPIO16, input and output */
    GPIO_NUM_17 = 17,   /*!< GPIO17, input and output */
    GPIO_NUM_18 = 18,   /*!< GPIO18, input and output */
    GPIO_NUM_19 = 19,   /*!< GPIO19, input and output */
    GPIO_NUM_20 = 20,   /*!< GPIO20, input and output */
    GPIO_NUM_21 = 21,   /*!< GPIO21, input and output */
    GPIO_NUM_22 = 22,   /*!< GPIO22, input and output */
    GPIO_NUM_23 = 23,   /*!< GPIO23, input and output */
    GPIO_NUM_25 = 25,   /*!< GPIO25, input and output */
    GPIO_NUM_26 = 26,   /*!< GPIO26, input and output */
    GPIO_NUM_27 = 27,   /*!< GPIO27, input and output */
    GPIO_NUM_28 = 28,   /*!< GPIO28, input and output */
    GPIO_NUM_29 = 29,   /*!< GPIO29, input and output */
    GPIO_NUM_30 = 30,   /*!< GPIO30, input and output */
    GPIO_NUM_31 = 31,   /*!< GPIO31, input and output */
    GPIO_NUM_32 = 32,   /*!< GPIO32, input and output */
    GPIO_NUM_33 = 33,   /*!< GPIO33, input and output */
    GPIO_NUM_34 = 34,   /*!< GPIO34, input mode only */
    GPIO_NUM_35 = 35,   /*!< GPIO35, input mode only */
    GPIO_NUM_36 = 36,   /*!< GPIO36, input mode only */
    GPIO_NUM_37 = 37,   /*!< GPIO37, input mode only */
    GPIO_NUM_38 = 38,   /*!< GPIO38, input mode only */
    GPIO_NUM_39 = 39,   /*!< GPIO39, input mode only */
    GPIO_NUM_MAX,
} gpio_num_t;

typedef enum {
    // For CPU domain
    SOC_MOD_CLK_CPU = 1,                       /*!< CPU_CLK can be sourced from XTAL, PLL, RC_FAST, or APLL by configuring soc_cpu_clk_src_t */
    // For RTC domain
    SOC_MOD_CLK_RTC_FAST,                      /*!< RTC_FAST_CLK can be sourced from XTAL_D4 or RC_FAST by configuring soc_rtc_fast_clk_src_t */
    SOC_MOD_CLK_RTC_SLOW,                      /*!< RTC_SLOW_CLK can be sourced from RC_SLOW, XTAL32K, or RC_FAST_D256 by configuring soc_rtc_slow_clk_src_t */
    // For digital domain: peripherals, WIFI, BLE
    SOC_MOD_CLK_APB,                           /*!< APB_CLK is highly dependent on the CPU_CLK source */
    SOC_MOD_CLK_PLL_D2,                        /*!< PLL_D2_CLK is derived from PLL, it has a fixed divider of 2 */
    SOC_MOD_CLK_PLL_F160M,                     /*!< PLL_F160M_CLK is derived from PLL, and has a fixed frequency of 160MHz */
    SOC_MOD_CLK_XTAL32K,                       /*!< XTAL32K_CLK comes from the external 32kHz crystal, passing a clock gating to the peripherals */
    SOC_MOD_CLK_RC_FAST,                       /*!< RC_FAST_CLK comes from the internal 8MHz rc oscillator, passing a clock gating to the peripherals */
    SOC_MOD_CLK_RC_FAST_D256,                  /*!< RC_FAST_D256_CLK comes from the internal 8MHz rc oscillator, divided by 256, and passing a clock gating to the peripherals */
    SOC_MOD_CLK_XTAL,                          /*!< XTAL_CLK comes from the external crystal (2~40MHz) */
    SOC_MOD_CLK_REF_TICK,                      /*!< REF_TICK is derived from APB, it has a fixed frequency of 1MHz even when APB frequency changes */
    SOC_MOD_CLK_APLL,                          /*!< APLL is sourced from PLL, and its frequency is configurable through APLL configuration registers */
    SOC_MOD_CLK_INVALID,                       /*!< Indication of the end of the available module clock sources */
} soc_module_clk_t;

typedef enum {
    I2C_CLK_SRC_APB = SOC_MOD_CLK_APB,
    I2C_CLK_SRC_DEFAULT = SOC_MOD_CLK_APB,
} i2c_clock_source_t;

typedef struct {
    i2c_port_num_t i2c_port;              /*!< I2C port number, `-1` for auto selecting, (not include LP I2C instance) */
    gpio_num_t sda_io_num;                /*!< GPIO number of I2C SDA signal, pulled-up internally */
    gpio_num_t scl_io_num;                /*!< GPIO number of I2C SCL signal, pulled-up internally */
    union {
        i2c_clock_source_t clk_source;    /*!< Clock source of I2C master bus */
    };
    uint8_t glitch_ignore_cnt;            /*!< If the glitch period on the line is less than this value, it can be filtered out, typically value is 7 (unit: I2C module clock cycle)*/
    int intr_priority;                    /*!< I2C interrupt priority, if set to 0, driver will select the default priority (1,2,3). */
    size_t trans_queue_depth;             /*!< Depth of internal transfer queue, increase this value can support more transfers pending in the background, only valid in asynchronous transaction. (Typically max_device_num * per_transaction)*/
    struct {
        uint32_t enable_internal_pullup: 1;  /*!< Enable internal pullups. Note: This is not strong enough to pullup buses under high-speed frequency. Recommend proper external pull-up if possible */
        uint32_t allow_pd:               1;  /*!< If set, the driver will backup/restore the I2C registers before/after entering/exist sleep mode.
                                              By this approach, the system can power off I2C's power domain.
                                              This can save power, but at the expense of more RAM being consumed */
    } flags;                                 /*!< I2C master config flags */
} i2c_master_bus_config_t;

typedef enum {
    I2C_NUM_0 = 0,              /*!< I2C port 0 */
    I2C_NUM_1,                  /*!< I2C port 1 */
    LP_I2C_NUM_0,               /*< LP_I2C port 0 */
    I2C_NUM_MAX,                /*!< I2C port max */
} i2c_port_t;

extern esp_err_t mock_i2c_new_master_bus_return;
extern int mock_i2c_new_master_bus_called;
extern i2c_master_bus_handle_t mock_i2c_bus_handle;
extern i2c_master_bus_config_t mock_i2c_bus_config;

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config, i2c_master_bus_handle_t *ret_bus_handle);
