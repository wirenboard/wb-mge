#include "array_size.h"
#include "debug_log.h"
#include "esp_log.h"
#include "gpio_expander.h"


#define PORT_EXPANDER_TEST_PINS_COUNT       3
#define PORT_EXPANDER_SHORTED_PIN_1         IO_EXPANDER_PIN_NUM_13  // P15 = IO1.5 = IO_EXPANDER_PIN_NUM_13
#define PORT_EXPANDER_SHORTED_PIN_2         IO_EXPANDER_PIN_NUM_14  // P16 = IO1.6 = IO_EXPANDER_PIN_NUM_14
#define PORT_EXPANDER_GROUNDED_PIN          IO_EXPANDER_PIN_NUM_15  // P17 = IO1.7 = IO_EXPANDER_PIN_NUM_15


esp_io_expander_pin_num_t port_expander_pins[PORT_EXPANDER_TEST_PINS_COUNT] = {
    PORT_EXPANDER_SHORTED_PIN_1,
    PORT_EXPANDER_SHORTED_PIN_2,
    PORT_EXPANDER_GROUNDED_PIN
};

typedef struct {
    esp_io_expander_dir_t direction;
    uint8_t out_level;
    uint8_t in_level;
} port_expander_test_pin_t;

typedef struct {
    port_expander_test_pin_t pins[PORT_EXPANDER_TEST_PINS_COUNT];
} port_expander_test_t;

// Test data array for GPIO port expander jumper tests (test = array row)
// When executing each test:
// 0) All GPIOs are reset to default mode (input) to avoid shorts between logic 1 and 0 between tests
// 1) GPIO mode is configured for all pins (input or output)
// 2) On GPIOs configured as outputs, specified level is set (out_level field: logic 0 or 1)
// 3) From GPIOs configured as inputs, level is read and compared with test specification (in_level field: logic 0 or 1)
static port_expander_test_t port_expander_tests[] = {
//           PORT_EXPANDER_SHORTED_PIN_1             PORT_EXPANDER_SHORTED_PIN_2            PORT_EXPANDER_GROUNDED_PIN
//            direction     |  out  | input           direction     |  out  | input           direction     |  out  | input
    {{  {IO_EXPANDER_INPUT,     0,      0   },  {IO_EXPANDER_OUTPUT,    0,      0   },  {IO_EXPANDER_INPUT,     0,      0   }  }},
    {{  {IO_EXPANDER_INPUT,     0,      1   },  {IO_EXPANDER_OUTPUT,    1,      0   },  {IO_EXPANDER_INPUT,     0,      0   }  }},
    {{  {IO_EXPANDER_INPUT,     0,      0   },  {IO_EXPANDER_OUTPUT,    0,      0   },  {IO_EXPANDER_INPUT,     0,      0   }  }},
    {{  {IO_EXPANDER_OUTPUT,    1,      0   },  {IO_EXPANDER_INPUT,     0,      1   },  {IO_EXPANDER_INPUT,     0,      0   }  }},
    {{  {IO_EXPANDER_OUTPUT,    0,      0   },  {IO_EXPANDER_INPUT,     0,      0   },  {IO_EXPANDER_INPUT,     0,      0   }  }},
};

#if DEBUG_LOG_ENABLE
    static const char* TAG = "port_expander_tests";
#endif


// Restore default pins directions (input)
static void reset_gpio_directions(void)
{
    for (unsigned pin = 0; pin < PORT_EXPANDER_TEST_PINS_COUNT; pin++) {
        gpio_expander_set_dir(port_expander_pins[pin], IO_EXPANDER_INPUT);
    }
}


// Set pins directions and outputs levels
static void set_gpio_directions_and_levels(port_expander_test_t* test)
{
    for (unsigned pin = 0; pin < PORT_EXPANDER_TEST_PINS_COUNT; pin++) {
        esp_io_expander_dir_t dir = test->pins[pin].direction;
        esp_io_expander_pin_num_t pin_num = port_expander_pins[pin];
        if (dir == IO_EXPANDER_OUTPUT) {
            gpio_expander_set_out_dir_and_level(pin_num, test->pins[pin].out_level);
        } else {
            gpio_expander_set_dir(pin_num, dir);
        }
    }
}


// Check pins input levels
static bool check_gpio_input_levels(port_expander_test_t* test)
{
    bool ok = true;

    for (unsigned pin = 0; pin < PORT_EXPANDER_TEST_PINS_COUNT; pin++) {
        esp_io_expander_dir_t dir = test->pins[pin].direction;
        if (dir != IO_EXPANDER_INPUT) {
            continue;
        }
        esp_io_expander_pin_num_t pin_num = port_expander_pins[pin];
        uint32_t pin_levels = 0;
        esp_err_t ret = gpio_expander_get_level(pin_num, &pin_levels);
        if (ret == ESP_OK) {
            bool test_ok;
            if (test->pins[pin].in_level) {
                test_ok = pin_levels;
            } else {
                test_ok = !pin_levels;
            }
            #if DEBUG_LOG_ENABLE
                if (!test_ok) {
                    unsigned index = (unsigned)((void*)test - (void*)port_expander_tests) / sizeof(port_expander_test_t);
                    ESP_LOGE(TAG, "Test #%u, pin #%u: incorrect input state", index, pin);
                }
            #endif
            ok = test_ok && ok;
        } else {
            #if DEBUG_LOG_ENABLE
                unsigned index = (unsigned)((void*)test - (void*)port_expander_tests) / sizeof(port_expander_test_t);
                ESP_LOGE(TAG, "Test #%u, pin #%u: failed to read GPIO state", index, pin);
            #endif
            ok = false;
        }
    }

    return ok;
}


esp_err_t port_expander_run_tests(void)
{
    bool ok = true;

    // Run tests
    for (unsigned index = 0; index < ARRAY_SIZE(port_expander_tests); index++) {
        reset_gpio_directions();
        set_gpio_directions_and_levels(&port_expander_tests[index]);
        bool test_ok = check_gpio_input_levels(&port_expander_tests[index]);
        ok = test_ok && ok;
        // Don't break if test failed to save all tests sequence
    }

    reset_gpio_directions();

    if (ok) {
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}
