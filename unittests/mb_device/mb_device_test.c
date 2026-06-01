#include "unity.h"
#include "console_log.h"

#include "mb_device.h"
#include "modbus_helpers.h"
#include "sys_info.h"

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/task.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---- The sys_info global (declared extern in sys_info.h) ------------------ */

sys_info_t sys_info;

/* ---- Mock state exposed by the local mocks ------------------------------- */

/* mocks/voltage_monitor.c */
void mock_voltage_set(float volts);
void mock_voltage_reset(void);

/* mocks/setting_items.c */
void mock_setting_items_set_timeout(int timeout_s);
void mock_setting_items_reset(void);

/* mocks/cache_multimaster.c */
void mock_cache_stats_set(uint32_t packets_processed, uint32_t last_packet_age_s,
                          uint32_t map_age_s, uint16_t devices_on_bus);
void mock_cache_stats_reset(void);

/* mocks/esp_heap_caps.c */
void mock_heap_set_sizes(size_t total, size_t free);
void mock_heap_reset(void);

/* mocks/esp_system.c */
void mock_set_reset_reason(esp_reset_reason_t reason);
void mock_reset_reason_reset(void);

/* mocks/freertos/task.c */
void mock_set_stack_high_water_mark(UBaseType_t words);
void mock_freertos_task_reset(void);

/* unittests/mocks/esp_timer.c */
void mock_esp_timer_reset(void);

/* ---- Constants mirrored from mb_device.c (Unit ID, FCs, exceptions) ------ */

#define DEV_UNIT_ID   0xFFu

#define FC_READ_HOLDING  0x03u
#define FC_READ_INPUT    0x04u
#define FC_READ_COILS    0x01u

#define EX_ILLEGAL_FUNCTION  0x01u
#define EX_ILLEGAL_ADDRESS   0x02u

/* MBAP header is 8 bytes: tid(2) protocol_id(2) length(2) unit_id(1) fc(1). */
#define MBAP_LEN  8u

/* ---- Helpers ------------------------------------------------------------- */

/* Read a 16-bit register value (big-endian) from the response payload at
 * register index i (0-based within the requested range). */
static uint16_t resp_reg(const uint8_t *buf, uint16_t i)
{
    const uint8_t *payload = buf + MBAP_LEN; /* payload[0] = byte_count */
    uint16_t hi = payload[1 + i * 2];
    uint16_t lo = payload[1 + i * 2 + 1];
    return (uint16_t)((hi << 8) | lo);
}

/* Read the byte_count field from the payload. */
static uint8_t resp_byte_count(const uint8_t *buf)
{
    return buf[MBAP_LEN];
}

/* Decode a packed string field (big-endian, hi byte = first char) back into a
 * NUL-terminated C string. count = number of registers in the field. */
static void decode_string(const uint8_t *buf, uint16_t count, char *out)
{
    for (uint16_t i = 0; i < count; i++) {
        uint16_t r = resp_reg(buf, i);
        out[i * 2]     = (char)(r >> 8);
        out[i * 2 + 1] = (char)(r & 0xFFu);
    }
    out[count * 2] = '\0';
}

/* ---- setUp / tearDown ---------------------------------------------------- */

void setUp(void)
{
    mock_voltage_reset();
    mock_setting_items_reset();
    mock_cache_stats_reset();
    mock_heap_reset();
    mock_reset_reason_reset();
    mock_freertos_task_reset();
    mock_esp_timer_reset();

    /* Deterministic device identity for every test. */
    memset(&sys_info, 0, sizeof(sys_info));
    strcpy(sys_info.device_name,       "TEST-DEV");
    strcpy(sys_info.firmware_ver,      "1.2.3+wb5");
    strcpy(sys_info.firmware_git_info, "abc1234_main");
    strcpy(sys_info.device_signature,  "sigXYZ");
    sys_info.device_serial_num = 0x0000112233445566ULL;
}

void tearDown(void)
{
}

/* ---- MBDEV-U-001: FC04 uptime (104,105) ---------------------------------- */

/* Verify uptime registers (32-bit, MSW-first) and the full MBAP header for a
 * 2-register FC04 read. esp_timer = 5_000_000 us => 5 s. */
void test_uptime_and_mbap_header(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-001: FC04 uptime + MBAP header");
    LOG_MESSAGE();

    mock_esp_timer_get_time_value = 5000000ULL; /* 5 seconds */

    uint8_t buf[260];
    uint8_t exc = 0xAA;
    uint16_t tid_net = 0x3412; /* arbitrary network-order tid; echoed verbatim */

    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, tid_net,
                                             104u, 2u, 0u, buf, &exc);

    /* total = MBAP(8) + byte_count_field(1) + data(4) = 13 */
    TEST_ASSERT_EQUAL_UINT(13u, n);

    const mb_tcp_header_t *hdr = (const mb_tcp_header_t *)buf;
    TEST_ASSERT_EQUAL_HEX16(tid_net, hdr->transaction_id); /* echoed verbatim */
    TEST_ASSERT_EQUAL_HEX16(0x0000, hdr->protocol_id);
    TEST_ASSERT_EQUAL_HEX8(DEV_UNIT_ID, hdr->unit_id);
    TEST_ASSERT_EQUAL_HEX8(FC_READ_INPUT, hdr->function);
    /* length = unit(1)+fc(1)+bc(1)+data(4) = 7, stored network order */
    TEST_ASSERT_EQUAL_HEX16(modbus_swap16(7u), hdr->length);

    TEST_ASSERT_EQUAL_HEX8(4u, resp_byte_count(buf));
    TEST_ASSERT_EQUAL_HEX16(0x0000, resp_reg(buf, 0)); /* reg 104 high word */
    TEST_ASSERT_EQUAL_HEX16(0x0005, resp_reg(buf, 1)); /* reg 105 low word  */
}

/* ---- MBDEV-U-002: FC04 supply voltage (121) ------------------------------ */

/* voltage = 12.0 V => 12000 mV. */
void test_supply_voltage(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-002: FC04 supply voltage");
    LOG_MESSAGE();

    mock_voltage_set(12.0f);

    uint8_t buf[260];
    uint8_t exc = 0xAA;
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             121u, 1u, 0u, buf, &exc);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 2u, n);
    TEST_ASSERT_EQUAL_UINT16(12000u, resp_reg(buf, 0));
}

/* ---- MBDEV-U-003: FC04 model string (200, 4 regs) ------------------------ */

/* device_name="TEST-DEV" packs to 0x5445 0x5354 0x2D44 0x4556. */
void test_model_string(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-003: FC04 model string");
    LOG_MESSAGE();

    uint8_t buf[260];
    uint8_t exc = 0xAA;
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             200u, 5u, 0u, buf, &exc);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 10u, n);
    TEST_ASSERT_EQUAL_HEX16(0x5445, resp_reg(buf, 0)); /* 'T''E' */
    TEST_ASSERT_EQUAL_HEX16(0x5354, resp_reg(buf, 1)); /* 'S''T' */
    TEST_ASSERT_EQUAL_HEX16(0x2D44, resp_reg(buf, 2)); /* '-''D' */
    TEST_ASSERT_EQUAL_HEX16(0x4556, resp_reg(buf, 3)); /* 'E''V' */
    TEST_ASSERT_EQUAL_HEX16(0x0000, resp_reg(buf, 4)); /* trailing zero-pad */
}

/* ---- MBDEV-U-004: FC04 firmware version string (250) --------------------- */

/* firmware_ver="1.2.3+wb5" decodes back from the packed registers. */
void test_fw_version_string(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-004: FC04 firmware version string");
    LOG_MESSAGE();

    const uint16_t count = 6u; /* 12 chars covers "1.2.3+wb5" (9 chars) + pad */
    uint8_t buf[260];
    uint8_t exc = 0xAA;
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             250u, count, 0u, buf, &exc);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + (size_t)count * 2u, n);

    char decoded[64] = {0};
    decode_string(buf, count, decoded);
    TEST_ASSERT_EQUAL_STRING("1.2.3+wb5", decoded);
}

/* ---- MBDEV-U-005: FC04 serial extension + serial number ------------------ */

/* serial = 0x0000112233445566. ext (266..269) MSW-first; serial (270..271) is
 * the low u32 MSW-first. */
void test_serial_registers(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-005: FC04 serial extension + serial number");
    LOG_MESSAGE();

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    /* serial extension: 266..269 (u64, MSW-first) */
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             266u, 4u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 8u, n);
    TEST_ASSERT_EQUAL_HEX16(0x0000, resp_reg(buf, 0));
    TEST_ASSERT_EQUAL_HEX16(0x1122, resp_reg(buf, 1));
    TEST_ASSERT_EQUAL_HEX16(0x3344, resp_reg(buf, 2));
    TEST_ASSERT_EQUAL_HEX16(0x5566, resp_reg(buf, 3));

    /* serial number: 270..271 (low u32, MSW-first) */
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      270u, 2u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 4u, n);
    TEST_ASSERT_EQUAL_HEX16(0x3344, resp_reg(buf, 0));
    TEST_ASSERT_EQUAL_HEX16(0x5566, resp_reg(buf, 1));
}

/* ---- MBDEV-U-006: FC04 firmware numeric version -------------------------- */

/* "1.2.3+wb5": MAJOR=1, MINOR=2, PATCH=3, SUFFIX=5, version=0x01020385.
 * LE words (324,325)=0x0385,0x0102; BE words (326,327)=0x0102,0x0385. */
void test_fw_numeric_plus_suffix(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-006: FC04 firmware numeric (+wb5)");
    LOG_MESSAGE();

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    /* 320..323: major, minor, patch, suffix */
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             320u, 4u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 8u, n);
    TEST_ASSERT_EQUAL_UINT16(1u, resp_reg(buf, 0)); /* MAJOR */
    TEST_ASSERT_EQUAL_UINT16(2u, resp_reg(buf, 1)); /* MINOR */
    TEST_ASSERT_EQUAL_UINT16(3u, resp_reg(buf, 2)); /* PATCH */
    TEST_ASSERT_EQUAL_UINT16(5u, resp_reg(buf, 3)); /* SUFFIX (raw +wb5) */

    /* 324..327: LE lo, LE hi, BE hi, BE lo */
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      324u, 4u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 8u, n);
    TEST_ASSERT_EQUAL_HEX16(0x0385, resp_reg(buf, 0)); /* LE low word  */
    TEST_ASSERT_EQUAL_HEX16(0x0102, resp_reg(buf, 1)); /* LE high word */
    TEST_ASSERT_EQUAL_HEX16(0x0102, resp_reg(buf, 2)); /* BE high word */
    TEST_ASSERT_EQUAL_HEX16(0x0385, resp_reg(buf, 3)); /* BE low word  */
}

/* ---- MBDEV-U-007: FC04 firmware numeric with "-rc2" suffix --------------- */

/* "1.2.3-rc2": suffix=-2 -> SUFFIX reg = 0xFFFE; enc = -1-(-2) = 1 ->
 * version low byte = 0x01, so LE low word = 0x0301 (patch 3 in high byte). */
void test_fw_numeric_rc_suffix(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-007: FC04 firmware numeric (-rc2)");
    LOG_MESSAGE();

    strcpy(sys_info.firmware_ver, "1.2.3-rc2");

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             323u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 2u, n);
    TEST_ASSERT_EQUAL_HEX16(0xFFFE, resp_reg(buf, 0)); /* (uint16_t)(int16_t)-2 */

    /* LE low word should hold patch(0x03) in high byte and enc(0x01) in low. */
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      324u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 2u, n);
    TEST_ASSERT_EQUAL_HEX16(0x0301, resp_reg(buf, 0));
}

/* ---- MBDEV-U-008: FC04 statistics block (336..342) ----------------------- */

/* packets=120, last_age=7, map_age=60, devices=3, cache timeout=42.
 * poll ppm = 120*60/60 = 120. */
void test_stats_block(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-008: FC04 statistics block");
    LOG_MESSAGE();

    mock_cache_stats_set(120u, 7u, 60u, 3u);
    mock_setting_items_set_timeout(42);

    uint8_t buf[260];
    uint8_t exc = 0xAA;
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             336u, 7u, 0u, buf, &exc);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 14u, n);
    TEST_ASSERT_EQUAL_UINT16(42u,  resp_reg(buf, 0)); /* 336 cache timeout    */
    TEST_ASSERT_EQUAL_UINT16(0u,   resp_reg(buf, 1)); /* 337 pkt proc hi      */
    TEST_ASSERT_EQUAL_UINT16(120u, resp_reg(buf, 2)); /* 338 pkt proc lo      */
    TEST_ASSERT_EQUAL_UINT16(0u,   resp_reg(buf, 3)); /* 339 last pkt age hi  */
    TEST_ASSERT_EQUAL_UINT16(7u,   resp_reg(buf, 4)); /* 340 last pkt age lo  */
    TEST_ASSERT_EQUAL_UINT16(3u,   resp_reg(buf, 5)); /* 341 devices on bus   */
    TEST_ASSERT_EQUAL_UINT16(120u, resp_reg(buf, 6)); /* 342 poll freq ppm    */
}

/* ---- MBDEV-U-009: FC04 RAM diagnostics (65505, 65506) -------------------- */

/* Values reported in KB (floor division by 1024).
 * total=200000, free=150000: free RAM = 150000/1024 = 146 KB;
 * used RAM = (200000-150000)/1024 = 50000/1024 = 48 KB. */
void test_ram_diagnostics(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-009: FC04 RAM free/used (KB)");
    LOG_MESSAGE();

    mock_heap_set_sizes(200000u, 150000u);

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             65505u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 2u, n);
    TEST_ASSERT_EQUAL_UINT16(146u, resp_reg(buf, 0)); /* free RAM in KB */

    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      65506u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 2u, n);
    TEST_ASSERT_EQUAL_UINT16(48u, resp_reg(buf, 0)); /* used RAM in KB */
}

/* ---- MBDEV-U-010: FC04 RAM u16 clamp at KB scale ------------------------- */

/* The u16 register still clamps when the KB value exceeds 0xFFFF.
 * total=200 MB, free=100 MB: used = 100 MB = 102400 KB > 65535 -> 0xFFFF;
 * free = 100 MB = 102400 KB > 65535 -> 0xFFFF. */
void test_ram_used_clamp(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-010: FC04 RAM u16 clamp (KB)");
    LOG_MESSAGE();

    mock_heap_set_sizes(200u * 1024u * 1024u, 100u * 1024u * 1024u);

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    /* used RAM = 100 MB = 102400 KB -> clamps to 0xFFFF */
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             65506u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 2u, n);
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, resp_reg(buf, 0));

    /* free RAM = 100 MB = 102400 KB -> clamps to 0xFFFF */
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      65505u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 2u, n);
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, resp_reg(buf, 0));
}

/* ---- MBDEV-U-011: FC04 stack diagnostics (65504, 65507) ------------------ */

/* Values reported in KB (floor division by 1024).
 * sizeof(StackType_t)==1, high-water=800 words -> free_min=800 bytes.
 * stack_bytes=3072: max stack used = (3072-800)/1024 = 2272/1024 = 2 KB;
 * stack size = 3072/1024 = 3 KB. */
void test_stack_diagnostics(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-011: FC04 stack used + size (KB)");
    LOG_MESSAGE();

    mock_set_stack_high_water_mark(800u);

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             65504u, 1u, 3072u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 2u, n);
    TEST_ASSERT_EQUAL_UINT16(2u, resp_reg(buf, 0)); /* max stack used in KB */

    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      65507u, 1u, 3072u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 2u, n);
    TEST_ASSERT_EQUAL_UINT16(3u, resp_reg(buf, 0)); /* stack size in KB */
}

/* ---- MBDEV-U-012: FC04 stack used = 0 when unknown/corrupted ------------- */

/* free_min >= stack_bytes (or stack_bytes==0) -> max stack used reports 0. */
void test_stack_used_unknown(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-012: FC04 stack used = 0 (unknown)");
    LOG_MESSAGE();

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    /* high-water (4000 bytes) exceeds the reported stack size (3072). */
    mock_set_stack_high_water_mark(4000u);
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             65504u, 1u, 3072u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 2u, n);
    TEST_ASSERT_EQUAL_UINT16(0u, resp_reg(buf, 0));

    /* stack_bytes == 0 -> unknown -> 0 */
    mock_set_stack_high_water_mark(800u);
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      65504u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 2u, n);
    TEST_ASSERT_EQUAL_UINT16(0u, resp_reg(buf, 0));
}

/* ---- MBDEV-U-013: FC04 reboot reason (65508) ----------------------------- */

/* POWERON->5 (POR), BROWNOUT->1 (LPWR), TASK_WDT->3 (IWDG). */
void test_reboot_reason(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-013: FC04 reboot reason mapping");
    LOG_MESSAGE();

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    mock_set_reset_reason(ESP_RST_POWERON);
    mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                  65508u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT16(5u, resp_reg(buf, 0)); /* WB_REBOOT_POR */

    mock_set_reset_reason(ESP_RST_BROWNOUT);
    mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                  65508u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT16(1u, resp_reg(buf, 0)); /* WB_REBOOT_LPWR */

    mock_set_reset_reason(ESP_RST_TASK_WDT);
    mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                  65508u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT16(3u, resp_reg(buf, 0)); /* WB_REBOOT_IWDG */
}

/* ---- MBDEV-U-014: FC03 signature string (290) ---------------------------- */

/* device_signature="sigXYZ" decoded from the holding-register string field. */
void test_signature_string_fc03(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-014: FC03 signature string");
    LOG_MESSAGE();

    const uint16_t count = 4u; /* 8 chars covers "sigXYZ" (6) + pad */
    uint8_t buf[260];
    uint8_t exc = 0xAA;
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_HOLDING, 0x0001,
                                             290u, count, 0u, buf, &exc);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + (size_t)count * 2u, n);
    /* function code in the response must reflect FC03 */
    const mb_tcp_header_t *hdr = (const mb_tcp_header_t *)buf;
    TEST_ASSERT_EQUAL_HEX8(FC_READ_HOLDING, hdr->function);

    char decoded[64] = {0};
    decode_string(buf, count, decoded);
    TEST_ASSERT_EQUAL_STRING("sigXYZ", decoded);
}

/* ---- MBDEV-U-015: error paths -------------------------------------------- */

/* Undefined registers / unsupported function / partial range / overflow. */
void test_error_paths(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-015: error paths");
    LOG_MESSAGE();

    uint8_t buf[260];
    uint8_t exc;
    size_t n;

    /* Undefined input register (FC04 @ 500) -> illegal data address */
    exc = 0;
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      500u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(0u, n);
    TEST_ASSERT_EQUAL_HEX8(EX_ILLEGAL_ADDRESS, exc);

    /* Undefined holding register (FC03 @ 100) -> illegal data address */
    exc = 0;
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_HOLDING, 0x0001,
                                      100u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(0u, n);
    TEST_ASSERT_EQUAL_HEX8(EX_ILLEGAL_ADDRESS, exc);

    /* Unsupported function code (FC01 coils) -> illegal function */
    exc = 0;
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_COILS, 0x0001,
                                      104u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(0u, n);
    TEST_ASSERT_EQUAL_HEX8(EX_ILLEGAL_FUNCTION, exc);

    /* Range spanning defined+undefined (104,105 defined; 106 undefined) */
    exc = 0;
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      104u, 3u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(0u, n);
    TEST_ASSERT_EQUAL_HEX8(EX_ILLEGAL_ADDRESS, exc);

    /* Address-space overflow (start=0xFFFF, count=2 => 0x10001 > 0x10000) */
    exc = 0;
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      0xFFFFu, 2u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(0u, n);
    TEST_ASSERT_EQUAL_HEX8(EX_ILLEGAL_ADDRESS, exc);
}

/* ---- MBDEV-U-016: mb_device_is_self -------------------------------------- */

void test_is_self(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-016: mb_device_is_self");
    LOG_MESSAGE();

    TEST_ASSERT_TRUE(mb_device_is_self(0xFFu));
    TEST_ASSERT_FALSE(mb_device_is_self(0x00u));
    TEST_ASSERT_FALSE(mb_device_is_self(0x01u));
    TEST_ASSERT_FALSE(mb_device_is_self(0xFEu));
}

/* ---- main ---------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_uptime_and_mbap_header);
    RUN_TEST(test_supply_voltage);
    RUN_TEST(test_model_string);
    RUN_TEST(test_fw_version_string);
    RUN_TEST(test_serial_registers);
    RUN_TEST(test_fw_numeric_plus_suffix);
    RUN_TEST(test_fw_numeric_rc_suffix);
    RUN_TEST(test_stats_block);
    RUN_TEST(test_ram_diagnostics);
    RUN_TEST(test_ram_used_clamp);
    RUN_TEST(test_stack_diagnostics);
    RUN_TEST(test_stack_used_unknown);
    RUN_TEST(test_reboot_reason);
    RUN_TEST(test_signature_string_fc03);
    RUN_TEST(test_error_paths);
    RUN_TEST(test_is_self);

    return UNITY_END();
}
