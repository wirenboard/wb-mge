#include "unity.h"

#include "slot_relay.h"
#include "driver/gpio.h"

#include <stdbool.h>

/* port_manager_ports_frozen() is the only thing slot_relay.c needs from port_manager */
static bool mock_ports_frozen = false;

bool port_manager_ports_frozen(void)
{
    return mock_ports_frozen;
}

void setUp(void)
{
    mock_ports_frozen = false;
    slot_relay_release();
    mock_gpio_reset();
}

void tearDown(void)
{
}

void test_set_refused_while_released(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, slot_relay_set(true));
    TEST_ASSERT_EQUAL_INT(0, mock_gpio_set_level_data.called);

    bool on = true;
    TEST_ASSERT_EQUAL(ESP_OK, slot_relay_get(&on));
    TEST_ASSERT_FALSE(on);
}

void test_acquire_drives_low_before_output(void)
{
    slot_relay_acquire();

    TEST_ASSERT_EQUAL(GPIO_NUM_13, mock_gpio_set_direction_data.gpio_num);
    TEST_ASSERT_EQUAL(GPIO_MODE_OUTPUT, mock_gpio_set_direction_data.mode);
    TEST_ASSERT_EQUAL_UINT32(0, mock_gpio_set_level_data.level);
    TEST_ASSERT_LESS_THAN_UINT(mock_gpio_set_direction_data.call_seq, mock_gpio_set_level_data.call_seq);
}

void test_set_and_get_after_acquire(void)
{
    bool on = false;

    slot_relay_acquire();

    TEST_ASSERT_EQUAL(ESP_OK, slot_relay_set(true));
    TEST_ASSERT_EQUAL(GPIO_NUM_13, mock_gpio_set_level_data.gpio_num);
    TEST_ASSERT_EQUAL_UINT32(1, mock_gpio_set_level_data.level);
    TEST_ASSERT_EQUAL(ESP_OK, slot_relay_get(&on));
    TEST_ASSERT_TRUE(on);

    TEST_ASSERT_EQUAL(ESP_OK, slot_relay_set(false));
    TEST_ASSERT_EQUAL_UINT32(0, mock_gpio_set_level_data.level);
    TEST_ASSERT_EQUAL(ESP_OK, slot_relay_get(&on));
    TEST_ASSERT_FALSE(on);
}

void test_set_refused_while_frozen(void)
{
    slot_relay_acquire();
    int level_calls = mock_gpio_set_level_data.called;

    mock_ports_frozen = true;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, slot_relay_set(true));
    TEST_ASSERT_EQUAL_INT(level_calls, mock_gpio_set_level_data.called);
}

void test_release_forgets_state(void)
{
    bool on = true;

    slot_relay_acquire();
    TEST_ASSERT_EQUAL(ESP_OK, slot_relay_set(true));

    slot_relay_release();
    TEST_ASSERT_EQUAL(ESP_OK, slot_relay_get(&on));
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, slot_relay_set(true));
}

void test_acquire_clears_state(void)
{
    bool on = true;

    slot_relay_acquire();
    TEST_ASSERT_EQUAL(ESP_OK, slot_relay_set(true));

    slot_relay_acquire();
    TEST_ASSERT_EQUAL_UINT32(0, mock_gpio_set_level_data.level);
    TEST_ASSERT_EQUAL(ESP_OK, slot_relay_get(&on));
    TEST_ASSERT_FALSE(on);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_set_refused_while_released);
    RUN_TEST(test_acquire_drives_low_before_output);
    RUN_TEST(test_set_and_get_after_acquire);
    RUN_TEST(test_set_refused_while_frozen);
    RUN_TEST(test_release_forgets_state);
    RUN_TEST(test_acquire_clears_state);

    return UNITY_END();
}
