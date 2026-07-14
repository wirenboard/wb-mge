#include "unity.h"
#include "console_log.h"

#include "config.h"
#include "sys_info.h"
#include "esp_mac.h"
#include "esp_efuse.h"
#include "esp_psram.h"

#include <string.h>

// PSRAM detection in sys_info_init() sits behind #if CONFIG_SPIRAM. This binary is
// built WITHOUT CONFIG_SPIRAM, mirroring the serial WB-MGE (mge_v3) board, where the
// esp_psram component is not linked in and the detection branch must be compiled out.
//
// The esp_psram mock is still linked here (AUX_SRC is shared across the suite), which
// is exactly what makes these tests meaningful: they force the mock to report a large,
// initialized PSRAM. If the CONFIG_SPIRAM branch ever leaked into a no-PSRAM build,
// sys_info would pick those mocked values up and the assertions below would fail.
//
// The PSRAM-enabled (mgu_v1) counterpart lives in sys_info_test.c.

#if CONFIG_SPIRAM
#error "sys_info_nopsram_test must be built without CONFIG_SPIRAM"
#endif

#define SERIAL_FROM_MAC                                 4328719365UL // MAC 00:01:02:03:04:05 converted to uint64

void setUp(void)
{
    mock_esp_mac_reset();
    mock_esp_efuse_reset();
    mock_esp_psram_reset();
    memset(&sys_info, 0, sizeof(sys_info));
}

void tearDown(void)
{

}

// Test that PSRAM is reported as absent even when the underlying driver would report it
void test_sys_info_init_psram_absent_despite_mock(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - no-PSRAM build ignores an initialized PSRAM");
    LOG_MESSAGE();

    // Mock reports an initialized 4 MB PSRAM; the no-PSRAM build must not consult it at all
    mock_esp_psram_is_initialized_return = true;
    mock_esp_psram_get_size_return = 4 * 1024 * 1024; // 4 MB

    esp_err_t result = sys_info_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "sys_info_init should return ESP_OK on a no-PSRAM board");

    // sys_info is zeroed in setUp, so assert that sys_info_init actually ran and populated
    // the struct. Without this the PSRAM assertions below would hold vacuously.
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        SERIAL_FROM_MAC, sys_info.device_serial_num, "sys_info_init must still populate the serial number"
    );

    TEST_ASSERT_FALSE_MESSAGE(
        sys_info.psram_available, "psram_available must be false on a no-PSRAM board even if the driver reports PSRAM"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, sys_info.psram_size_kb, "psram_size_kb must be 0 on a no-PSRAM board even if the driver reports a size"
    );
}

// Test that PSRAM stays absent when the driver also reports it as uninitialized
void test_sys_info_init_psram_absent_with_idle_mock(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - no-PSRAM build reports no PSRAM");
    LOG_MESSAGE();

    mock_esp_psram_is_initialized_return = false;
    mock_esp_psram_get_size_return = 0;

    esp_err_t result = sys_info_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "sys_info_init should return ESP_OK on a no-PSRAM board");
    // Anchor: without this, the zero-assertions below would also pass against the
    // struct that setUp() zeroes, i.e. even if sys_info_init() bailed out early.
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        SERIAL_FROM_MAC, sys_info.device_serial_num, "sys_info_init must still populate the serial number");
    TEST_ASSERT_FALSE_MESSAGE(sys_info.psram_available, "psram_available should be false on a no-PSRAM board");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, sys_info.psram_size_kb, "psram_size_kb should be 0 on a no-PSRAM board");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_sys_info_init_psram_absent_despite_mock);
    RUN_TEST(test_sys_info_init_psram_absent_with_idle_mock);

    return UNITY_END();
}
