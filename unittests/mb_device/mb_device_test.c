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

/* Decode a WB string field packed ONE character per register, in the low byte
 * (model 200, firmware version 250, signature 290). This mirrors what the
 * standard read does:  modbus_client -t3 -r 290 -c 12 | sed 's/ 0x00/\x/g'
 * count = number of registers in the field. */
static void decode_string_1c(const uint8_t *buf, uint16_t count, char *out)
{
    for (uint16_t i = 0; i < count; i++) {
        uint16_t r = resp_reg(buf, i);
        /* the high byte must be zero — anything else means the field was packed
         * with the wrong layout and WB tooling would read garbage */
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00u, (uint8_t)(r >> 8),
            "1-char-per-register field must leave the high byte zero");
        out[i] = (char)(r & 0xFFu);
    }
    out[count] = '\0';
}

/* Decode a WB string field packed TWO characters per register, first character
 * in the LOW byte (git info, 220-244). count = number of registers in the field. */
static void decode_string_2c(const uint8_t *buf, uint16_t count, char *out)
{
    for (uint16_t i = 0; i < count; i++) {
        uint16_t r = resp_reg(buf, i);
        out[i * 2]     = (char)(r & 0xFFu);
        out[i * 2 + 1] = (char)(r >> 8);
    }
    out[count * 2] = '\0';
}

/* Assemble a raw Modbus TCP request frame (MBAP header + PDU) into buf:
 * tid_net is stored verbatim (network order); the PDU is [fc][start_hi][start_lo]
 * [count_hi][count_lo]. Returns the full frame length (MBAP_LEN + 5 = 13). */
static size_t make_req(uint8_t *buf, uint16_t tid_net, uint8_t unit_id, uint8_t fc,
                       uint16_t start, uint16_t count)
{
    mb_tcp_header_t *h = (mb_tcp_header_t *)buf;
    h->transaction_id = tid_net;
    h->protocol_id    = 0x0000;
    h->length         = modbus_swap16(6u); /* unit_id + fc + 4 PDU bytes */
    h->unit_id        = unit_id;
    h->function       = fc;
    buf[MBAP_LEN + 0] = (uint8_t)(start >> 8);
    buf[MBAP_LEN + 1] = (uint8_t)(start & 0xFFu);
    buf[MBAP_LEN + 2] = (uint8_t)(count >> 8);
    buf[MBAP_LEN + 3] = (uint8_t)(count & 0xFFu);
    return MBAP_LEN + 5u;
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

/* ---- MBDEV-U-003: FC04 model string (200) -------------------------------- */

/* The model field is 20 registers wide for a 20-character field: ONE character
 * per register, in the LOW byte (high byte 0x00) — the WB reference layout.
 * device_name="TEST-DEV" therefore reads 0x0054 0x0045 0x0053 0x0054 ... */
void test_model_string(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-003: FC04 model string (1 char/reg, low byte)");
    LOG_MESSAGE();

    uint8_t buf[260];
    uint8_t exc = 0xAA;
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             200u, 9u, 0u, buf, &exc);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 18u, n);
    TEST_ASSERT_EQUAL_HEX16(0x0054, resp_reg(buf, 0)); /* 'T' */
    TEST_ASSERT_EQUAL_HEX16(0x0045, resp_reg(buf, 1)); /* 'E' */
    TEST_ASSERT_EQUAL_HEX16(0x0053, resp_reg(buf, 2)); /* 'S' */
    TEST_ASSERT_EQUAL_HEX16(0x0054, resp_reg(buf, 3)); /* 'T' */
    TEST_ASSERT_EQUAL_HEX16(0x002D, resp_reg(buf, 4)); /* '-' */
    TEST_ASSERT_EQUAL_HEX16(0x0044, resp_reg(buf, 5)); /* 'D' */
    TEST_ASSERT_EQUAL_HEX16(0x0045, resp_reg(buf, 6)); /* 'E' */
    TEST_ASSERT_EQUAL_HEX16(0x0056, resp_reg(buf, 7)); /* 'V' */
    TEST_ASSERT_EQUAL_HEX16(0x0000, resp_reg(buf, 8)); /* trailing zero-pad */

    char decoded[64] = {0};
    decode_string_1c(buf, 8u, decoded);
    TEST_ASSERT_EQUAL_STRING("TEST-DEV", decoded);
}

/* ---- MBDEV-U-004: FC04 firmware version string (250) --------------------- */

/* firmware_ver="1.2.3+wb5" decodes back from the packed registers. */
void test_fw_version_string(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-004: FC04 firmware version string");
    LOG_MESSAGE();

    const uint16_t count = 9u; /* 1 char/reg: "1.2.3+wb5" is 9 chars -> 9 regs */
    uint8_t buf[260];
    uint8_t exc = 0xAA;
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             250u, count, 0u, buf, &exc);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + (size_t)count * 2u, n);

    char decoded[64] = {0};
    decode_string_1c(buf, count, decoded);
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

/* ---- MBDEV-U-008: FC04 statistics block (337..343) ----------------------- */

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
                                             337u, 7u, 0u, buf, &exc);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 14u, n);
    TEST_ASSERT_EQUAL_UINT16(0u,   resp_reg(buf, 0)); /* 337 pkt proc hi      */
    TEST_ASSERT_EQUAL_UINT16(120u, resp_reg(buf, 1)); /* 338 pkt proc lo      */
    TEST_ASSERT_EQUAL_UINT16(0u,   resp_reg(buf, 2)); /* 339 last pkt age hi  */
    TEST_ASSERT_EQUAL_UINT16(7u,   resp_reg(buf, 3)); /* 340 last pkt age lo  */
    TEST_ASSERT_EQUAL_UINT16(3u,   resp_reg(buf, 4)); /* 341 devices on bus   */
    TEST_ASSERT_EQUAL_UINT16(120u, resp_reg(buf, 5)); /* 342 poll freq ppm    */
    TEST_ASSERT_EQUAL_UINT16(42u,  resp_reg(buf, 6)); /* 343 cache timeout    */
}

/* Register 336 (0x0150) must stay undefined: it is the last register of the
 * standard WB bootloader-version field, so the gateway must not answer it. */
void test_bootloader_version_slot_not_claimed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-008b: reg 336 (bootloader-version slot) is not claimed");
    LOG_MESSAGE();

    uint8_t buf[260];
    uint8_t exc = 0;
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             336u, 1u, 0u, buf, &exc);

    TEST_ASSERT_EQUAL_UINT(0u, n);
    TEST_ASSERT_EQUAL_HEX8(EX_ILLEGAL_ADDRESS, exc);
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

    const uint16_t count = 6u; /* 1 char/reg: "sigXYZ" is 6 chars -> 6 regs */
    uint8_t buf[260];
    uint8_t exc = 0xAA;
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_HOLDING, 0x0001,
                                             290u, count, 0u, buf, &exc);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + (size_t)count * 2u, n);
    /* function code in the response must reflect FC03 */
    const mb_tcp_header_t *hdr = (const mb_tcp_header_t *)buf;
    TEST_ASSERT_EQUAL_HEX8(FC_READ_HOLDING, hdr->function);

    char decoded[64] = {0};
    decode_string_1c(buf, count, decoded);
    TEST_ASSERT_EQUAL_STRING("sigXYZ", decoded);

    /* This is the read wb-mcu-fw-flasher --get-device-info performs, so the whole
     * 12-register field must come back with every high byte zero. */
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_HOLDING, 0x0001,
                                      290u, 12u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 24u, n);
    char full[64] = {0};
    decode_string_1c(buf, 12u, full);
    TEST_ASSERT_EQUAL_STRING("sigXYZ", full);   /* the tail is zero-padded */
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

/* ====================================================================== */
/* TASK B: mb_device_handle_self_request (validation + dispatch)          */
/* ====================================================================== */

/* Decode the MBAP length field (network order) from a response buffer. */
static uint16_t resp_mbap_length(const uint8_t *buf)
{
    const mb_tcp_header_t *hdr = (const mb_tcp_header_t *)buf;
    return modbus_swap16(hdr->length);
}

/* ---- MBDEV-U-017: bad FC 0x06 -> exception 0x86/0x01 --------------------- */

/* FC06 (write single register) is unsupported -> ILLEGAL_FUNCTION exception. */
void test_self_req_bad_fc_06(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-017: self-req bad FC 0x06 -> exception");
    LOG_MESSAGE();

    uint8_t req[260];
    uint8_t resp[260];
    uint16_t tid_net = 0x3412;
    size_t req_len = make_req(req, tid_net, DEV_UNIT_ID, 0x06u, 104u, 2u);

    size_t n = mb_device_handle_self_request(req, req_len, 0u, resp);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u, n);
    TEST_ASSERT_EQUAL_HEX8(0x86u, resp[7]);  /* fc | 0x80          */
    TEST_ASSERT_EQUAL_HEX8(0x01u, resp[8]);  /* ILLEGAL_FUNCTION   */
    TEST_ASSERT_EQUAL_UINT16(3u, resp_mbap_length(resp));
    const mb_tcp_header_t *hdr = (const mb_tcp_header_t *)resp;
    TEST_ASSERT_EQUAL_HEX16(tid_net, hdr->transaction_id); /* echoed verbatim */
}

/* ---- MBDEV-U-018: bad FC 0x01 -> exception 0x81/0x01 --------------------- */

void test_self_req_bad_fc_01(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-018: self-req bad FC 0x01 -> exception");
    LOG_MESSAGE();

    uint8_t req[260];
    uint8_t resp[260];
    size_t req_len = make_req(req, 0x0001, DEV_UNIT_ID, 0x01u, 104u, 2u);

    size_t n = mb_device_handle_self_request(req, req_len, 0u, resp);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u, n);
    TEST_ASSERT_EQUAL_HEX8(0x81u, resp[7]);  /* fc | 0x80        */
    TEST_ASSERT_EQUAL_HEX8(0x01u, resp[8]);  /* ILLEGAL_FUNCTION */
}

/* ---- MBDEV-U-019: truncated request -> exception 0x03 -------------------- */

/* req_len == MBAP + 3 (one PDU byte short) with a valid FC -> ILLEGAL_DATA_VALUE. */
void test_self_req_truncated(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-019: self-req truncated PDU -> exception 0x03");
    LOG_MESSAGE();

    uint8_t req[260];
    uint8_t resp[260];
    make_req(req, 0x0001, DEV_UNIT_ID, FC_READ_HOLDING, 104u, 2u);
    size_t req_len = MBAP_LEN + 3u; /* one byte short of the 4 PDU bytes */

    size_t n = mb_device_handle_self_request(req, req_len, 0u, resp);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u, n);
    TEST_ASSERT_EQUAL_HEX8(0x83u, resp[7]);  /* FC03 | 0x80        */
    TEST_ASSERT_EQUAL_HEX8(0x03u, resp[8]);  /* ILLEGAL_DATA_VALUE */
}

/* ---- MBDEV-U-020: count == 0 -> exception 0x03 --------------------------- */

void test_self_req_count_zero(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-020: self-req count==0 -> exception 0x03");
    LOG_MESSAGE();

    uint8_t req[260];
    uint8_t resp[260];
    size_t req_len = make_req(req, 0x0001, DEV_UNIT_ID, FC_READ_INPUT, 104u, 0u);

    size_t n = mb_device_handle_self_request(req, req_len, 0u, resp);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u, n);
    TEST_ASSERT_EQUAL_HEX8(0x03u, resp[8]);  /* ILLEGAL_DATA_VALUE */
}

/* ---- MBDEV-U-021: count == 126 -> exception 0x03 ------------------------- */

void test_self_req_count_too_big(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-021: self-req count==126 -> exception 0x03");
    LOG_MESSAGE();

    uint8_t req[260];
    uint8_t resp[260];
    size_t req_len = make_req(req, 0x0001, DEV_UNIT_ID, FC_READ_INPUT, 104u, 126u);

    size_t n = mb_device_handle_self_request(req, req_len, 0u, resp);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u, n);
    TEST_ASSERT_EQUAL_HEX8(0x03u, resp[8]);  /* ILLEGAL_DATA_VALUE */
}

/* ---- MBDEV-U-022: count == 125 passes the gate, address bad -> 0x02 ------ */

/* count==125 is the max allowed: the quantity gate must PASS, then the builder
 * hits an undefined register (106) and returns ILLEGAL_DATA_ADDRESS (0x02),
 * NOT ILLEGAL_DATA_VALUE (0x03). This distinguishes the two failure causes. */
void test_self_req_count_max_addr_bad(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-022: self-req count==125 gate passes, addr bad -> 0x02");
    LOG_MESSAGE();

    uint8_t req[260];
    uint8_t resp[260];
    size_t req_len = make_req(req, 0x0001, DEV_UNIT_ID, FC_READ_INPUT, 104u, 125u);

    size_t n = mb_device_handle_self_request(req, req_len, 0u, resp);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u, n);
    TEST_ASSERT_EQUAL_HEX8(0x02u, resp[8]);  /* ILLEGAL_DATA_ADDRESS (count was accepted) */
}

/* ---- MBDEV-U-023: valid FC04 read -> success ADU matches builder --------- */

/* FC04 uptime @104 count 2: the success ADU must byte-for-byte equal what
 * mb_device_build_read_response produces for the same inputs. */
void test_self_req_valid_fc04(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-023: self-req valid FC04 -> success ADU");
    LOG_MESSAGE();

    mock_esp_timer_get_time_value = 5000000ULL; /* 5 seconds */

    uint8_t req[260];
    uint8_t resp[260];
    uint16_t tid_net = 0x3412;
    size_t req_len = make_req(req, tid_net, DEV_UNIT_ID, FC_READ_INPUT, 104u, 2u);

    size_t n = mb_device_handle_self_request(req, req_len, 0u, resp);

    /* total = MBAP(8) + byte_count_field(1) + data(4) = 13 */
    TEST_ASSERT_EQUAL_UINT(13u, n);
    TEST_ASSERT_EQUAL_HEX8(0x04u, resp[7]); /* FC04 echoed (no exception bit) */
    TEST_ASSERT_EQUAL_HEX8(4u, resp[8]);    /* byte count                     */

    /* Compare against the direct builder output for identical inputs. */
    uint8_t ref[260];
    uint8_t exc = 0xAA;
    size_t rn = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, tid_net,
                                              104u, 2u, 0u, ref, &exc);
    TEST_ASSERT_EQUAL_UINT(rn, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ref, resp, n);
}

/* ---- MBDEV-U-024: valid FC03 signature read -> success ADU --------------- */

void test_self_req_valid_fc03(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-024: self-req valid FC03 -> success ADU");
    LOG_MESSAGE();

    uint8_t req[260];
    uint8_t resp[260];
    size_t req_len = make_req(req, 0x0001, DEV_UNIT_ID, FC_READ_HOLDING, 290u, 6u);

    size_t n = mb_device_handle_self_request(req, req_len, 0u, resp);

    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 12u, n);
    TEST_ASSERT_EQUAL_HEX8(0x03u, resp[7]); /* FC03 echoed   */
    TEST_ASSERT_EQUAL_HEX8(12u, resp[8]);   /* byte count    */
}

/* ====================================================================== */
/* TASK C: close measured coverage gaps                                   */
/* ====================================================================== */

/* ---- MBDEV-U-025: firmware git-info string registers (REG_GIT_BASE) ------ */

/* The git-info string field (220..244, 25 regs) packs sys_info.firmware_git_info.
 * Read enough regs to span the string and decode it; also read the full 25-reg
 * field so the upper bound and trailing zero-pad are exercised. */
void test_git_info_string(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-025: FC04 firmware git-info string");
    LOG_MESSAGE();

    strcpy(sys_info.firmware_git_info, "g1a2b3c4_main");

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    /* 13 chars -> 7 regs cover the string + pad */
    const uint16_t count = 7u;
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                             220u, count, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + (size_t)count * 2u, n);

    char decoded[64] = {0};
    decode_string_2c(buf, count, decoded);
    TEST_ASSERT_EQUAL_STRING("g1a2b3c4_main", decoded);

    /* Byte order within the pair: 'g' is the FIRST character, so it must be in
     * the LOW byte of register 220 and '1' in the high byte. */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x3167u, resp_reg(buf, 0),
        "git info packs the first character of the pair in the low byte");

    /* Full 25-reg field: upper bound + trailing zero pad. */
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      220u, 25u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT(MBAP_LEN + 1u + 50u, n);
    char full[64] = {0};
    decode_string_2c(buf, 25u, full);
    TEST_ASSERT_EQUAL_STRING("g1a2b3c4_main", full); /* trailing zeros stripped by NUL */
}

/* ---- MBDEV-U-026: reboot-reason mapping (remaining ESP_RST_* codes) ------ */

/* Exercise every reboot-reason branch not covered by MBDEV-U-013:
 * DEEPSLEEP->1, INT_WDT->2, WDT->3, SW->4, PANIC->4, EXT->6, UNKNOWN->0. */
void test_reboot_reason_all(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-026: FC04 reboot reason (remaining codes)");
    LOG_MESSAGE();

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    struct { esp_reset_reason_t in; uint16_t out; } cases[] = {
        { ESP_RST_DEEPSLEEP, 1u }, /* WB_REBOOT_LPWR */
        { ESP_RST_INT_WDT,   2u }, /* WB_REBOOT_WWDG */
        { ESP_RST_WDT,       3u }, /* WB_REBOOT_IWDG */
        { ESP_RST_SW,        4u }, /* WB_REBOOT_SFT  */
        { ESP_RST_PANIC,     4u }, /* WB_REBOOT_SFT  */
        { ESP_RST_EXT,       6u }, /* WB_REBOOT_PIN  */
        { ESP_RST_UNKNOWN,   0u }, /* WB_REBOOT_UNKNOWN (default) */
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        mock_set_reset_reason(cases[i].in);
        mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      65508u, 1u, 0u, buf, &exc);
        TEST_ASSERT_EQUAL_UINT16(cases[i].out, resp_reg(buf, 0));
    }
}

/* ---- MBDEV-U-027: poll-freq edge cases (REG_POLL_FREQ_PPM = 342) --------- */

/* (a) map_age_s == 0 -> else branch -> 0.
 * (b) packets*60/map_age > 0xFFFF -> saturation clamp -> 0xFFFF. */
void test_poll_freq_edges(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-027: FC04 poll-freq zero + saturation");
    LOG_MESSAGE();

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    /* (a) map_age_s == 0 -> 0 (avoid divide-by-zero else branch). */
    mock_cache_stats_set(1000u, 0u, 0u, 0u);
    mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                  342u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT16(0u, resp_reg(buf, 0));

    /* (b) packets*60/map_age = 1e6*60/1 = 6e7 > 0xFFFF -> clamps to 0xFFFF. */
    mock_cache_stats_set(1000000u, 0u, 1u, 0u);
    mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                  342u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, resp_reg(buf, 0));
}

/* ---- MBDEV-U-028: firmware version with NO suffix ------------------------ */

/* "4.5.6" has neither +wb nor -rc: SUFFIX reg (323) == 0, and the encoded
 * suffix in the LE-low word low byte == 0x80 (suffix 0 -> enc 0+128). */
void test_fw_numeric_no_suffix(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-028: FC04 firmware numeric (no suffix)");
    LOG_MESSAGE();

    strcpy(sys_info.firmware_ver, "4.5.6");

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    /* SUFFIX register (323) == 0 (no +wb / no -rc). */
    mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                  323u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, resp_reg(buf, 0));

    /* LE low word (324): patch(0x06) in hi byte, enc(0+128 = 0x80) in lo byte. */
    mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                  324u, 1u, 0u, buf, &exc);
    uint16_t le_lo = resp_reg(buf, 0);
    TEST_ASSERT_EQUAL_HEX8(0x80u, (uint8_t)(le_lo & 0xFFu)); /* encoded suffix */
    TEST_ASSERT_EQUAL_HEX16(0x0680u, le_lo);
}

/* ---- MBDEV-U-029: string end-of-field boundaries ------------------------- */

/* End-of-string handling for both packing layouts.
 *
 * 1-char/reg (firmware version @250): the last character sits alone in the low
 * byte of its register and every register past the string reads 0x0000.
 *
 * 2-char/reg (git info @220): an odd-length string leaves the last register
 * half-filled — the final character in the LOW byte, the missing second
 * character zero-padded in the high byte. */
void test_pack_string_odd_boundary(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBDEV-U-029: string end-of-field boundaries");
    LOG_MESSAGE();

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    /* --- 1 char per register: "ABCDE" at base 250 --- */
    strcpy(sys_info.firmware_ver, "ABCDE");

    mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                  250u, 7u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x0041u, resp_reg(buf, 0), "reg 250 = 'A' in the low byte");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x0045u, resp_reg(buf, 4), "reg 254 = 'E', the last character");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x0000u, resp_reg(buf, 5), "reg 255 is past the string: zero");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x0000u, resp_reg(buf, 6), "reg 256 is past the string: zero");

    /* --- 2 chars per register: "ABC" (odd) at base 220 --- */
    strcpy(sys_info.firmware_git_info, "ABC");

    mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                  220u, 3u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x4241u, resp_reg(buf, 0),
        "reg 220 = 'A' (low) + 'B' (high)");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x0043u, resp_reg(buf, 1),
        "reg 221 = 'C' (low) + zero pad (high) — the odd tail");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x0000u, resp_reg(buf, 2),
        "reg 222 is past the string: zero");
}

/* ---- MBDEV-U-030: FC03 and FC04 share ONE address space ------------------ */

/* The WB common register map nominally files model / git / firmware version /
 * serial under INPUT registers and the signature under HOLDING registers, but the
 * reference firmwares answer the whole map on both function codes and the standard
 * tooling reads it that way (wb-mcu-fw-flasher --get-device-info, and the usual
 * `modbus_client -t3 -r <addr>` invocations). Both FCs therefore resolve through
 * the same map: every defined address — the signature included — must return an
 * identical value on FC03 and FC04. */
void test_info_block_readable_via_fc03(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "MBDEV-U-030: FC03 and FC04 must serve one shared address space");
    LOG_MESSAGE();

    static const uint16_t addrs[] = {
        200u, 219u,   /* model            */
        220u, 244u,   /* git info         */
        250u, 265u,   /* firmware version */
        266u, 269u,   /* serial extension */
        270u, 271u,   /* serial number    */
        290u, 301u,   /* signature        */
        320u, 327u,   /* numeric version  */
    };

    uint8_t in_buf[260];
    uint8_t hold_buf[260];

    for (size_t i = 0; i < sizeof(addrs) / sizeof(addrs[0]); i++) {
        uint8_t exc_in = 0xAA, exc_hold = 0xAA;

        size_t n_in = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                                    addrs[i], 1u, 0u, in_buf, &exc_in);
        size_t n_hold = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_HOLDING, 0x0001,
                                                      addrs[i], 1u, 0u, hold_buf, &exc_hold);

        TEST_ASSERT_TRUE_MESSAGE(n_in > 0, "the address must be served on FC04");
        TEST_ASSERT_TRUE_MESSAGE(n_hold > 0, "the same address must be served on FC03");
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(resp_reg(in_buf, 0), resp_reg(hold_buf, 0),
            "FC03 and FC04 must return the same value for a shared-map register");
    }

    uint8_t buf[260];
    uint8_t exc = 0xAA;

    /* The signature keeps working on FC03 — this is the read WB tooling performs. */
    size_t n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_HOLDING, 0x0001,
                                             290u, 6u, 0u, buf, &exc);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "the signature must still be served on FC03");
    char decoded[64] = {0};
    decode_string_1c(buf, 6u, decoded);
    TEST_ASSERT_EQUAL_STRING("sigXYZ", decoded);

    /* ...and, the map being shared, the very same field now answers on FC04 too.
     * This used to be an ILLEGAL DATA ADDRESS: the signature was the one field
     * that broke the FC03/FC04 symmetry. */
    exc = 0xAA;
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      290u, 6u, 0u, buf, &exc);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "the signature must now be served on FC04 as well");
    char decoded_fc04[64] = {0};
    decode_string_1c(buf, 6u, decoded_fc04);
    TEST_ASSERT_EQUAL_STRING("sigXYZ", decoded_fc04);

    /* An address defined in the map is illegal on neither FC; one defined nowhere
     * is illegal on both. */
    exc = 0xAA;
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_HOLDING, 0x0001,
                                      1000u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, n, "an undefined address must not be served on FC03");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x02u, exc, "undefined address -> ILLEGAL DATA ADDRESS");

    exc = 0xAA;
    n = mb_device_build_read_response(DEV_UNIT_ID, FC_READ_INPUT, 0x0001,
                                      1000u, 1u, 0u, buf, &exc);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, n, "an undefined address must not be served on FC04");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x02u, exc, "undefined address -> ILLEGAL DATA ADDRESS");
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
    RUN_TEST(test_bootloader_version_slot_not_claimed);
    RUN_TEST(test_ram_diagnostics);
    RUN_TEST(test_ram_used_clamp);
    RUN_TEST(test_stack_diagnostics);
    RUN_TEST(test_stack_used_unknown);
    RUN_TEST(test_reboot_reason);
    RUN_TEST(test_signature_string_fc03);
    RUN_TEST(test_error_paths);
    RUN_TEST(test_is_self);

    /* TASK B: mb_device_handle_self_request */
    RUN_TEST(test_self_req_bad_fc_06);
    RUN_TEST(test_self_req_bad_fc_01);
    RUN_TEST(test_self_req_truncated);
    RUN_TEST(test_self_req_count_zero);
    RUN_TEST(test_self_req_count_too_big);
    RUN_TEST(test_self_req_count_max_addr_bad);
    RUN_TEST(test_self_req_valid_fc04);
    RUN_TEST(test_self_req_valid_fc03);

    /* TASK C: coverage gaps */
    RUN_TEST(test_git_info_string);
    RUN_TEST(test_reboot_reason_all);
    RUN_TEST(test_poll_freq_edges);
    RUN_TEST(test_fw_numeric_no_suffix);
    RUN_TEST(test_pack_string_odd_boundary);
    RUN_TEST(test_info_block_readable_via_fc03);

    return UNITY_END();
}
