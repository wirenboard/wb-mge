// Host-unit tests for the QEMU GPIO/UART wrap shim (main/qemu/gpio_shim_qemu.c)
// and the extracted direction mapper (main/qemu/gpio_dir_map.c).
//
// On the host there is no linker --wrap: the __wrap_* functions are compiled in
// directly and called by name. The model API (vio_native_*) and the originals
// (__real_*) are provided here as spy stubs that record calls into globals.

#include "unity.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "hal/gpio_types.h"

#include "gpio_dir_map.h"
#include "virtual_io_qemu.h"

// --- __wrap_* prototypes (signatures match gpio_shim_qemu.c) --------------------
esp_err_t __wrap_gpio_config(const gpio_config_t *pGPIOConfig);
esp_err_t __wrap_gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t __wrap_gpio_reset_pin(gpio_num_t gpio_num);
esp_err_t __wrap_gpio_set_level(gpio_num_t gpio_num, uint32_t level);
int       __wrap_gpio_get_level(gpio_num_t gpio_num);
esp_err_t __wrap_uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num,
                              int rts_io_num, int cts_io_num);

// --- Spy state ------------------------------------------------------------------
#define SPY_N 64

// vio_native_set_direction records
static int             sd_num[SPY_N];
static vio_dir_state_t sd_dir[SPY_N];
static int             sd_count;

// vio_native_fw_set_level records
static int fl_num[SPY_N];
static int fl_level[SPY_N];
static int fl_count;

// vio_native_get_level
static int stub_get_level_ret;
static int get_level_count;

// vio_native_is_tracked
static bool stub_is_tracked;
static int  last_tracked_query;

// __real_* call counts
static int real_gpio_config_count;
static int real_gpio_set_direction_count;
static int real_gpio_reset_pin_count;
static int real_gpio_set_level_count;
static int real_gpio_get_level_count;
static int real_uart_set_pin_count;
static int stub_real_get_level_ret;

// --- Model API spy stubs --------------------------------------------------------
void vio_native_set_direction(int gpio_num, vio_dir_state_t dir)
{
    if (sd_count < SPY_N) {
        sd_num[sd_count] = gpio_num;
        sd_dir[sd_count] = dir;
    }
    sd_count++;
}

void vio_native_fw_set_level(int gpio_num, int level)
{
    if (fl_count < SPY_N) {
        fl_num[fl_count] = gpio_num;
        fl_level[fl_count] = level;
    }
    fl_count++;
}

int vio_native_get_level(int gpio_num)
{
    (void)gpio_num;
    get_level_count++;
    return stub_get_level_ret;
}

bool vio_native_is_tracked(int gpio_num)
{
    last_tracked_query = gpio_num;
    return stub_is_tracked;
}

// --- Original (__real_*) spy stubs ----------------------------------------------
esp_err_t __real_gpio_config(const gpio_config_t *pGPIOConfig)
{
    (void)pGPIOConfig;
    real_gpio_config_count++;
    return ESP_OK;
}

esp_err_t __real_gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    (void)gpio_num;
    (void)mode;
    real_gpio_set_direction_count++;
    return ESP_OK;
}

esp_err_t __real_gpio_reset_pin(gpio_num_t gpio_num)
{
    (void)gpio_num;
    real_gpio_reset_pin_count++;
    return ESP_OK;
}

esp_err_t __real_gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    (void)gpio_num;
    (void)level;
    real_gpio_set_level_count++;
    return ESP_OK;
}

int __real_gpio_get_level(gpio_num_t gpio_num)
{
    (void)gpio_num;
    real_gpio_get_level_count++;
    return stub_real_get_level_ret;
}

esp_err_t __real_uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num,
                              int rts_io_num, int cts_io_num)
{
    (void)uart_num;
    (void)tx_io_num;
    (void)rx_io_num;
    (void)rts_io_num;
    (void)cts_io_num;
    real_uart_set_pin_count++;
    return ESP_OK;
}

// --- Unity fixtures -------------------------------------------------------------
void setUp(void)
{
    sd_count = 0;
    fl_count = 0;
    for (int i = 0; i < SPY_N; i++) {
        sd_num[i] = 0;
        sd_dir[i] = VIO_DIR_UNCONFIGURED;
        fl_num[i] = 0;
        fl_level[i] = 0;
    }
    stub_get_level_ret = 0;
    get_level_count = 0;
    stub_is_tracked = false;
    last_tracked_query = -1;
    real_gpio_config_count = 0;
    real_gpio_set_direction_count = 0;
    real_gpio_reset_pin_count = 0;
    real_gpio_set_level_count = 0;
    real_gpio_get_level_count = 0;
    real_uart_set_pin_count = 0;
    stub_real_get_level_ret = 0;
}

void tearDown(void) {}

// --- Tests ----------------------------------------------------------------------
void test_dir_from_mode_mapping(void)
{
    TEST_ASSERT_EQUAL_INT(VIO_DIR_OUTPUT,       dir_from_mode(GPIO_MODE_OUTPUT));
    TEST_ASSERT_EQUAL_INT(VIO_DIR_INPUT,        dir_from_mode(GPIO_MODE_INPUT));
    TEST_ASSERT_EQUAL_INT(VIO_DIR_UNCONFIGURED, dir_from_mode(GPIO_MODE_DISABLE));
    // Both input and output bits set -> OUTPUT wins (firmware-driven). Critical.
    TEST_ASSERT_EQUAL_INT(VIO_DIR_OUTPUT,       dir_from_mode(GPIO_MODE_INPUT_OUTPUT));
    TEST_ASSERT_EQUAL_INT(VIO_DIR_OUTPUT,       dir_from_mode(GPIO_MODE_OUTPUT_OD));
    TEST_ASSERT_EQUAL_INT(VIO_DIR_OUTPUT,       dir_from_mode(GPIO_MODE_INPUT_OUTPUT_OD));
}

void test_wrap_gpio_config_single_bit(void)
{
    gpio_config_t cfg = {0};
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pin_bit_mask = (1ULL << 4);

    esp_err_t rc = __wrap_gpio_config(&cfg);

    TEST_ASSERT_EQUAL_INT(ESP_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, sd_count);
    TEST_ASSERT_EQUAL_INT(4, sd_num[0]);
    TEST_ASSERT_EQUAL_INT(VIO_DIR_INPUT, sd_dir[0]);
    TEST_ASSERT_EQUAL_INT(1, real_gpio_config_count);
}

void test_wrap_gpio_config_multi_bit(void)
{
    gpio_config_t cfg = {0};
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pin_bit_mask = (1ULL << 4) | (1ULL << 15);

    __wrap_gpio_config(&cfg);

    TEST_ASSERT_EQUAL_INT(2, sd_count);
    TEST_ASSERT_EQUAL_INT(4, sd_num[0]);
    TEST_ASSERT_EQUAL_INT(VIO_DIR_OUTPUT, sd_dir[0]);
    TEST_ASSERT_EQUAL_INT(15, sd_num[1]);
    TEST_ASSERT_EQUAL_INT(VIO_DIR_OUTPUT, sd_dir[1]);
    TEST_ASSERT_EQUAL_INT(1, real_gpio_config_count);
}

void test_wrap_gpio_config_null_guard(void)
{
    esp_err_t rc = __wrap_gpio_config(NULL);

    TEST_ASSERT_EQUAL_INT(ESP_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, sd_count);
    TEST_ASSERT_EQUAL_INT(1, real_gpio_config_count);
}

void test_wrap_gpio_config_pin_beyond_max_ignored(void)
{
    gpio_config_t cfg = {0};
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pin_bit_mask = (1ULL << 40); // == GPIO_NUM_MAX, outside 0..39 loop range

    __wrap_gpio_config(&cfg);

    TEST_ASSERT_EQUAL_INT(0, sd_count);
    TEST_ASSERT_EQUAL_INT(1, real_gpio_config_count);
}

void test_wrap_gpio_set_level_tracked(void)
{
    stub_is_tracked = true;

    esp_err_t rc = __wrap_gpio_set_level(4, 1);

    TEST_ASSERT_EQUAL_INT(ESP_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, fl_count);
    TEST_ASSERT_EQUAL_INT(4, fl_num[0]);
    TEST_ASSERT_EQUAL_INT(1, fl_level[0]);
    TEST_ASSERT_EQUAL_INT(1, real_gpio_set_level_count);
}

void test_wrap_gpio_set_level_untracked(void)
{
    stub_is_tracked = false;

    esp_err_t rc = __wrap_gpio_set_level(4, 1);

    TEST_ASSERT_EQUAL_INT(ESP_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, fl_count);
    TEST_ASSERT_EQUAL_INT(1, real_gpio_set_level_count);
}

void test_wrap_gpio_get_level_tracked(void)
{
    stub_is_tracked = true;
    stub_get_level_ret = 1;

    int level = __wrap_gpio_get_level(4);

    TEST_ASSERT_EQUAL_INT(1, level);
    TEST_ASSERT_EQUAL_INT(0, real_gpio_get_level_count);
}

void test_wrap_gpio_get_level_untracked(void)
{
    stub_is_tracked = false;
    stub_real_get_level_ret = 0;

    int level = __wrap_gpio_get_level(4);

    TEST_ASSERT_EQUAL_INT(0, level);
    TEST_ASSERT_EQUAL_INT(1, real_gpio_get_level_count);
}

void test_wrap_uart_set_pin_rts(void)
{
    esp_err_t rc = __wrap_uart_set_pin(0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                       4, UART_PIN_NO_CHANGE);

    TEST_ASSERT_EQUAL_INT(ESP_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, sd_count);
    TEST_ASSERT_EQUAL_INT(4, sd_num[0]);
    TEST_ASSERT_EQUAL_INT(VIO_DIR_OUTPUT, sd_dir[0]);
    TEST_ASSERT_EQUAL_INT(1, fl_count);
    TEST_ASSERT_EQUAL_INT(4, fl_num[0]);
    TEST_ASSERT_EQUAL_INT(1, fl_level[0]);
    TEST_ASSERT_EQUAL_INT(1, real_uart_set_pin_count);
}

void test_wrap_uart_set_pin_no_change(void)
{
    esp_err_t rc = __wrap_uart_set_pin(0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    TEST_ASSERT_EQUAL_INT(ESP_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, sd_count);
    TEST_ASSERT_EQUAL_INT(0, fl_count);
    TEST_ASSERT_EQUAL_INT(1, real_uart_set_pin_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dir_from_mode_mapping);
    RUN_TEST(test_wrap_gpio_config_single_bit);
    RUN_TEST(test_wrap_gpio_config_multi_bit);
    RUN_TEST(test_wrap_gpio_config_null_guard);
    RUN_TEST(test_wrap_gpio_config_pin_beyond_max_ignored);
    RUN_TEST(test_wrap_gpio_set_level_tracked);
    RUN_TEST(test_wrap_gpio_set_level_untracked);
    RUN_TEST(test_wrap_gpio_get_level_tracked);
    RUN_TEST(test_wrap_gpio_get_level_untracked);
    RUN_TEST(test_wrap_uart_set_pin_rts);
    RUN_TEST(test_wrap_uart_set_pin_no_change);
    return UNITY_END();
}
