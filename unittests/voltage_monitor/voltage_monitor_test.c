#include "unity.h"
#include "console_log.h"

#include <stdbool.h>

// Access non-static functions exported by VM_STATIC macro in unittest env
float exp_filter(float cur_value, float new_value);
bool check_sys_voltage_bounds(float sys_voltage, bool ok_state);

/* Forward declaration for the prot_engine reset function exposed in __unittest_env__ */
void voltage_monitor_reset_prot_engine(void);

void setUp(void)
{
    voltage_monitor_reset_prot_engine(); /* reset static prot_engine state between tests */
}

void tearDown(void)
{
}

// ---------------------------------------------------------------------------
// Tests for check_sys_voltage_bounds
// ---------------------------------------------------------------------------

// ok_state=true, voltage below min FAIL threshold → should return false
void test_check_sys_voltage_bounds_ok_below_min_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test check_sys_voltage_bounds - ok=true, voltage < MIN_FAIL");
    LOG_MESSAGE();

    bool result = check_sys_voltage_bounds(7.4f, true);
    TEST_ASSERT_FALSE_MESSAGE(result, "Voltage 7.4V should trigger FAIL when ok_state=true (MIN_FAIL=7.5)");
}

// ok_state=true, voltage still above min FAIL threshold → should return true
void test_check_sys_voltage_bounds_ok_above_min_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test check_sys_voltage_bounds - ok=true, voltage > MIN_FAIL");
    LOG_MESSAGE();

    bool result = check_sys_voltage_bounds(7.6f, true);
    TEST_ASSERT_TRUE_MESSAGE(result, "Voltage 7.6V should remain OK when ok_state=true (MIN_FAIL=7.5)");
}

// ok_state=true, voltage above max FAIL threshold → should return false
void test_check_sys_voltage_bounds_ok_above_max_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test check_sys_voltage_bounds - ok=true, voltage > MAX_FAIL");
    LOG_MESSAGE();

    bool result = check_sys_voltage_bounds(29.1f, true);
    TEST_ASSERT_FALSE_MESSAGE(result, "Voltage 29.1V should trigger FAIL when ok_state=true (MAX_FAIL=29.0)");
}

// ok_state=true, voltage below max FAIL threshold → should return true
void test_check_sys_voltage_bounds_ok_below_max_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test check_sys_voltage_bounds - ok=true, voltage < MAX_FAIL");
    LOG_MESSAGE();

    bool result = check_sys_voltage_bounds(28.9f, true);
    TEST_ASSERT_TRUE_MESSAGE(result, "Voltage 28.9V should remain OK when ok_state=true (MAX_FAIL=29.0)");
}

// ok_state=false, voltage below min OK threshold → should return false (not recovered)
void test_check_sys_voltage_bounds_fail_below_min_ok(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test check_sys_voltage_bounds - ok=false, voltage < MIN_OK");
    LOG_MESSAGE();

    bool result = check_sys_voltage_bounds(7.9f, false);
    TEST_ASSERT_FALSE_MESSAGE(result, "Voltage 7.9V should not recover when ok_state=false (MIN_OK=8.0)");
}

// ok_state=false, voltage at min OK threshold → should return true (recovered)
void test_check_sys_voltage_bounds_fail_at_min_ok(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test check_sys_voltage_bounds - ok=false, voltage == MIN_OK");
    LOG_MESSAGE();

    bool result = check_sys_voltage_bounds(8.0f, false);
    TEST_ASSERT_TRUE_MESSAGE(result, "Voltage 8.0V should recover when ok_state=false (MIN_OK=8.0)");
}

// ok_state=false, voltage above max OK threshold → should return false (not recovered)
void test_check_sys_voltage_bounds_fail_above_max_ok(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test check_sys_voltage_bounds - ok=false, voltage > MAX_OK");
    LOG_MESSAGE();

    bool result = check_sys_voltage_bounds(28.1f, false);
    TEST_ASSERT_FALSE_MESSAGE(result, "Voltage 28.1V should not recover when ok_state=false (MAX_OK=28.0)");
}

// ok_state=false, voltage at max OK threshold → should return true (recovered)
void test_check_sys_voltage_bounds_fail_at_max_ok(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test check_sys_voltage_bounds - ok=false, voltage == MAX_OK");
    LOG_MESSAGE();

    bool result = check_sys_voltage_bounds(28.0f, false);
    TEST_ASSERT_TRUE_MESSAGE(result, "Voltage 28.0V should recover when ok_state=false (MAX_OK=28.0)");
}

// Boundary: ok_state=true, voltage exactly at MIN_FAIL (7.5) → remains OK
// because condition is strictly: voltage < MIN_FAIL (not <=)
void test_check_sys_voltage_bounds_ok_exactly_at_min_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test check_sys_voltage_bounds - ok=true, voltage == MIN_FAIL boundary");
    LOG_MESSAGE();

    // Implementation uses strict < so exactly 7.5 does NOT trigger FAIL
    bool result = check_sys_voltage_bounds(7.5f, true);
    TEST_ASSERT_TRUE_MESSAGE(result, "Voltage exactly 7.5V should remain OK when ok_state=true (condition: voltage < MIN_FAIL=7.5)");
}

// Boundary: ok_state=false, voltage exactly at MIN_OK → should return true
void test_check_sys_voltage_bounds_fail_exactly_at_min_ok(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test check_sys_voltage_bounds - ok=false, voltage == MIN_OK boundary");
    LOG_MESSAGE();

    bool result = check_sys_voltage_bounds(8.0f, false);
    TEST_ASSERT_TRUE_MESSAGE(result, "Voltage exactly 8.0V should recover when ok_state=false (MIN_OK=8.0)");
}

// ok_state=true, voltage exactly at MAX_FAIL (29.0V) — condition is strict >, so 29.0 does NOT trigger FAIL
void test_check_sys_voltage_bounds_ok_exactly_at_max_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test check_sys_voltage_bounds - ok=true, voltage == MAX_FAIL (29.0V boundary)");
    LOG_MESSAGE();

    /* Implementation: ok_state && voltage > MAX_FAIL(29.0) → false; 29.0 > 29.0 is false → stays OK */
    bool result = check_sys_voltage_bounds(29.0f, true);
    TEST_ASSERT_TRUE_MESSAGE(result, "Voltage exactly 29.0V should remain OK (condition > 29.0, not >=)");
}

// ---------------------------------------------------------------------------
// Tests for exp_filter
// ---------------------------------------------------------------------------

// When cur != new, result should be between cur and new (alpha > 0)
void test_exp_filter_moves_toward_new_value(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test exp_filter - result is between cur and new when they differ");
    LOG_MESSAGE();

    float result = exp_filter(10.0f, 20.0f);
    TEST_ASSERT_GREATER_THAN_MESSAGE(10.0f, result, "exp_filter result should be greater than cur_value");
    TEST_ASSERT_LESS_THAN_MESSAGE(20.0f, result, "exp_filter result should be less than new_value");
}

// When cur == new, result should equal the same value (no change)
void test_exp_filter_no_change_when_equal(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test exp_filter - no change when cur_value equals new_value");
    LOG_MESSAGE();

    float result = exp_filter(10.0f, 10.0f);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(10.0f, result, "exp_filter should return the same value when cur equals new");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_check_sys_voltage_bounds_ok_below_min_fail);
    RUN_TEST(test_check_sys_voltage_bounds_ok_above_min_fail);
    RUN_TEST(test_check_sys_voltage_bounds_ok_above_max_fail);
    RUN_TEST(test_check_sys_voltage_bounds_ok_below_max_fail);
    RUN_TEST(test_check_sys_voltage_bounds_fail_below_min_ok);
    RUN_TEST(test_check_sys_voltage_bounds_fail_at_min_ok);
    RUN_TEST(test_check_sys_voltage_bounds_fail_above_max_ok);
    RUN_TEST(test_check_sys_voltage_bounds_fail_at_max_ok);
    RUN_TEST(test_check_sys_voltage_bounds_ok_exactly_at_min_fail);
    RUN_TEST(test_check_sys_voltage_bounds_ok_exactly_at_max_fail);
    RUN_TEST(test_check_sys_voltage_bounds_fail_exactly_at_min_ok);

    RUN_TEST(test_exp_filter_moves_toward_new_value);
    RUN_TEST(test_exp_filter_no_change_when_equal);

    return UNITY_END();
}
