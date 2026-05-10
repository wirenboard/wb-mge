#include "unity.h"
#include "console_log.h"

#include "cache_multimaster.h"
#include "modbus_helpers.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>

#include "cache_modbus_server_internal.h"

/* ---- Mock state variables exposed by mocks/cache_multimaster.c ----------- */

extern cache_lookup_result_t mock_lookup_result;
extern uint16_t              mock_lookup_value;
extern int                   mock_lookup_call_count;
extern uint8_t               mock_lookup_last_slave_id;
extern uint8_t               mock_lookup_last_fc;
extern uint16_t              mock_lookup_last_address;
extern uint16_t              mock_lookup_last_timeout;

extern cache_lookup_result_t mock_lookup_results[];
extern uint16_t              mock_lookup_values_arr[];
extern int                   mock_lookup_arr_count;
extern int                   mock_lookup_arr_index;

extern bool mock_cache_enabled;

void mock_cache_multimaster_reset(void);

/* ---- Mock state exposed by mocks/setting_items.c ------------------------- */

void mock_setting_items_set_timeout(int timeout_s);
void mock_setting_items_reset(void);

/* ---- Mock state exposed by mocks/tcp_server.c ---------------------------- */

extern uint8_t mock_tcp_send_buf[];
extern size_t  mock_tcp_send_len;
extern int     mock_tcp_send_called;

void mock_tcp_server_reset(void);

/* ---- Modbus constants (duplicated from cache_modbus_server.c) ------------ */

#define MB_FC_READ_COILS            0x01u
#define MB_FC_READ_DISCRETE_INPUTS  0x02u
#define MB_FC_READ_HOLDING_REGS     0x03u
#define MB_FC_READ_INPUT_REGS       0x04u

#define MB_EX_ILLEGAL_ADDRESS    0x02u
#define MB_EX_GW_TARGET_FAILED   0x0Bu

/* ---- setUp / tearDown ---------------------------------------------------- */

void setUp(void)
{
    mock_cache_multimaster_reset();
    mock_tcp_server_reset();
    mock_setting_items_reset();
}

void tearDown(void)
{
}

/* ---- CMS-U-001a: FC03, 1 register, correct layout ----------------------- */

/* Verify that build_register_response() for FC03 with 1 register:
 *   - returns total length 11 (8 MBAP + 1 byte_count field + 2 data bytes)
 *   - fills MBAP header correctly (transaction_id echoed, protocol_id=0,
 *     length=htons(5), unit_id, function)
 *   - fills payload: byte_count=2, then value big-endian */
void test_build_register_response_fc03_one_register(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-001a: FC03 1 register correct layout");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0x1234;

    uint8_t  resp_buf[512];
    uint8_t  exception_code = 0;
    uint16_t tid = htons(1);

    size_t len = cache_modbus_server_build_register_response(
        /*unit_id=*/1, /*fc=*/MB_FC_READ_HOLDING_REGS, /*transaction_id=*/tid,
        /*start_addr=*/100, /*count=*/1, /*timeout=*/0,
        resp_buf, &exception_code);

    /* Total length: 8 (MBAP) + 1 (byte_count field) + 2 (1 reg × 2 bytes) = 11 */
    TEST_ASSERT_EQUAL_size_t(11, len);

    mb_tcp_header_t hdr;
    memcpy(&hdr, resp_buf, sizeof(hdr));

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(tid, hdr.transaction_id,
        "transaction_id must be echoed verbatim (network byte order)");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x0000, hdr.protocol_id,
        "protocol_id must be 0");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(htons(5), hdr.length,
        "MBAP length must be htons(5): unit_id(1)+FC(1)+byte_count_field(1)+data(2)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, hdr.unit_id,
        "unit_id must be 1");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_FC_READ_HOLDING_REGS, hdr.function,
        "function must be FC03");

    /* Payload starts right after the MBAP header */
    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, payload[0],
        "byte_count must be 2 (1 register × 2 bytes)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x12, payload[1],
        "register value high byte must be 0x12");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x34, payload[2],
        "register value low byte must be 0x34");
}

/* ---- CMS-U-001b: FC04, 3 registers, all same value ---------------------- */

/* Verify that build_register_response() with count=3 registers packs all three
 * values into the payload in order (big-endian per register). */
void test_build_register_response_fc04_three_registers(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-001b: FC04 3 registers packed correctly");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0xABCD;

    uint8_t  resp_buf[512];
    uint8_t  exception_code = 0;
    uint16_t tid = htons(7);

    size_t len = cache_modbus_server_build_register_response(
        /*unit_id=*/5, /*fc=*/MB_FC_READ_INPUT_REGS, /*transaction_id=*/tid,
        /*start_addr=*/200, /*count=*/3, /*timeout=*/0,
        resp_buf, &exception_code);

    /* Total length: 8 + 1 + 6 = 15 */
    TEST_ASSERT_EQUAL_size_t(15, len);

    mb_tcp_header_t hdr;
    memcpy(&hdr, resp_buf, sizeof(hdr));

    /* MBAP length = 1(unit_id) + 1(FC) + 1(byte_count field) + 6(data) = 9 */
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(htons(9), hdr.length,
        "MBAP length must be htons(9) for 3 registers");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(5, hdr.unit_id, "unit_id must be 5");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_FC_READ_INPUT_REGS, hdr.function,
        "function must be FC04");

    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(6, payload[0],
        "byte_count must be 6 (3 registers × 2 bytes)");
    /* All 3 registers should have value 0xABCD */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xAB, payload[1], "reg[0] hi = 0xAB");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xCD, payload[2], "reg[0] lo = 0xCD");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xAB, payload[3], "reg[1] hi = 0xAB");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xCD, payload[4], "reg[1] lo = 0xCD");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xAB, payload[5], "reg[2] hi = 0xAB");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xCD, payload[6], "reg[2] lo = 0xCD");

    /* Exactly 3 lookups must have been performed */
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, mock_lookup_call_count,
        "lookup must be called once per register");
}

/* ---- CMS-U-001c: FC03, 3 different register values via array mode -------- */

/* Verify sequential lookup with three different values packed correctly. */
void test_build_register_response_fc03_three_different_values(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-001c: FC03 3 different register values packed in order");
    LOG_MESSAGE();

    /* Set up array mode: 3 successive lookups return 0x0001, 0x0002, 0x0003 */
    mock_lookup_arr_count         = 3;
    mock_lookup_results[0]        = CACHE_LOOKUP_FOUND;
    mock_lookup_values_arr[0]     = 0x0001;
    mock_lookup_results[1]        = CACHE_LOOKUP_FOUND;
    mock_lookup_values_arr[1]     = 0x0002;
    mock_lookup_results[2]        = CACHE_LOOKUP_FOUND;
    mock_lookup_values_arr[2]     = 0x0003;

    uint8_t  resp_buf[512];
    uint8_t  exception_code = 0;

    size_t len = cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(2), 0, 3, 0, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_size_t(15, len);

    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(6, payload[0], "byte_count must be 6");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, payload[1], "reg[0] hi = 0x00");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x01, payload[2], "reg[0] lo = 0x01");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, payload[3], "reg[1] hi = 0x00");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x02, payload[4], "reg[1] lo = 0x02");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, payload[5], "reg[2] hi = 0x00");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x03, payload[6], "reg[2] lo = 0x03");
}

/* ---- CMS-U-002a: FC01, 1 coil ON --------------------------------------- */

/* Verify that a single ON coil sets bit 0 of the first payload byte. */
void test_build_coil_response_fc01_one_coil_on(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-002a: FC01 1 coil ON");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 1; /* coil is ON */

    uint8_t  resp_buf[512];
    uint8_t  exception_code = 0;
    uint16_t tid = htons(3);

    size_t len = cache_modbus_server_build_coil_response(
        /*unit_id=*/2, /*fc=*/MB_FC_READ_COILS, /*transaction_id=*/tid,
        /*start_addr=*/0, /*count=*/1, /*timeout=*/0,
        resp_buf, &exception_code);

    /* Total: 8 (MBAP) + 1 (coil_bytes field) + 1 (coil byte) = 10 */
    TEST_ASSERT_EQUAL_size_t(10, len);

    mb_tcp_header_t hdr;
    memcpy(&hdr, resp_buf, sizeof(hdr));

    /* MBAP length = 1(unit_id) + 1(FC) + 1(coil_bytes field) + 1(data) = 4 */
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(htons(4), hdr.length,
        "MBAP length must be htons(4) for 1 coil");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_FC_READ_COILS, hdr.function,
        "function must be FC01");

    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, payload[0],
        "coil_bytes must be 1 (ceil(1/8)=1)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x01, payload[1],
        "bit 0 must be set for coil[0]=ON");
}

/* ---- CMS-U-002b: FC01, 1 coil OFF ------------------------------------- */

/* Verify that a single OFF coil leaves the byte as 0x00. */
void test_build_coil_response_fc01_one_coil_off(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-002b: FC01 1 coil OFF");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0; /* coil is OFF */

    uint8_t resp_buf[512];
    uint8_t exception_code = 0;

    size_t len = cache_modbus_server_build_coil_response(
        1, MB_FC_READ_COILS, htons(1), 0, 1, 0, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_size_t(10, len);
    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, payload[1],
        "byte must be 0x00 for coil[0]=OFF");
}

/* ---- CMS-U-002c: FC01, 9 coils all ON --------------------------------- */

/* Verify that 9 coils produce coil_bytes=2, payload[1]=0xFF (coils 0-7),
 * payload[2]=0x01 (coil 8 in bit 0). */
void test_build_coil_response_fc01_nine_coils_all_on(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-002c: FC01 9 coils all ON");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 1; /* all coils ON */

    uint8_t resp_buf[512];
    uint8_t exception_code = 0;

    size_t len = cache_modbus_server_build_coil_response(
        1, MB_FC_READ_COILS, htons(5), 0, 9, 0, resp_buf, &exception_code);

    /* Total: 8 + 1 (coil_bytes field) + 2 (coil bytes for 9 coils) = 11 */
    TEST_ASSERT_EQUAL_size_t(11, len);

    mb_tcp_header_t hdr;
    memcpy(&hdr, resp_buf, sizeof(hdr));

    /* MBAP length = 1 + 1 + 1 + 2 = 5 */
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(htons(5), hdr.length,
        "MBAP length must be htons(5) for 9 coils");

    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, payload[0],
        "coil_bytes must be 2 (ceil(9/8)=2)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFF, payload[1],
        "coils 0-7 all ON: byte 0 must be 0xFF");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x01, payload[2],
        "coil 8 ON: byte 1 bit 0 must be set (0x01)");
}

/* ---- CMS-U-002d: FC02, 8 coils all OFF -------------------------------- */

/* Verify that 8 OFF discrete inputs produce payload[1]=0x00. */
void test_build_coil_response_fc02_eight_coils_zero(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-002d: FC02 8 discrete inputs all OFF");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0;

    size_t len = cache_modbus_server_build_coil_response(
        3, MB_FC_READ_DISCRETE_INPUTS, htons(9), 10, 8, 0, resp_buf, &exception_code);

    /* Total: 8 + 1 + 1 = 10 */
    TEST_ASSERT_EQUAL_size_t(10, len);

    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, payload[0],
        "coil_bytes must be 1 for 8 coils");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, payload[1],
        "all 8 coils OFF: payload byte must be 0x00");
}

/* ---- CMS-U-002e: FC01, bit packing — alternating ON/OFF ---------------- */

/* Verify that alternating ON/OFF coils produce the correct bit pattern. */
void test_build_coil_response_fc01_alternating_bits(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-002e: FC01 alternating coil bits");
    LOG_MESSAGE();

    /* 8 coils: ON, OFF, ON, OFF, ON, OFF, ON, OFF → 0x55 */
    mock_lookup_arr_count = 8;
    for (int i = 0; i < 8; i++) {
        mock_lookup_results[i]    = CACHE_LOOKUP_FOUND;
        mock_lookup_values_arr[i] = (uint16_t)(i % 2 == 0 ? 1 : 0);
    }

    uint8_t resp_buf[512];
    uint8_t exception_code = 0;

    size_t len = cache_modbus_server_build_coil_response(
        1, MB_FC_READ_COILS, htons(10), 0, 8, 0, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_size_t(10, len);
    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x55, payload[1],
        "alternating ON/OFF coils starting at 0 must produce 0x55");
}

/* ---- CMS-U-005a: register lookup returns NOT_FOUND ---------------------- */

/* Verify that a NOT_FOUND lookup causes the builder to return 0 with
 * exception_code set to 0x02 (MB_EX_ILLEGAL_ADDRESS). */
void test_build_register_response_not_found(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-005a: register NOT_FOUND → exception 0x02");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_NOT_FOUND;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0xFF; /* sentinel */

    size_t len = cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(1), 0, 1, 0, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_size_t(0, len);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_ILLEGAL_ADDRESS, exception_code,
        "exception_code must be 0x02 (ILLEGAL_ADDRESS) for NOT_FOUND");
}

/* ---- CMS-U-005b: second register NOT_FOUND after first found ------------ */

/* When the second lookup in a multi-register request returns NOT_FOUND, the
 * builder must return 0 and set exception_code even though the first succeeded. */
void test_build_register_response_second_not_found(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-005b: second register NOT_FOUND");
    LOG_MESSAGE();

    /* First call FOUND, second call NOT_FOUND */
    mock_lookup_arr_count     = 2;
    mock_lookup_results[0]    = CACHE_LOOKUP_FOUND;
    mock_lookup_values_arr[0] = 0x0001;
    mock_lookup_results[1]    = CACHE_LOOKUP_NOT_FOUND;
    mock_lookup_values_arr[1] = 0;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0xFF;

    size_t len = cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(2), 0, 2, 0, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_size_t(0, len);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_ILLEGAL_ADDRESS, exception_code,
        "exception_code must be 0x02 when second register is NOT_FOUND");
}

/* ---- CMS-U-006a: register lookup returns STALE -------------------------- */

/* Verify that a STALE lookup causes the builder to return 0 with
 * exception_code set to 0x0B (MB_EX_GW_TARGET_FAILED). */
void test_build_register_response_stale(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-006a: register STALE → exception 0x0B");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_STALE;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0xFF;

    size_t len = cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(1), 0, 1, 0, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_size_t(0, len);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_GW_TARGET_FAILED, exception_code,
        "exception_code must be 0x0B (GW_TARGET_FAILED) for STALE");
}

/* ---- CMS-U-006b: coil lookup returns NOT_FOUND -------------------------- */

void test_build_coil_response_not_found(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-006b: coil NOT_FOUND → exception 0x02");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_NOT_FOUND;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0xFF;

    size_t len = cache_modbus_server_build_coil_response(
        1, MB_FC_READ_COILS, htons(1), 0, 1, 0, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_size_t(0, len);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_ILLEGAL_ADDRESS, exception_code,
        "exception_code must be 0x02 for coil NOT_FOUND");
}

/* ---- CMS-U-006c: coil lookup returns STALE ------------------------------ */

void test_build_coil_response_stale(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-006c: coil STALE → exception 0x0B");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_STALE;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0xFF;

    size_t len = cache_modbus_server_build_coil_response(
        1, MB_FC_READ_COILS, htons(1), 0, 1, 0, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_size_t(0, len);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_GW_TARGET_FAILED, exception_code,
        "exception_code must be 0x0B for coil STALE");
}

/* ---- CMS-U-010: MBAP length field correctness for various counts --------- */

/* Verify the MBAP length formula for registers: htons(3 + count*2). */
void test_register_response_mbap_length_field(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-010a: register MBAP length for count=1..5");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0x0000;

    for (uint16_t count = 1; count <= 5; count++) {
        mock_lookup_call_count = 0; /* reset counter only (not full reset) */

        uint8_t  resp_buf[512];
        uint8_t  exception_code = 0;

        size_t len = cache_modbus_server_build_register_response(
            1, MB_FC_READ_HOLDING_REGS, htons(count), 0, count, 0,
            resp_buf, &exception_code);

        /* Expected total length: 8 + 1 + count*2 */
        size_t expected_len = 8u + 1u + (size_t)(count * 2u);
        TEST_ASSERT_EQUAL_size_t(expected_len, len);

        mb_tcp_header_t hdr;
        memcpy(&hdr, resp_buf, sizeof(hdr));

        /* MBAP length = 3 + count*2 */
        uint16_t expected_mbap_len = (uint16_t)(3u + count * 2u);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(htons(expected_mbap_len), hdr.length,
            "MBAP length must be htons(3 + count*2)");
    }
}

/* ---- CMS-U-010b: MBAP length field for coils ---------------------------- */

/* Verify the MBAP length formula for coils: htons(3 + ceil(count/8)). */
void test_coil_response_mbap_length_field(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-010b: coil MBAP length for various counts");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0;

    struct {
        uint16_t count;
        uint8_t  expected_coil_bytes;
        uint16_t expected_mbap_len;
        size_t   expected_total_len;
    } cases[] = {
        { 1, 1, 4, 10 },  /* ceil(1/8)=1, MBAP=3+1=4, total=8+1+1=10 */
        { 7, 1, 4, 10 },  /* ceil(7/8)=1, MBAP=3+1=4, total=8+1+1=10 */
        { 8, 1, 4, 10 },  /* ceil(8/8)=1, MBAP=3+1=4, total=8+1+1=10 */
        { 9, 2, 5, 11 },  /* ceil(9/8)=2, MBAP=3+2=5, total=8+1+2=11 */
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        mock_lookup_call_count = 0;

        uint8_t resp_buf[512];
        uint8_t exception_code = 0;

        size_t len = cache_modbus_server_build_coil_response(
            1, MB_FC_READ_COILS, htons((uint16_t)(i + 1)), 0, cases[i].count, 0,
            resp_buf, &exception_code);

        TEST_ASSERT_EQUAL_size_t(cases[i].expected_total_len, len);

        mb_tcp_header_t hdr;
        memcpy(&hdr, resp_buf, sizeof(hdr));
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(htons(cases[i].expected_mbap_len), hdr.length,
            "coil MBAP length mismatch");

        uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(cases[i].expected_coil_bytes, payload[0],
            "coil_bytes field mismatch");
    }
}

/* ---- CMS-U-013: value_timeout_s is passed to lookup -------------------- */

/* Verify that the timeout_s parameter is forwarded to cache_multimaster_lookup()
 * exactly as provided. */
void test_register_response_timeout_passed_to_lookup(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-013: timeout_s forwarded to lookup");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0x5A5A;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0;

    cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(1), 100, 1, 60, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(60, mock_lookup_last_timeout,
        "value_timeout_s must be passed verbatim to cache_multimaster_lookup()");
}

/* Verify that the coil builder also forwards value_timeout_s. */
void test_coil_response_timeout_passed_to_lookup(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-013b: coil timeout_s forwarded to lookup");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 1;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0;

    cache_modbus_server_build_coil_response(
        2, MB_FC_READ_COILS, htons(1), 50, 1, 120, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(120, mock_lookup_last_timeout,
        "value_timeout_s must be passed to cache_multimaster_lookup() for coils");
}

/* ---- CMS-U-014: lookup arguments (slave_id, fc, address) are correct ----- */

/* Verify that the builder calls lookup with the correct slave_id, fc, and
 * starting address for the first register. */
void test_register_response_lookup_args(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-014: register lookup args (slave_id, fc, addr)");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0;

    cache_modbus_server_build_register_response(
        /*unit_id=*/42, /*fc=*/MB_FC_READ_INPUT_REGS, htons(1),
        /*start_addr=*/300, /*count=*/1, /*timeout=*/10, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(42, mock_lookup_last_slave_id,
        "slave_id must equal unit_id");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_FC_READ_INPUT_REGS, mock_lookup_last_fc,
        "function_code must equal fc");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(300, mock_lookup_last_address,
        "address must equal start_addr for first register");
}

/* Verify address increments correctly for the last register in a range. */
void test_register_response_lookup_address_increments(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-014b: register lookup address increments");
    LOG_MESSAGE();

    /* Use array mode to capture the address of the last (5th) lookup */
    mock_lookup_arr_count = 5;
    for (int i = 0; i < 5; i++) {
        mock_lookup_results[i]    = CACHE_LOOKUP_FOUND;
        mock_lookup_values_arr[i] = 0;
    }

    uint8_t resp_buf[512];
    uint8_t exception_code = 0;

    cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(1), 100, 5, 0, resp_buf, &exception_code);

    /* Last lookup should be for address 100+4=104 */
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(104, mock_lookup_last_address,
        "last lookup address must be start_addr + count - 1");
}

/* ---- main ---------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_build_register_response_fc03_one_register);
    RUN_TEST(test_build_register_response_fc04_three_registers);
    RUN_TEST(test_build_register_response_fc03_three_different_values);

    RUN_TEST(test_build_coil_response_fc01_one_coil_on);
    RUN_TEST(test_build_coil_response_fc01_one_coil_off);
    RUN_TEST(test_build_coil_response_fc01_nine_coils_all_on);
    RUN_TEST(test_build_coil_response_fc02_eight_coils_zero);
    RUN_TEST(test_build_coil_response_fc01_alternating_bits);

    RUN_TEST(test_build_register_response_not_found);
    RUN_TEST(test_build_register_response_second_not_found);
    RUN_TEST(test_build_register_response_stale);
    RUN_TEST(test_build_coil_response_not_found);
    RUN_TEST(test_build_coil_response_stale);

    RUN_TEST(test_register_response_mbap_length_field);
    RUN_TEST(test_coil_response_mbap_length_field);

    RUN_TEST(test_register_response_timeout_passed_to_lookup);
    RUN_TEST(test_coil_response_timeout_passed_to_lookup);
    RUN_TEST(test_register_response_lookup_args);
    RUN_TEST(test_register_response_lookup_address_increments);

    return UNITY_END();
}
