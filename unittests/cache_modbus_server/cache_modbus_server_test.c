#include "unity.h"
#include "console_log.h"

#include "cache_multimaster.h"
#include "modbus_helpers.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>

#include "cache_modbus_server_internal.h"

/* Test shim: exposes the static process_data_from_tcp() for unit tests */
void cache_modbus_server_test_process(tcp_desc_t *desc, int client_sock,
                                       uint8_t *data, size_t len);

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

/* ---- Mock state exposed by mocks/mb_device.c (self-unit 0xFF handler) ----- */

extern int mock_mb_device_handle_called;

void mock_mb_device_set_response(const uint8_t *buf, size_t len);
void mock_mb_device_reset(void);

/* ---- Forward declaration for build_request() helper ---------------------- */

static void build_request(uint8_t *buf, uint16_t txid, uint8_t unit_id, uint8_t fc,
                           uint16_t start, uint16_t count);

/* ---- Modbus constants (duplicated from cache_modbus_server.c) ------------ */

#define MB_FC_READ_COILS            0x01u
#define MB_FC_READ_DISCRETE_INPUTS  0x02u
#define MB_FC_READ_HOLDING_REGS     0x03u
#define MB_FC_READ_INPUT_REGS       0x04u

#define MB_EX_ILLEGAL_ADDRESS    0x02u
#define MB_EX_ILLEGAL_DATA_VALUE 0x03u
#define MB_EX_GW_TARGET_FAILED   0x0Bu

/* ---- setUp / tearDown ---------------------------------------------------- */

void setUp(void)
{
    cache_modbus_server_test_reset();   /* reinit reassembly table */
    mock_cache_multimaster_reset();
    mock_tcp_server_reset();
    mock_setting_items_reset();
    mock_mb_device_reset();
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

/* ---- CMS-U-005c: coil lookup returns NOT_FOUND -------------------------- */

/* Verify that a NOT_FOUND lookup on a coil causes the builder to return 0 with
 * exception_code set to 0x02 (MB_EX_ILLEGAL_ADDRESS). */
void test_build_coil_response_not_found(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-005c: coil NOT_FOUND → exception 0x02");
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

/* ---- CMS-U-005d: partial multi-register block yields no frame ------------ */

/* served-data-3 / cache-lookup-2: a SCADA block read (FC03 start=100 count=11)
 * that spans beyond the polled sub-range — registers 100..108 cached, 109 not —
 * cannot be answered with a partial Modbus frame (the protocol fixes the
 * response to exactly `count` registers). The builder must therefore fail the
 * WHOLE block with exception 0x02 and emit NO bytes — no partial/leaked valid
 * data. It must also stop at the first missing register (not scan the rest). */
void test_build_register_response_partial_block_no_leak(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-005d: partial block (9 found + gap) -> 0x02, no partial frame");
    LOG_MESSAGE();

    /* 100..108 FOUND, 109 NOT_FOUND (index 9 of the 11-register block) */
    mock_lookup_arr_count = 10;
    for (int i = 0; i < 9; i++) {
        mock_lookup_results[i]    = CACHE_LOOKUP_FOUND;
        mock_lookup_values_arr[i] = (uint16_t)(0x1000 + i);
    }
    mock_lookup_results[9]    = CACHE_LOOKUP_NOT_FOUND;
    mock_lookup_values_arr[9] = 0;

    uint8_t resp_buf[512];
    memset(resp_buf, 0xEE, sizeof(resp_buf));
    uint8_t exception_code = 0xFF;

    size_t len = cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(7), 100, 11, 0, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, len,
        "incomplete block must yield no frame — valid registers must NOT leak as a partial response");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_ILLEGAL_ADDRESS, exception_code,
        "exception 0x02 on the first missing register of the block");
    TEST_ASSERT_EQUAL_INT_MESSAGE(10, mock_lookup_call_count,
        "builder must stop at the first missing register (10th lookup), not scan the whole block");
}

/* ---- CMS-U-006d: single stale register in the middle fails the block ----- */

/* Symmetric to CMS-U-005d: one stale register mid-block makes the whole FC03
 * block fail with 0x0B (GW target failed). Documents the "blink" behavior where
 * a block alternates between data and 0x0B as one register ages past timeout. */
void test_build_register_response_mid_block_stale(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-006d: mid-block STALE -> 0x0B for whole block");
    LOG_MESSAGE();

    mock_lookup_arr_count = 4;
    mock_lookup_results[0] = CACHE_LOOKUP_FOUND; mock_lookup_values_arr[0] = 0x1111;
    mock_lookup_results[1] = CACHE_LOOKUP_FOUND; mock_lookup_values_arr[1] = 0x2222;
    mock_lookup_results[2] = CACHE_LOOKUP_STALE; mock_lookup_values_arr[2] = 0x3333;
    mock_lookup_results[3] = CACHE_LOOKUP_FOUND; mock_lookup_values_arr[3] = 0x4444;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0xFF;

    size_t len = cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(7), 100, 4, 5, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, len, "a mid-block stale register fails the whole block");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_GW_TARGET_FAILED, exception_code,
        "exception 0x0B when a register in the block is stale");
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, mock_lookup_call_count,
        "builder stops at the stale register (3rd lookup)");
}

/* ---- CMS-U-005e: builder is self-safe against out-of-range count --------- */

/* Defensive guard: the builders are public and their resp_buf/byte_count
 * contract only holds for a protocol-legal count. count==0 or count beyond the
 * Modbus per-request maximum (125 regs / 2000 coils) must yield exception 0x03
 * (ILLEGAL_DATA_VALUE) and no frame, independent of the caller's own check. */
void test_build_response_count_guard(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-005e: builder count guard (0 / over-max) -> 0x03");
    LOG_MESSAGE();

    uint8_t resp_buf[512];
    uint8_t ex;

    /* registers: count == 0 */
    ex = 0xFF;
    TEST_ASSERT_EQUAL_size_t(0, cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(1), 0, 0, 0, resp_buf, &ex));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_ILLEGAL_DATA_VALUE, ex, "reg count==0 -> 0x03");

    /* registers: count == 126 (> 125) — would overflow uint8_t byte_count */
    ex = 0xFF;
    TEST_ASSERT_EQUAL_size_t(0, cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(1), 0, 126, 0, resp_buf, &ex));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_ILLEGAL_DATA_VALUE, ex, "reg count>125 -> 0x03");

    /* coils: count == 0 */
    ex = 0xFF;
    TEST_ASSERT_EQUAL_size_t(0, cache_modbus_server_build_coil_response(
        1, MB_FC_READ_COILS, htons(1), 0, 0, 0, resp_buf, &ex));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_ILLEGAL_DATA_VALUE, ex, "coil count==0 -> 0x03");

    /* coils: count == 2001 (> 2000) */
    ex = 0xFF;
    TEST_ASSERT_EQUAL_size_t(0, cache_modbus_server_build_coil_response(
        1, MB_FC_READ_COILS, htons(1), 0, 2001, 0, resp_buf, &ex));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_ILLEGAL_DATA_VALUE, ex, "coil count>2000 -> 0x03");
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

/* ---- CMS-U-007b: cache disabled → exception 0x02 for any FC -------------- */

/* Verify that when cache_multimaster_is_enabled() returns false, all supported
 * function codes return exception 0x02 (ILLEGAL_ADDRESS). */
void test_cache_modbus_server_cache_disabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-007b: cache disabled → exception 0x02 for any FC");
    LOG_MESSAGE();

    mock_cache_enabled = false;

    uint8_t buf[12];
    uint8_t fcs[] = { MB_FC_READ_HOLDING_REGS, MB_FC_READ_INPUT_REGS,
                      MB_FC_READ_COILS, MB_FC_READ_DISCRETE_INPUTS };

    for (size_t i = 0; i < sizeof(fcs) / sizeof(fcs[0]); i++) {
        build_request(buf, 0x0001, 1, fcs[i], 0, 1);
        cache_modbus_server_test_process(NULL, 1, buf, 12);

        TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
            "cache disabled: send must be called once");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(fcs[i] | 0x80u), mock_tcp_send_buf[7],
            "cache disabled: exception FC must be FC|0x80");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x02u, mock_tcp_send_buf[8],
            "cache disabled: exception code must be 0x02 (ILLEGAL_ADDRESS)");

        mock_tcp_server_reset();
        /* keep mock_cache_enabled = false across iterations */
    }
}

/* ---- CMS-U-008: invalid MBAP framing → no response sent ------------------ */

/* Verify that invalid MBAP framing (wrong protocol_id or length mismatch)
 * causes process_data_from_tcp() to silently drop the request without sending
 * any response. */
void test_cache_modbus_server_invalid_mbap_framing(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-008: invalid MBAP framing → no response");
    LOG_MESSAGE();

    uint8_t buf[12];

    /* Sub-test A: protocol_id != 0 → no response */
    build_request(buf, 0x0001, 1, MB_FC_READ_HOLDING_REGS, 0, 1);
    buf[2] = 0x00;
    buf[3] = 0x01; /* protocol_id = 1 (invalid; must be 0) */
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_tcp_send_called,
        "invalid protocol_id: no response must be sent");

    mock_tcp_server_reset();

    /* Sub-test B: MBAP length field mismatches actual packet length → no response.
     * Build a 12-byte packet but set length=7 → req_packet_len = 7+6 = 13 ≠ 12. */
    build_request(buf, 0x0002, 1, MB_FC_READ_HOLDING_REGS, 0, 1);
    buf[4] = 0x00;
    buf[5] = 0x07; /* length = 7, but packet is 12 bytes → 7+6=13 ≠ 12 */
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_tcp_send_called,
        "MBAP length mismatch: no response must be sent");
}

/* ---- CMS-U-009: MBAP echo in successful response ------------------------- */

/* Verify that a successful FC03 response echoes transaction_id and unit_id,
 * sets protocol_id = 0x0000, and leaves FC without the 0x80 exception flag. */
void test_cache_modbus_server_mbap_echo_in_success_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-009: MBAP echo in success response");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0x5678;

    uint8_t buf[12];
    /* Use non-trivial transaction_id 0xA1B2 and unit_id 0x12 */
    build_request(buf, 0xA1B2, 0x12, MB_FC_READ_HOLDING_REGS, 0, 1);
    cache_modbus_server_test_process(NULL, 1, buf, 12);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "MBAP echo: send must be called once for successful response");

    /* transaction_id echoed (network byte order: 0xA1B2 → bytes 0xA1, 0xB2) */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xA1u, mock_tcp_send_buf[0],
        "transaction_id hi must be echoed as 0xA1");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xB2u, mock_tcp_send_buf[1],
        "transaction_id lo must be echoed as 0xB2");

    /* protocol_id must be 0x0000 */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, mock_tcp_send_buf[2],
        "protocol_id hi must be 0");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, mock_tcp_send_buf[3],
        "protocol_id lo must be 0");

    /* unit_id echoed */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x12u, mock_tcp_send_buf[6],
        "unit_id must be echoed as 0x12");

    /* FC in normal response must NOT have 0x80 set */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_FC_READ_HOLDING_REGS, mock_tcp_send_buf[7],
        "FC in success response must be 0x03 without 0x80 exception flag");
}

/* ---- CMS-U-012: address overflow at uint16 boundary ---------------------- */

/* Verify that start_addr=0xFFFF and start_addr=0xFFFE with count=2 do not
 * cause uint16 overflow in the address passed to cache_multimaster_lookup(). */
void test_cache_modbus_server_address_boundary(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-012: address boundary 0xFFFF no overflow");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0xDEAD;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0;

    /* Sub-test A: start_addr=0xFFFF, count=1 → lookup called with address 0xFFFF */
    cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(1), 0xFFFF, 1, 0, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xFFFF, mock_lookup_last_address,
        "start_addr=0xFFFF, count=1: lookup address must be 0xFFFF");

    /* Sub-test B: start_addr=0xFFFE, count=2 → lookups at 0xFFFE then 0xFFFF */
    mock_lookup_arr_count     = 2;
    mock_lookup_results[0]    = CACHE_LOOKUP_FOUND;
    mock_lookup_values_arr[0] = 0x0001;
    mock_lookup_results[1]    = CACHE_LOOKUP_FOUND;
    mock_lookup_values_arr[1] = 0x0002;

    exception_code = 0;
    cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(2), 0xFFFE, 2, 0, resp_buf, &exception_code);

    /* Last lookup must be for 0xFFFF (= 0xFFFE + 1), not 0x0000 (overflow) */
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xFFFF, mock_lookup_last_address,
        "start_addr=0xFFFE, count=2: second lookup address must be 0xFFFF (no overflow)");

    /* Sub-test C: start_addr=0xFFFF, count=2 → overflow detected → exception 0x02, no lookup */
    mock_cache_multimaster_reset();
    exception_code = 0xFF;
    size_t len_c = cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(3), 0xFFFF, 2, 0, resp_buf, &exception_code);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, len_c,
        "start_addr=0xFFFF, count=2: overflow detected, builder must return 0");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_ILLEGAL_ADDRESS, exception_code,
        "start_addr=0xFFFF, count=2: exception_code_out must be set to MB_EX_ILLEGAL_ADDRESS (0x02)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_lookup_call_count,
        "start_addr=0xFFFF, count=2: no lookup must be performed when overflow detected");

    /* Sub-test D: build_coil_response start_addr=0xFFFF, count=2 → overflow → exception 0x02 */
    mock_cache_multimaster_reset();
    exception_code = 0xFF;
    size_t len_d = cache_modbus_server_build_coil_response(
        1, MB_FC_READ_COILS, htons(4), 0xFFFF, 2, 0, resp_buf, &exception_code);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, len_d,
        "coil: start_addr=0xFFFF, count=2: overflow detected, builder must return 0");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_ILLEGAL_ADDRESS, exception_code,
        "coil: start_addr=0xFFFF, count=2: exception_code_out must be MB_EX_ILLEGAL_ADDRESS (0x02)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_lookup_call_count,
        "coil: start_addr=0xFFFF, count=2: no lookup must be performed when overflow detected");
}

/* ---- Helper: build a standard 12-byte Modbus TCP request packet ---------- */

static void build_request(uint8_t *buf, uint16_t txid, uint8_t unit_id, uint8_t fc,
                           uint16_t start, uint16_t count)
{
    buf[0]  = (uint8_t)(txid >> 8);
    buf[1]  = (uint8_t)(txid & 0xFF);
    buf[2]  = 0x00;  /* protocol_id hi */
    buf[3]  = 0x00;  /* protocol_id lo */
    buf[4]  = 0x00;  /* length hi */
    buf[5]  = 0x06;  /* length lo = 6 */
    buf[6]  = unit_id;
    buf[7]  = fc;
    buf[8]  = (uint8_t)(start >> 8);
    buf[9]  = (uint8_t)(start & 0xFF);
    buf[10] = (uint8_t)(count >> 8);
    buf[11] = (uint8_t)(count & 0xFF);
}

/* ---- CMS-U-003: count validation (FC03 and FC01) ------------------------- */

/* Verify that count=0 and out-of-range counts trigger exception 0x03
 * (ILLEGAL_DATA_VALUE), while boundary-valid counts are accepted. */
void test_cache_modbus_server_count_validation(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-003: count validation for FC03 and FC01");
    LOG_MESSAGE();

    uint8_t buf[12];

    /* Sub-test: FC03 count=0 → exception 0x03 */
    build_request(buf, 0x0001, 1, MB_FC_READ_HOLDING_REGS, 0, 0);
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC03 count=0: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(MB_FC_READ_HOLDING_REGS | 0x80u),
        mock_tcp_send_buf[7], "FC03 count=0: exception FC must be FC|0x80");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x03u, mock_tcp_send_buf[8],
        "FC03 count=0: exception code must be 0x03 (ILLEGAL_DATA_VALUE)");

    mock_cache_multimaster_reset();
    mock_tcp_server_reset();

    /* Sub-test: FC03 count=126 (> MB_MAX_REGISTERS=125) → exception 0x03 */
    build_request(buf, 0x0002, 1, MB_FC_READ_HOLDING_REGS, 0, 126);
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC03 count=126: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(MB_FC_READ_HOLDING_REGS | 0x80u),
        mock_tcp_send_buf[7], "FC03 count=126: exception FC must be FC|0x80");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x03u, mock_tcp_send_buf[8],
        "FC03 count=126: exception code must be 0x03 (ILLEGAL_DATA_VALUE)");

    mock_cache_multimaster_reset();
    mock_tcp_server_reset();

    /* Sub-test: FC03 count=125 (== MB_MAX_REGISTERS, valid) → success response */
    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0x1234;
    build_request(buf, 0x0003, 1, MB_FC_READ_HOLDING_REGS, 0, 125);
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC03 count=125: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_FC_READ_HOLDING_REGS, mock_tcp_send_buf[7],
        "FC03 count=125: no exception, FC must be 0x03 without 0x80");
    /* Response length = 8 (MBAP) + 1 (byte_count field) + 125*2 = 259 bytes */
    TEST_ASSERT_EQUAL_size_t(259u, mock_tcp_send_len);
    /* byte_count field = 125*2 = 250 */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(250u, mock_tcp_send_buf[8],
        "FC03 count=125: byte_count field must be 250");

    mock_cache_multimaster_reset();
    mock_tcp_server_reset();

    /* Sub-test: FC01 count=0 → exception 0x03 */
    build_request(buf, 0x0004, 1, MB_FC_READ_COILS, 0, 0);
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC01 count=0: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(MB_FC_READ_COILS | 0x80u),
        mock_tcp_send_buf[7], "FC01 count=0: exception FC must be FC|0x80");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x03u, mock_tcp_send_buf[8],
        "FC01 count=0: exception code must be 0x03 (ILLEGAL_DATA_VALUE)");

    mock_cache_multimaster_reset();
    mock_tcp_server_reset();

    /* Sub-test: FC01 count=2001 (> MB_MAX_COILS=2000) → exception 0x03 */
    build_request(buf, 0x0005, 1, MB_FC_READ_COILS, 0, 2001);
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC01 count=2001: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(MB_FC_READ_COILS | 0x80u),
        mock_tcp_send_buf[7], "FC01 count=2001: exception FC must be FC|0x80");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x03u, mock_tcp_send_buf[8],
        "FC01 count=2001: exception code must be 0x03 (ILLEGAL_DATA_VALUE)");

    mock_cache_multimaster_reset();
    mock_tcp_server_reset();

    /* Sub-test: FC01 count=2000 (== MB_MAX_COILS, valid) → success response */
    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0;
    build_request(buf, 0x0006, 1, MB_FC_READ_COILS, 0, 2000);
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC01 count=2000: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_FC_READ_COILS, mock_tcp_send_buf[7],
        "FC01 count=2000: no exception, FC must be 0x01 without 0x80");
    /* Response length = 8 (MBAP) + 1 (coil_bytes field) + ceil(2000/8)=250 = 259 */
    TEST_ASSERT_EQUAL_size_t(259u, mock_tcp_send_len);
    /* coil_bytes field = ceil(2000/8) = 250 */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(250u, mock_tcp_send_buf[8],
        "FC01 count=2000: coil_bytes field must be 250");
}

/* ---- CMS-U-004: unsupported FC codes → exception 0x01 ------------------- */

/* Verify that unsupported function codes result in exception 0x01
 * (ILLEGAL_FUNCTION). */
void test_cache_modbus_server_unsupported_fc(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-004: unsupported FC codes → exception 0x01");
    LOG_MESSAGE();

    uint8_t buf[12];
    uint8_t unsupported_fcs[] = { 0x05, 0x06, 0x00, 0xFF };

    for (size_t i = 0; i < sizeof(unsupported_fcs) / sizeof(unsupported_fcs[0]); i++) {
        uint8_t fc = unsupported_fcs[i];
        build_request(buf, 0x0001, 1, fc, 0, 1);
        cache_modbus_server_test_process(NULL, 1, buf, 12);

        TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
            "unsupported FC: send must be called once");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(fc | 0x80u), mock_tcp_send_buf[7],
            "unsupported FC: exception FC must be FC|0x80");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x01u, mock_tcp_send_buf[8],
            "unsupported FC: exception code must be 0x01 (ILLEGAL_FUNCTION)");

        mock_tcp_server_reset();
    }
}

/* ---- CMS-U-007: short and truncated packets are handled without crash ----- */

/* Verify that short/malformed packets are safely rejected. */
void test_cache_modbus_server_short_null_packets(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-007: short/truncated packets handled safely");
    LOG_MESSAGE();

    /* Sub-test A: packet < sizeof(mb_tcp_header_t)=8 bytes — no response sent */
    uint8_t short_buf[7] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x01 };
    cache_modbus_server_test_process(NULL, 1, short_buf, 7);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_tcp_send_called,
        "packet < 8 bytes: no response must be sent");

    mock_tcp_server_reset();

    /* Sub-test B: valid MBAP framing but PDU too short (only 1 byte after header).
     * Packet: [0x00,0x01, 0x00,0x00, 0x00,0x03, 0x01, 0x03, 0x00] (9 bytes)
     *   - length field = 3 → req_packet_len = 3+6 = 9 = len ✓ (framing OK)
     *   - fc = 0x03 (supported)
     *   - but len=9 < sizeof(mb_tcp_header_t)+4=12 → exception 0x01 */
    uint8_t pdu_short_buf[9] = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x01, 0x03, 0x00 };
    cache_modbus_server_test_process(NULL, 1, pdu_short_buf, 9);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "PDU-too-short packet: send must be called once with exception");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x01u, mock_tcp_send_buf[8],
        "PDU-too-short packet: exception code must be 0x01 (ILLEGAL_FUNCTION)");
}

/* ---- CMS-U-011: exception response format ------------------------------- */

/* Verify that the exception response format is correct:
 * FC|0x80 set, MBAP length=htons(3), exception byte at position 8,
 * transaction_id and unit_id echoed. */
void test_cache_modbus_server_exception_format(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "CMS-U-011: exception response format");
    LOG_MESSAGE();

    /* NOT_FOUND → exception 0x02 (ILLEGAL_ADDRESS) */
    mock_lookup_result = CACHE_LOOKUP_NOT_FOUND;

    uint8_t buf[12];
    build_request(buf, 0xABCD, 0x07, MB_FC_READ_HOLDING_REGS, 0, 1);
    cache_modbus_server_test_process(NULL, 42, buf, 12);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "exception format: send must be called once");
    /* Exception response: sizeof(mb_tcp_header_t)=8 + 1 exception byte = 9 */
    TEST_ASSERT_EQUAL_size_t(9u, mock_tcp_send_len);

    /* MBAP header fields */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xABu, mock_tcp_send_buf[0],
        "transaction_id hi must be echoed as 0xAB");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xCDu, mock_tcp_send_buf[1],
        "transaction_id lo must be echoed as 0xCD");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, mock_tcp_send_buf[2],
        "protocol_id hi must be 0");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, mock_tcp_send_buf[3],
        "protocol_id lo must be 0");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, mock_tcp_send_buf[4],
        "MBAP length hi must be 0");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x03u, mock_tcp_send_buf[5],
        "MBAP length lo must be 3");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x07u, mock_tcp_send_buf[6],
        "unit_id must be echoed as 0x07");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(MB_FC_READ_HOLDING_REGS | 0x80u),
        mock_tcp_send_buf[7], "exception FC must be FC|0x80");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_ILLEGAL_ADDRESS, mock_tcp_send_buf[8],
        "exception code must be 0x02 (ILLEGAL_ADDRESS) for NOT_FOUND");
}

/* ---- CMS-U-027: build_register_response — count=125 (max registers) ----- */

/* Verify that count=125 (MB_MAX_REGISTERS) produces resp_len=259 and exactly
 * 125 lookup calls; catches off-by-one in the loop upper bound and uint8_t
 * overflow for byte_count (125×2=250 still fits in uint8_t). */
void test_build_register_response_max_count(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-027: build_register_response count=125 (max registers)");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0xABCD;

    uint8_t  resp_buf[512];
    uint8_t  exception_code = 0;

    size_t len = cache_modbus_server_build_register_response(
        /*unit_id=*/1, /*fc=*/MB_FC_READ_HOLDING_REGS, /*transaction_id=*/htons(1),
        /*start_addr=*/0, /*count=*/125, /*timeout=*/0,
        resp_buf, &exception_code);

    /* Total: 8 (MBAP) + 1 (byte_count field) + 125×2 = 259 */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(259u, len,
        "count=125: total response length must be 259");

    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(250u, payload[0],
        "byte_count field must be 250 (125 registers × 2 bytes)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(125, mock_lookup_call_count,
        "lookup must be called exactly 125 times for count=125");
}

/* ---- CMS-U-028: build_coil_response — 16 coils, first 8 ON / last 8 OFF - */

/* Verify that coils 0..7=ON, 8..15=OFF produces two bytes: 0xFF, 0x00.
 * Catches byte boundary error in bit-packing loop. */
void test_build_coil_response_16_coils_first_on_second_off(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-028: build_coil_response 16 coils, first 8 ON / second 8 OFF");
    LOG_MESSAGE();

    /* Array mode: coils 0..7 ON, coils 8..15 OFF */
    mock_lookup_arr_count = 16;
    for (int i = 0; i < 8; i++) {
        mock_lookup_results[i]    = CACHE_LOOKUP_FOUND;
        mock_lookup_values_arr[i] = 1;
    }
    for (int i = 8; i < 16; i++) {
        mock_lookup_results[i]    = CACHE_LOOKUP_FOUND;
        mock_lookup_values_arr[i] = 0;
    }

    uint8_t resp_buf[512];
    uint8_t exception_code = 0;

    size_t len = cache_modbus_server_build_coil_response(
        /*unit_id=*/1, /*fc=*/MB_FC_READ_COILS, /*transaction_id=*/htons(1),
        /*start_addr=*/0, /*count=*/16, /*timeout=*/0,
        resp_buf, &exception_code);

    /* Total: 8 (MBAP) + 1 (coil_bytes field) + 2 (coil bytes for 16 coils) = 11 */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(11u, len,
        "16 coils: total length must be 11");

    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2u, payload[0],
        "coil_bytes must be 2 for 16 coils");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFFu, payload[1],
        "coils 0..7 all ON: first coil byte must be 0xFF");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, payload[2],
        "coils 8..15 all OFF: second coil byte must be 0x00");
}

/* ---- CMS-U-029: build_coil_response — 16 coils, first 8 OFF / last 8 ON - */

/* Verify that coils 0..7=OFF, 8..15=ON produces bytes 0x00, 0xFF.
 * Catches bit index error (i%8) placing bits in wrong byte position. */
void test_build_coil_response_16_coils_first_off_second_on(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-029: build_coil_response 16 coils, first 8 OFF / second 8 ON");
    LOG_MESSAGE();

    /* Array mode: coils 0..7 OFF, coils 8..15 ON */
    mock_lookup_arr_count = 16;
    for (int i = 0; i < 8; i++) {
        mock_lookup_results[i]    = CACHE_LOOKUP_FOUND;
        mock_lookup_values_arr[i] = 0;
    }
    for (int i = 8; i < 16; i++) {
        mock_lookup_results[i]    = CACHE_LOOKUP_FOUND;
        mock_lookup_values_arr[i] = 1;
    }

    uint8_t resp_buf[512];
    uint8_t exception_code = 0;

    size_t len = cache_modbus_server_build_coil_response(
        1, MB_FC_READ_COILS, htons(1), 0, 16, 0, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_size_t_MESSAGE(11u, len,
        "16 coils: total length must be 11");

    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2u, payload[0],
        "coil_bytes must be 2 for 16 coils");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, payload[1],
        "coils 0..7 all OFF: first coil byte must be 0x00");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFFu, payload[2],
        "coils 8..15 all ON: second coil byte must be 0xFF");
}

/* ---- CMS-U-030: build_coil_response — partial last byte (count=15 all ON) */

/* Verify that count=15 all ON coils produce coil_bytes=2, byte[0]=0xFF,
 * byte[1]=0x7F (7 low bits set; bit 7 of last byte must NOT be set).
 * Catches surplus high bits set in partial last byte. */
void test_build_coil_response_partial_last_byte(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-030: build_coil_response count=15 all ON, partial last byte");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 1; /* all coils ON */

    uint8_t resp_buf[512];
    uint8_t exception_code = 0;

    size_t len = cache_modbus_server_build_coil_response(
        1, MB_FC_READ_COILS, htons(1), 0, 15, 0, resp_buf, &exception_code);

    /* Total: 8 + 1 (coil_bytes field) + 2 (ceil(15/8)=2 bytes) = 11 */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(11u, len,
        "count=15: total length must be 11");

    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2u, payload[0],
        "coil_bytes must be 2 for count=15");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFFu, payload[1],
        "coils 0..7 all ON: first byte must be 0xFF");
    /* Coils 8..14 set (7 bits) → 0x7F; bit 7 must NOT be set (coil 15 doesn't exist) */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x7Fu, payload[2],
        "coils 8..14 ON (7 bits): second byte must be 0x7F, bit7 must NOT be set");
}

/* ---- CMS-U-031: build_coil_response — STALE on second coil → exception --- */

/* Verify that a 2-coil request where coil[0]=FOUND and coil[1]=STALE returns 0
 * with exception_code=0x0B (MB_EX_GW_TARGET_FAILED). */
void test_build_coil_response_stale_on_second_coil(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-031: build_coil_response STALE on second coil → exception 0x0B");
    LOG_MESSAGE();

    /* Array mode: first coil FOUND, second coil STALE */
    mock_lookup_arr_count     = 2;
    mock_lookup_results[0]    = CACHE_LOOKUP_FOUND;
    mock_lookup_values_arr[0] = 1;
    mock_lookup_results[1]    = CACHE_LOOKUP_STALE;
    mock_lookup_values_arr[1] = 0;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0xFF; /* sentinel */

    size_t len = cache_modbus_server_build_coil_response(
        1, MB_FC_READ_COILS, htons(1), 0, 2, 0, resp_buf, &exception_code);

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, len,
        "STALE on second coil: builder must return 0");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_GW_TARGET_FAILED, exception_code,
        "STALE on second coil: exception_code must be 0x0B (GW_TARGET_FAILED)");
}

/* ---- CMS-U-032: build_register_response — zero value is NOT treated as failure */

/* Verify that a successful lookup returning value=0x0000 produces a valid
 * response (len > 0) with both data bytes set to 0x00.
 * Catches misuse of value==0 as a sentinel for lookup failure. */
void test_build_register_response_zero_value(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-032: build_register_response value=0x0000 is valid response");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0x0000;

    uint8_t resp_buf[512];
    uint8_t exception_code = 0xFF; /* sentinel */

    size_t len = cache_modbus_server_build_register_response(
        1, MB_FC_READ_HOLDING_REGS, htons(1), 0, 1, 0, resp_buf, &exception_code);

    TEST_ASSERT_MESSAGE(len > 0u, "value=0x0000: builder must return non-zero length");

    uint8_t *payload = resp_buf + sizeof(mb_tcp_header_t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, payload[1],
        "value=0x0000: register high byte must be 0x00");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, payload[2],
        "value=0x0000: register low byte must be 0x00");
}

/* ---- CMS-U-033: FC04 count validation (same limits as FC03) --------------- */

/* Verify that FC04 applies the same count limits as FC03:
 * count=0 and count=126 → exception 0x03; count=125 → success. */
void test_cache_modbus_server_fc04_count_validation(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-033: FC04 count validation (count=0 and count=126 → 0x03; count=125 → success)");
    LOG_MESSAGE();

    uint8_t buf[12];

    /* Sub-test A: FC04 count=0 → exception 0x03 */
    build_request(buf, 0x0001, 1, MB_FC_READ_INPUT_REGS, 0, 0);
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC04 count=0: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(MB_FC_READ_INPUT_REGS | 0x80u),
        mock_tcp_send_buf[7], "FC04 count=0: exception FC must be FC|0x80");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x03u, mock_tcp_send_buf[8],
        "FC04 count=0: exception code must be 0x03 (ILLEGAL_DATA_VALUE)");

    mock_cache_multimaster_reset();
    mock_tcp_server_reset();

    /* Sub-test B: FC04 count=126 (> MB_MAX_REGISTERS=125) → exception 0x03 */
    build_request(buf, 0x0002, 1, MB_FC_READ_INPUT_REGS, 0, 126);
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC04 count=126: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(MB_FC_READ_INPUT_REGS | 0x80u),
        mock_tcp_send_buf[7], "FC04 count=126: exception FC must be FC|0x80");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x03u, mock_tcp_send_buf[8],
        "FC04 count=126: exception code must be 0x03 (ILLEGAL_DATA_VALUE)");

    mock_cache_multimaster_reset();
    mock_tcp_server_reset();

    /* Sub-test C: FC04 count=125 (valid) → success response */
    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0x1234;
    build_request(buf, 0x0003, 1, MB_FC_READ_INPUT_REGS, 0, 125);
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC04 count=125: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_FC_READ_INPUT_REGS, mock_tcp_send_buf[7],
        "FC04 count=125: no exception, FC must be 0x04 without 0x80");
    /* Response length: 8 (MBAP) + 1 (byte_count field) + 125*2 = 259 bytes */
    TEST_ASSERT_EQUAL_size_t(259u, mock_tcp_send_len);
    /* byte_count field = 125*2 = 250 */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(250u, mock_tcp_send_buf[8],
        "FC04 count=125: byte_count field must be 250");
}

/* ---- CMS-U-034: FC02 count validation (same limits as FC01) --------------- */

/* Verify that FC02 applies the same count limits as FC01:
 * count=0 and count=2001 → exception 0x03; count=2000 → success. */
void test_cache_modbus_server_fc02_count_validation(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-034: FC02 count validation (count=0 and count=2001 → 0x03; count=2000 → success)");
    LOG_MESSAGE();

    uint8_t buf[12];

    /* Sub-test A: FC02 count=0 → exception 0x03 */
    build_request(buf, 0x0001, 1, MB_FC_READ_DISCRETE_INPUTS, 0, 0);
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC02 count=0: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(MB_FC_READ_DISCRETE_INPUTS | 0x80u),
        mock_tcp_send_buf[7], "FC02 count=0: exception FC must be FC|0x80");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x03u, mock_tcp_send_buf[8],
        "FC02 count=0: exception code must be 0x03 (ILLEGAL_DATA_VALUE)");

    mock_cache_multimaster_reset();
    mock_tcp_server_reset();

    /* Sub-test B: FC02 count=2001 (> MB_MAX_COILS=2000) → exception 0x03 */
    build_request(buf, 0x0002, 1, MB_FC_READ_DISCRETE_INPUTS, 0, 2001);
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC02 count=2001: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(MB_FC_READ_DISCRETE_INPUTS | 0x80u),
        mock_tcp_send_buf[7], "FC02 count=2001: exception FC must be FC|0x80");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x03u, mock_tcp_send_buf[8],
        "FC02 count=2001: exception code must be 0x03 (ILLEGAL_DATA_VALUE)");

    mock_cache_multimaster_reset();
    mock_tcp_server_reset();

    /* Sub-test C: FC02 count=2000 (valid, == MB_MAX_COILS) → success response */
    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0;
    build_request(buf, 0x0003, 1, MB_FC_READ_DISCRETE_INPUTS, 0, 2000);
    cache_modbus_server_test_process(NULL, 1, buf, 12);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC02 count=2000: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_FC_READ_DISCRETE_INPUTS, mock_tcp_send_buf[7],
        "FC02 count=2000: no exception, FC must be 0x02 without 0x80");
    /* Response length = 8 (MBAP) + 1 (coil_bytes field) + ceil(2000/8)=250 = 259 */
    TEST_ASSERT_EQUAL_size_t(259u, mock_tcp_send_len);
}

/* ---- CMS-U-035: process_data_from_tcp — value_timeout_s forwarded -------- */

/* Verify that value_timeout_s read from setting_items is forwarded verbatim
 * to cache_multimaster_lookup(). */
void test_cache_modbus_server_timeout_forwarded(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-035: process_data_from_tcp value_timeout_s forwarded to lookup");
    LOG_MESSAGE();

    /* Set the timeout that setting_items_read_int() will return */
    mock_setting_items_set_timeout(30);

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0x1234;

    uint8_t buf[12];
    build_request(buf, 0x0001, 1, MB_FC_READ_HOLDING_REGS, 0, 1);
    cache_modbus_server_test_process(NULL, 1, buf, 12);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "timeout forwarded: send must be called once for success");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(30u, mock_lookup_last_timeout,
        "timeout from setting_items must be forwarded verbatim to lookup");
}

/* ---- CMS-U-036: FC01 NOT_FOUND end-to-end sends exception 0x02 ----------- */

/* Verify that a coil NOT_FOUND propagates through process_data_from_tcp to
 * send_exception with code 0x02 (MB_EX_ILLEGAL_ADDRESS). */
void test_cache_modbus_server_fc01_not_found_end_to_end(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-036: FC01 NOT_FOUND end-to-end sends exception 0x02");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_NOT_FOUND;

    uint8_t buf[12];
    build_request(buf, 0x0001, 1, MB_FC_READ_COILS, 0, 1);
    cache_modbus_server_test_process(NULL, 1, buf, 12);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC01 NOT_FOUND: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(MB_FC_READ_COILS | 0x80u),
        mock_tcp_send_buf[7],
        "FC01 NOT_FOUND: exception FC must be 0x01|0x80");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_ILLEGAL_ADDRESS, mock_tcp_send_buf[8],
        "FC01 NOT_FOUND: exception code must be 0x02 (ILLEGAL_ADDRESS)");
}

/* ---- CMS-U-037: FC02 STALE end-to-end sends exception 0x0B --------------- */

/* Verify that a coil STALE propagates through process_data_from_tcp to
 * send_exception with code 0x0B (MB_EX_GW_TARGET_FAILED). */
void test_cache_modbus_server_fc02_stale_end_to_end(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-037: FC02 STALE end-to-end sends exception 0x0B");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_STALE;

    uint8_t buf[12];
    build_request(buf, 0x0001, 1, MB_FC_READ_DISCRETE_INPUTS, 0, 1);
    cache_modbus_server_test_process(NULL, 1, buf, 12);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "FC02 STALE: send must be called once");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(MB_FC_READ_DISCRETE_INPUTS | 0x80u),
        mock_tcp_send_buf[7],
        "FC02 STALE: exception FC must be 0x02|0x80");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MB_EX_GW_TARGET_FAILED, mock_tcp_send_buf[8],
        "FC02 STALE: exception code must be 0x0B (GW_TARGET_FAILED)");
}

/* ---- CMS-U-038: split frame — two halves ---------------------------------- */

/* Verify that a 12-byte request split into two 6-byte chunks is reassembled
 * correctly: first half must not trigger a response; second half must. */
void test_reassembly_split_frame(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-038: TCP stream reassembly — split frame (two halves)");
    LOG_MESSAGE();

    uint8_t buf[12];
    build_request(buf, 0x0010, 1, MB_FC_READ_HOLDING_REGS, 0, 1);

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0x1234;
    mock_setting_items_set_timeout(0);

    /* Send only the first 6 bytes: frame is incomplete, no response yet. */
    cache_modbus_server_test_process(NULL, 10, buf, 6);
    TEST_ASSERT_EQUAL_INT(0, mock_tcp_send_called);

    /* Send the remaining 6 bytes: frame completes, response sent. */
    cache_modbus_server_test_process(NULL, 10, buf + 6, 6);
    TEST_ASSERT_EQUAL_INT(1, mock_tcp_send_called);
}

/* ---- CMS-U-039: coalesced frames — two frames in one call ----------------- */

/* Verify that two complete 12-byte requests sent in a single call are both
 * dispatched: exactly two responses must be sent. */
void test_reassembly_coalesced_frames(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-039: TCP stream reassembly — coalesced frames (two in one recv)");
    LOG_MESSAGE();

    uint8_t buf[24]; /* room for two 12-byte requests */
    build_request(buf,      0x0020, 1, MB_FC_READ_HOLDING_REGS, 0, 1);
    build_request(buf + 12, 0x0021, 1, MB_FC_READ_HOLDING_REGS, 1, 1);

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0xABCD;
    mock_setting_items_set_timeout(0);

    /* Two complete frames sent in one recv() call. */
    cache_modbus_server_test_process(NULL, 11, buf, 24);
    TEST_ASSERT_EQUAL_INT(2, mock_tcp_send_called);
}

/* ---- CMS-U-040: carry-over — 1.5 frames, second arrives later ------------- */

/* Verify that 18 bytes (frame1 + first half of frame2) dispatches only frame1,
 * and the remaining 6 bytes of frame2 dispatches frame2. */
void test_reassembly_carryover(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-040: TCP stream reassembly — 1.5 frames then remainder");
    LOG_MESSAGE();

    uint8_t buf[24];
    build_request(buf,      0x0030, 1, MB_FC_READ_HOLDING_REGS, 0, 1);
    build_request(buf + 12, 0x0031, 1, MB_FC_READ_HOLDING_REGS, 0, 1);

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0x5678;
    mock_setting_items_set_timeout(0);

    /* First 18 bytes: frame1(12) + half of frame2(6). Only frame1 dispatched. */
    cache_modbus_server_test_process(NULL, 12, buf, 18);
    TEST_ASSERT_EQUAL_INT(1, mock_tcp_send_called);

    /* Remaining 6 bytes of frame2. Now frame2 dispatched. */
    mock_tcp_server_reset();
    cache_modbus_server_test_process(NULL, 12, buf + 18, 6);
    TEST_ASSERT_EQUAL_INT(1, mock_tcp_send_called);
}

/* ---- CMS-U-041: independent reassembly buffers per socket ----------------- */

/* Verify that two different sockets each maintain their own reassembly buffer:
 * interleaved partial frames from sockets 20 and 21 must not corrupt each other. */
void test_reassembly_independent_sockets(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-041: TCP stream reassembly — independent buffers per socket");
    LOG_MESSAGE();

    uint8_t buf_a[12], buf_b[12];
    build_request(buf_a, 0x0040, 1, MB_FC_READ_HOLDING_REGS, 0, 1);
    build_request(buf_b, 0x0041, 2, MB_FC_READ_HOLDING_REGS, 0, 1);

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0x0001;
    mock_setting_items_set_timeout(0);

    /* Interleave partial frames from two sockets. */
    cache_modbus_server_test_process(NULL, 20, buf_a, 6);   /* sock 20: first half */
    cache_modbus_server_test_process(NULL, 21, buf_b, 6);   /* sock 21: first half */
    TEST_ASSERT_EQUAL_INT(0, mock_tcp_send_called);

    cache_modbus_server_test_process(NULL, 20, buf_a + 6, 6);  /* sock 20: completes */
    TEST_ASSERT_EQUAL_INT(1, mock_tcp_send_called);

    mock_tcp_server_reset();
    cache_modbus_server_test_process(NULL, 21, buf_b + 6, 6);  /* sock 21: completes */
    TEST_ASSERT_EQUAL_INT(1, mock_tcp_send_called);
}

/* ---- CMS-U-042: oversized-frame boundary (flen == CACHE_MB_FRAME_MAX+1) --- */

/* Verify the oversized-frame resync boundary. An 8-byte MBAP header that
 * declares a total frame length of exactly CACHE_MB_FRAME_MAX+1 (301) must be
 * rejected as oversized and trigger a resync (the buffer is dropped). A valid
 * 12-byte frame delivered afterwards must then be dispatched normally.
 *
 * Mutation trap: if the oversized bound is widened from "> 300" to "> 301",
 * the 301-byte declared frame is no longer dropped; its 8 garbage bytes stay
 * buffered, the following valid frame is parsed against the stale header, and
 * nothing is ever dispatched (0 sends instead of 1). */
void test_reassembly_oversized_frame_boundary(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-042: TCP stream reassembly — oversized frame (flen==301) resync boundary");
    LOG_MESSAGE();

    mock_lookup_result = CACHE_LOOKUP_FOUND;
    mock_lookup_value  = 0x4321;
    mock_setting_items_set_timeout(0);

    /* 8-byte MBAP header declaring an oversized frame:
     * frame_total_len = mbap_len + 6, so mbap_len = 295 (0x0127) -> flen = 301. */
    uint8_t oversized_hdr[8];
    oversized_hdr[0] = 0x00; oversized_hdr[1] = 0x50;  /* transaction id */
    oversized_hdr[2] = 0x00; oversized_hdr[3] = 0x00;  /* protocol id = 0 */
    oversized_hdr[4] = 0x01; oversized_hdr[5] = 0x27;  /* MBAP length = 295 -> flen = 301 */
    oversized_hdr[6] = 0x01;                            /* unit id */
    oversized_hdr[7] = MB_FC_READ_HOLDING_REGS;         /* function */

    /* Oversized header: original drops it to resync; no response. */
    cache_modbus_server_test_process(NULL, 30, oversized_hdr, sizeof(oversized_hdr));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_tcp_send_called,
        "oversized frame (flen==301) must not produce a response");

    /* A valid 12-byte frame must now be dispatched: after the resync the buffer
     * is clean, so this frame completes and a single response is sent. */
    uint8_t buf[12];
    build_request(buf, 0x0051, 1, MB_FC_READ_HOLDING_REGS, 0, 1);
    cache_modbus_server_test_process(NULL, 30, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "valid frame after oversized resync must be dispatched (mutant leaves stale buffer -> 0)");
}

/* ---- CMS-U-043: self unit (0xFF) served when cache DISABLED -------------- */

/* Load-bearing test: a request addressed to the gateway itself (Unit ID 0xFF)
 * must be served from the built-in device handler BEFORE the cache-enabled
 * gate. With the cache disabled, a unit-0xFF FC04 request must NOT produce a
 * cache exception (0x02); instead the verbatim ADU from mb_device_handle_self_
 * request() must be sent. */
void test_cache_modbus_server_self_unit_served_when_cache_disabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-043: self unit 0xFF served when cache DISABLED (before cache gate)");
    LOG_MESSAGE();

    /* Known success-shaped ADU returned by the self handler (11 bytes). */
    const uint8_t known[11] = {
        0x00, 0x01,  /* transaction id */
        0x00, 0x00,  /* protocol id    */
        0x00, 0x05,  /* length = 5     */
        0xFF,        /* unit id 0xFF   */
        0x04,        /* FC04           */
        0x02,        /* byte count     */
        0xAB, 0xCD,  /* one register   */
    };
    mock_mb_device_set_response(known, sizeof(known));

    /* Cache disabled, same as CMS-U-007b. */
    mock_cache_enabled = false;

    uint8_t buf[12];
    build_request(buf, 0x0001, 0xFF, MB_FC_READ_INPUT_REGS, 0, 1);
    cache_modbus_server_test_process(NULL, 1, buf, 12);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "self unit, cache disabled: send must be called once");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(sizeof(known), mock_tcp_send_len,
        "self unit: sent length must equal the self handler's ADU length");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, memcmp(mock_tcp_send_buf, known, sizeof(known)),
        "self unit: ADU must be sent verbatim (NOT a cache exception 0x02)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mb_device_handle_called,
        "self unit: the self-device handler must be invoked exactly once");
}

/* ---- CMS-U-044: self unit (0xFF) forwarded verbatim, bypasses cache ------ */

/* With the cache ENABLED, a unit-0xFF request must still be served by the
 * self-device handler verbatim, and must NOT consult the register cache
 * (cache_multimaster_lookup must not be called). */
void test_cache_modbus_server_self_unit_bypasses_cache_lookup(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "CMS-U-044: self unit 0xFF forwarded verbatim and bypasses cache lookup");
    LOG_MESSAGE();

    const uint8_t known[11] = {
        0x12, 0x34,  /* transaction id */
        0x00, 0x00,  /* protocol id    */
        0x00, 0x05,  /* length = 5     */
        0xFF,        /* unit id 0xFF   */
        0x03,        /* FC03           */
        0x02,        /* byte count     */
        0xBE, 0xEF,  /* one register   */
    };
    mock_mb_device_set_response(known, sizeof(known));

    /* Cache ENABLED (default after reset). */
    mock_cache_enabled = true;

    uint8_t buf[12];
    build_request(buf, 0x1234, 0xFF, MB_FC_READ_HOLDING_REGS, 0, 1);
    cache_modbus_server_test_process(NULL, 1, buf, 12);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_tcp_send_called,
        "self unit, cache enabled: send must be called once");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(sizeof(known), mock_tcp_send_len,
        "self unit: sent length must equal the self handler's ADU length");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, memcmp(mock_tcp_send_buf, known, sizeof(known)),
        "self unit: ADU must be sent verbatim");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mb_device_handle_called,
        "self unit: the self-device handler must be invoked (not the cache path)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_lookup_call_count,
        "self unit: cache lookup must NOT be consulted for unit 0xFF");
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
    RUN_TEST(test_build_register_response_partial_block_no_leak);
    RUN_TEST(test_build_register_response_mid_block_stale);
    RUN_TEST(test_build_response_count_guard);
    RUN_TEST(test_build_coil_response_not_found);
    RUN_TEST(test_build_coil_response_stale);

    RUN_TEST(test_register_response_mbap_length_field);
    RUN_TEST(test_coil_response_mbap_length_field);

    RUN_TEST(test_register_response_timeout_passed_to_lookup);
    RUN_TEST(test_coil_response_timeout_passed_to_lookup);
    RUN_TEST(test_register_response_lookup_args);
    RUN_TEST(test_register_response_lookup_address_increments);

    RUN_TEST(test_cache_modbus_server_count_validation);
    RUN_TEST(test_cache_modbus_server_unsupported_fc);
    RUN_TEST(test_cache_modbus_server_short_null_packets);
    RUN_TEST(test_cache_modbus_server_cache_disabled);
    RUN_TEST(test_cache_modbus_server_invalid_mbap_framing);
    RUN_TEST(test_cache_modbus_server_mbap_echo_in_success_response);
    RUN_TEST(test_cache_modbus_server_address_boundary);
    RUN_TEST(test_cache_modbus_server_exception_format);

    RUN_TEST(test_build_register_response_max_count);
    RUN_TEST(test_build_coil_response_16_coils_first_on_second_off);
    RUN_TEST(test_build_coil_response_16_coils_first_off_second_on);
    RUN_TEST(test_build_coil_response_partial_last_byte);
    RUN_TEST(test_build_coil_response_stale_on_second_coil);
    RUN_TEST(test_build_register_response_zero_value);
    RUN_TEST(test_cache_modbus_server_fc04_count_validation);
    RUN_TEST(test_cache_modbus_server_fc02_count_validation);
    RUN_TEST(test_cache_modbus_server_timeout_forwarded);
    RUN_TEST(test_cache_modbus_server_fc01_not_found_end_to_end);
    RUN_TEST(test_cache_modbus_server_fc02_stale_end_to_end);

    RUN_TEST(test_reassembly_split_frame);
    RUN_TEST(test_reassembly_coalesced_frames);
    RUN_TEST(test_reassembly_carryover);
    RUN_TEST(test_reassembly_independent_sockets);
    RUN_TEST(test_reassembly_oversized_frame_boundary);

    RUN_TEST(test_cache_modbus_server_self_unit_served_when_cache_disabled);
    RUN_TEST(test_cache_modbus_server_self_unit_bypasses_cache_lookup);

    return UNITY_END();
}
