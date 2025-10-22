#include "esp_io_expander.h"
#include "array_size.h"


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

static port_expander_test_t port_expander_tests[] = {
//           PORT_EXPANDER_SHORTED_PIN_1             PORT_EXPANDER_SHORTED_PIN_2            PORT_EXPANDER_GROUNDED_PIN
//            direction     |  out  | input           direction     |  out  | input           direction     |  out  | input
    {{  {IO_EXPANDER_INPUT,     0,      0   },  {IO_EXPANDER_OUTPUT,    0,      0   },  {IO_EXPANDER_INPUT,     0,      0   }  }},
    {{  {IO_EXPANDER_INPUT,     0,      1   },  {IO_EXPANDER_OUTPUT,    1,      0   },  {IO_EXPANDER_INPUT,     0,      0   }  }},
    {{  {IO_EXPANDER_INPUT,     0,      0   },  {IO_EXPANDER_OUTPUT,    0,      0   },  {IO_EXPANDER_INPUT,     0,      0   }  }},
    {{  {IO_EXPANDER_OUTPUT,    1,      0   },  {IO_EXPANDER_INPUT,     0,      1   },  {IO_EXPANDER_INPUT,     0,      0   }  }},
    {{  {IO_EXPANDER_OUTPUT,    0,      0   },  {IO_EXPANDER_INPUT,     0,      0   },  {IO_EXPANDER_INPUT,     0,      0   }  }},

};

esp_err_t port_expander_run_tests(esp_io_expander_handle_t io_expander_handle)
{
    if (io_expander_handle == NULL) {
        return ESP_FAIL;
    }

    bool ok = true;

    // Run tests
    for (unsigned index = 0; index < ARRAY_SIZE(port_expander_tests); index++) {
        // Set pins directions and outputs levels
        for (unsigned pin = 0; pin < PORT_EXPANDER_TEST_PINS_COUNT; pin++) {
            esp_io_expander_dir_t dir = port_expander_tests[index].pins[pin].direction;
            esp_io_expander_pin_num_t pin_num = port_expander_pins[pin];
            esp_io_expander_set_dir(io_expander_handle, pin_num, dir);
            if (dir == IO_EXPANDER_OUTPUT) {
                esp_io_expander_set_level(io_expander_handle, pin_num, port_expander_tests[index].pins[pin].out_level);
            }
        }
        // Check pins input levels
        for (unsigned pin = 0; pin < PORT_EXPANDER_TEST_PINS_COUNT; pin++) {
            esp_io_expander_dir_t dir = port_expander_tests[index].pins[pin].direction;
            if (dir != IO_EXPANDER_INPUT) {
                continue;
            }
            esp_io_expander_pin_num_t pin_num = port_expander_pins[pin];
            uint32_t pin_levels = 0;
            esp_err_t ret = esp_io_expander_get_level(io_expander_handle, pin_num, &pin_levels);
            if (ret == ESP_OK) {
                if (port_expander_tests[index].pins[pin].in_level) {
                    ok = pin_levels && ok;
                } else {
                    ok = !pin_levels && ok;
                }
            } else {
                ok = false;
            }
        }
    }

    // Restore default pins directions (input)
    for (unsigned pin = 0; pin < PORT_EXPANDER_TEST_PINS_COUNT; pin++) {
        esp_io_expander_set_dir(io_expander_handle, port_expander_pins[pin], IO_EXPANDER_INPUT);
    }

    if (ok) {
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}
