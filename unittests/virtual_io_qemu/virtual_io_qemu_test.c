#include "unity.h"

/* Pull in the module under test directly so the tests can reach its static
 * helpers (encode_record/parse_record/handle_rx_record/send_full_dump_locked)
 * AND poke the static s_ctx. The mock headers under mocks/ resolve all of
 * virtual_io_qemu.c's includes (lwip/, soc/, freertos/, esp_log, ...). */
#include "virtual_io_qemu.c"

#include <string.h>

/* ── Assertion helpers ───────────────────────────────────────────────────── */

/* Count how many captured datagrams are an exact 5-byte match of rec5. Only
 * datagrams whose stored length is exactly 5 are considered. */
static int sent_count(const char *rec5)
{
    int count = 0;
    for (int i = 0; i < mock_sendto_count; i++) {
        if (mock_sendto_len[i] == 5 &&
            memcmp(mock_sendto_data[i], rec5, 5) == 0) {
            count++;
        }
    }
    return count;
}

static bool was_sent(const char *rec5)
{
    return sent_count(rec5) > 0;
}

/* ── Unity fixture ───────────────────────────────────────────────────────── */

void setUp(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));   // all pins UNCONFIGURED, exp_shadow 0, peer unknown
    mock_freertos_semaphore_reset();
    mock_lwip_reset();
    gpio_expander_init(NULL);           // creates the mutex (semphr mock returns non-NULL), resets exp_shadow
    s_ctx.peer_known = true;
    s_ctx.sock = 3;                     // enable send_record_locked
    mock_lwip_reset();                  // clear any captures from init
}

void tearDown(void)
{
}

/* ── encode_record ───────────────────────────────────────────────────────── */

static void test_encode_two_digit_and_levels(void)
{
    char buf[8];

    encode_record('E', 0, 0, buf);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("E00/0", buf, 5, "encode('E',0,0) should be E00/0");

    encode_record('E', 15, 1, buf);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("E15/1", buf, 5, "encode('E',15,1) should be E15/1");

    encode_record('G', 34, 0, buf);
    TEST_ASSERT_EQUAL_MESSAGE('3', buf[1], "encode('G',34,0) tens digit should be '3'");
    TEST_ASSERT_EQUAL_MESSAGE('4', buf[2], "encode('G',34,0) ones digit should be '4'");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("G34/0", buf, 5, "encode('G',34,0) should be G34/0");

    encode_record('E', 5, 0, buf);
    TEST_ASSERT_EQUAL_MESSAGE('0', buf[1], "encode('E',5,0) tens digit should be '0' (off-by-one guard)");
    TEST_ASSERT_EQUAL_MESSAGE('5', buf[2], "encode('E',5,0) ones digit should be '5'");
}

static void test_encode_value_semantics(void)
{
    char buf[8];

    encode_record('E', 4, 7, buf);
    TEST_ASSERT_EQUAL_MESSAGE('1', buf[4], "E collapses any non-zero value to '1'");

    encode_record('V', 4, 2, buf);
    TEST_ASSERT_EQUAL_MESSAGE('2', buf[4], "V keeps the multi-valued cause (2)");

    encode_record('V', 4, 7, buf);
    TEST_ASSERT_EQUAL_MESSAGE('7', buf[4], "V keeps the multi-valued cause (7)");
}

/* ── parse_record ────────────────────────────────────────────────────────── */

static void test_parse_valid(void)
{
    char type;
    int num, value;

    TEST_ASSERT_TRUE_MESSAGE(parse_record("E07/1", 5, &type, &num, &value), "E07/1 must parse");
    TEST_ASSERT_EQUAL_MESSAGE('E', type, "E07/1 type");
    TEST_ASSERT_EQUAL_MESSAGE(7, num, "E07/1 num");
    TEST_ASSERT_EQUAL_MESSAGE(1, value, "E07/1 value");

    TEST_ASSERT_TRUE_MESSAGE(parse_record("G34/0", 5, &type, &num, &value), "G34/0 must parse");
    TEST_ASSERT_EQUAL_MESSAGE('G', type, "G34/0 type");
    TEST_ASSERT_EQUAL_MESSAGE(34, num, "G34/0 num");
    TEST_ASSERT_EQUAL_MESSAGE(0, value, "G34/0 value");
}

static void test_parse_trailing_newline(void)
{
    char type;
    int num, value;

    TEST_ASSERT_TRUE_MESSAGE(parse_record("E07/1\n", 6, &type, &num, &value),
                             "E07/1 with trailing newline must parse");
    TEST_ASSERT_EQUAL_MESSAGE('E', type, "trailing-newline type");
    TEST_ASSERT_EQUAL_MESSAGE(7, num, "trailing-newline num");
    TEST_ASSERT_EQUAL_MESSAGE(1, value, "trailing-newline value");
}

static void test_parse_rejects_d_and_v_on_rx(void)
{
    char type;
    int num, value;

    TEST_ASSERT_FALSE_MESSAGE(parse_record("D04/1", 5, &type, &num, &value), "D is TX-only, rejected on RX");
    TEST_ASSERT_FALSE_MESSAGE(parse_record("V04/2", 5, &type, &num, &value), "V is TX-only, rejected on RX");
}

static void test_parse_rejects_level_2_for_eg(void)
{
    char type;
    int num, value;

    TEST_ASSERT_FALSE_MESSAGE(parse_record("E07/2", 5, &type, &num, &value), "E level must be 0/1");
    TEST_ASSERT_FALSE_MESSAGE(parse_record("G34/2", 5, &type, &num, &value), "G level must be 0/1");
}

static void test_parse_rejects_malformed(void)
{
    char type;
    int num, value;

    TEST_ASSERT_FALSE_MESSAGE(parse_record("EZZ/1", 5, &type, &num, &value), "non-digit number rejected");
    TEST_ASSERT_FALSE_MESSAGE(parse_record("E0/71", 5, &type, &num, &value), "separator not at index 3 rejected");
    TEST_ASSERT_FALSE_MESSAGE(parse_record("E07/", 4, &type, &num, &value), "len 4 rejected");
    TEST_ASSERT_FALSE_MESSAGE(parse_record("E07/1X", 6, &type, &num, &value), "6 bytes with idx5 != newline rejected");
    TEST_ASSERT_FALSE_MESSAGE(parse_record("E07/1XX", 7, &type, &num, &value), "len 7 rejected");
}

/* ── dispatch (handle_rx_record) ─────────────────────────────────────────── */

static void test_dispatch_expander_guard(void)
{
    /* num >= 16 must be blocked by the num<16 guard: no shadow change, no record. */
    handle_rx_record("E16/1", 5);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, s_ctx.exp_shadow, "E16 must NOT touch the expander shadow");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_sendto_count, "E16 must emit no record");

    /* num < 16 sets the bit and emits exactly one record. */
    handle_rx_record("E05/1", 5);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE((uint16_t)(1u << 5), s_ctx.exp_shadow, "E05/1 must set bit 5");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sent_count("E05/1"), "E05/1 must be emitted exactly once");
}

/* ── direction machine ───────────────────────────────────────────────────── */

static void test_dir_unconfigured_to_input_seed(void)
{
    vio_native_set_direction(34, VIO_DIR_INPUT);
    TEST_ASSERT_TRUE_MESSAGE(vio_native_is_tracked(34), "pin 34 must be tracked after INPUT config");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, vio_native_get_level(34), "INPUT seed should be idle-HIGH (1)");
    TEST_ASSERT_TRUE_MESSAGE(was_sent("D34/0"), "D34/0 (INPUT direction) must be emitted");
    TEST_ASSERT_TRUE_MESSAGE(was_sent("G34/1"), "G34/1 (seeded idle-HIGH) must be emitted");
}

static void test_dir_redundant_input_no_reset(void)
{
    vio_native_set_direction(34, VIO_DIR_INPUT);
    /* INPUT pin: host may legally drive it to 0. */
    virtual_io_native_apply_from_host(34, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, vio_native_get_level(34), "host drove INPUT pin to 0");
    TEST_ASSERT_TRUE_MESSAGE(was_sent("G34/0"), "G34/0 echo must be emitted for the host write");

    mock_lwip_reset();
    /* Redundant same-direction reconfigure must NOT re-seed the level or re-emit D. */
    vio_native_set_direction(34, VIO_DIR_INPUT);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, vio_native_get_level(34), "redundant INPUT config must NOT re-seed to 1");
    TEST_ASSERT_FALSE_MESSAGE(was_sent("D34/0"), "redundant INPUT config must NOT emit a new D34/0");
}

static void test_fw_output_legal_and_change_detect(void)
{
    vio_native_set_direction(4, VIO_DIR_OUTPUT);
    TEST_ASSERT_TRUE_MESSAGE(was_sent("D04/1"), "D04/1 (OUTPUT direction) must be emitted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, sent_count("G04/0"), "OUTPUT config must not emit a G record");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, sent_count("G04/1"), "OUTPUT config must not emit a G record");

    mock_lwip_reset();
    vio_native_fw_set_level(4, 1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sent_count("G04/1"), "firmware OUTPUT write must emit G04/1 once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, vio_native_get_level(4), "OUTPUT level should be 1");

    mock_lwip_reset();
    vio_native_fw_set_level(4, 1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, sent_count("G04/1"), "redundant write must NOT re-emit (change-detection)");
}

static void test_fw_drives_input_violation(void)
{
    vio_native_set_direction(34, VIO_DIR_INPUT); // seeds level 1
    mock_lwip_reset();
    vio_native_fw_set_level(34, 0);
    TEST_ASSERT_TRUE_MESSAGE(was_sent("V34/1"), "firmware writing INPUT must raise V34/1");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, vio_native_get_level(34), "INPUT level must be unchanged (rejected)");
}

static void test_fw_operate_unconfigured_violation(void)
{
    /* pin 5 is UNCONFIGURED after setUp. */
    vio_native_fw_set_level(5, 0);
    TEST_ASSERT_TRUE_MESSAGE(was_sent("V05/2"), "firmware operating UNCONFIGURED pin must raise V05/2");
}

static void test_host_drives_output_violation(void)
{
    vio_native_set_direction(4, VIO_DIR_OUTPUT);
    mock_lwip_reset();
    virtual_io_native_apply_from_host(4, 0);
    TEST_ASSERT_TRUE_MESSAGE(was_sent("V04/0"), "host driving OUTPUT pin must raise V04/0");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, vio_native_get_level(4), "OUTPUT level unchanged by rejected host write");
}

static void test_host_operate_unconfigured_violation(void)
{
    /* pin 6 is UNCONFIGURED after setUp. */
    virtual_io_native_apply_from_host(6, 1);
    TEST_ASSERT_TRUE_MESSAGE(was_sent("V06/2"), "host operating UNCONFIGURED pin must raise V06/2");
}

/* ── send_full_dump_locked ───────────────────────────────────────────────── */

static void test_full_dump_empty_state(void)
{
    mock_lwip_reset();
    send_full_dump_locked();

    TEST_ASSERT_EQUAL_INT_MESSAGE(16, mock_sendto_count, "empty state dump must emit exactly 16 records");
    for (int i = 0; i < mock_sendto_count; i++) {
        /* type 'E' for every record already proves zero 'D'/'G' records in the
         * empty-state dump (no native pin is configured). */
        TEST_ASSERT_EQUAL_MESSAGE('E', mock_sendto_data[i][0], "every dump record must be type 'E'");
        TEST_ASSERT_EQUAL_MESSAGE('0', mock_sendto_data[i][4], "every E record value must be '0'");
    }
}

static void test_full_dump_with_configured_pin(void)
{
    vio_native_set_direction(4, VIO_DIR_OUTPUT);
    vio_native_fw_set_level(4, 1);
    mock_lwip_reset();
    send_full_dump_locked();

    int e_count = 0;
    int dg_other = 0;
    for (int i = 0; i < mock_sendto_count; i++) {
        char t = (char)mock_sendto_data[i][0];
        if (t == 'E') {
            e_count++;
        } else if ((t == 'D' || t == 'G')) {
            /* The only legal D/G in this dump are for pin 04. */
            if (!(mock_sendto_data[i][1] == '0' && mock_sendto_data[i][2] == '4')) {
                dg_other++;
            }
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(16, e_count, "dump must contain 16 'E' records");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sent_count("D04/1"), "dump must contain exactly one D04/1");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sent_count("G04/1"), "dump must contain exactly one G04/1");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, dg_other, "no D/G for any pin other than 04");
}

/* ── expander shadow contract ────────────────────────────────────────────── */

static void test_expander_single_bit_change_detect(void)
{
    mock_lwip_reset();
    gpio_expander_set_level((1u << 7), 1);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE((uint16_t)(1u << 7), s_ctx.exp_shadow, "bit 7 must be set");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sent_count("E07/1"), "E07/1 must be emitted once");

    mock_lwip_reset();
    gpio_expander_set_level((1u << 7), 1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, sent_count("E07/1"), "redundant set must NOT re-emit (change-detection)");
}

static void test_expander_multi_bit(void)
{
    mock_lwip_reset();
    gpio_expander_set_level((1u << 0) | (1u << 1), 1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sent_count("E00/1"), "E00/1 must be emitted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sent_count("E01/1"), "E01/1 must be emitted");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE((uint16_t)((1u << 0) | (1u << 1)), s_ctx.exp_shadow, "bits 0,1 must be set");
}

static void test_expander_get_level_masked(void)
{
    gpio_expander_set_level((1u << 0) | (1u << 2), 1);
    uint32_t out = 0;
    gpio_expander_get_level((1u << 2), &out);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE((1u << 2), out, "get_level must preserve bit position and mask out bit 0");
}

/* ── wire-format contract (encoder <-> parser; depends on R1 + R2) ────────── */

/* These constants (SEP_INDEX=3, LEVEL_INDEX=4, RECORD_LEN=5) mirror
 * api_tests/io_bus_helpers.py so the firmware encoder and the Python parser
 * cannot silently drift. */
#define WIRE_SEP_INDEX    3
#define WIRE_LEVEL_INDEX  4

static void test_wire_contract_roundtrip(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, VIRTUAL_IO_RECORD_LEN, "RECORD_LEN must be 5");

    const char types[] = {'E', 'G'};
    const int nums[] = {0, 5, 15, 34, 99};
    const int values[] = {0, 1};

    for (size_t ti = 0; ti < sizeof(types) / sizeof(types[0]); ti++) {
        for (size_t ni = 0; ni < sizeof(nums) / sizeof(nums[0]); ni++) {
            for (size_t vi = 0; vi < sizeof(values) / sizeof(values[0]); vi++) {
                char buf[8];
                char type;
                int num, value;
                encode_record(types[ti], nums[ni], values[vi], buf);
                TEST_ASSERT_EQUAL_MESSAGE('/', buf[WIRE_SEP_INDEX], "separator must be at SEP_INDEX 3");
                /* LEVEL_INDEX 4 holds the level digit. */
                TEST_ASSERT_TRUE_MESSAGE((buf[WIRE_LEVEL_INDEX] == '0') || (buf[WIRE_LEVEL_INDEX] == '1'),
                                         "level digit at LEVEL_INDEX 4 must be '0'/'1' for E/G");
                TEST_ASSERT_TRUE_MESSAGE(parse_record(buf, 5, &type, &num, &value),
                                         "encoded E/G record must round-trip through parse_record");
                TEST_ASSERT_EQUAL_MESSAGE(types[ti], type, "round-trip type identity");
                TEST_ASSERT_EQUAL_MESSAGE(nums[ni], num, "round-trip num identity");
                TEST_ASSERT_EQUAL_MESSAGE(values[vi], value, "round-trip value identity");
            }
        }
    }
}

static void test_wire_contract_tx_only_layout(void)
{
    char buf[8];

    /* TX-only types are rejected by parse_record, so assert the byte layout directly. */
    encode_record('D', 4, 1, buf);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("D04/1", buf, 5, "encode('D',4,1) should be D04/1");

    encode_record('D', 15, 0, buf);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("D15/0", buf, 5, "encode('D',15,0) should be D15/0");

    encode_record('V', 4, 2, buf);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("V04/2", buf, 5, "encode('V',4,2) should be V04/2");
}

/* ── runner ──────────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_encode_two_digit_and_levels);
    RUN_TEST(test_encode_value_semantics);

    RUN_TEST(test_parse_valid);
    RUN_TEST(test_parse_trailing_newline);
    RUN_TEST(test_parse_rejects_d_and_v_on_rx);
    RUN_TEST(test_parse_rejects_level_2_for_eg);
    RUN_TEST(test_parse_rejects_malformed);

    RUN_TEST(test_dispatch_expander_guard);

    RUN_TEST(test_dir_unconfigured_to_input_seed);
    RUN_TEST(test_dir_redundant_input_no_reset);
    RUN_TEST(test_fw_output_legal_and_change_detect);
    RUN_TEST(test_fw_drives_input_violation);
    RUN_TEST(test_fw_operate_unconfigured_violation);
    RUN_TEST(test_host_drives_output_violation);
    RUN_TEST(test_host_operate_unconfigured_violation);

    RUN_TEST(test_full_dump_empty_state);
    RUN_TEST(test_full_dump_with_configured_pin);

    RUN_TEST(test_expander_single_bit_change_detect);
    RUN_TEST(test_expander_multi_bit);
    RUN_TEST(test_expander_get_level_masked);

    RUN_TEST(test_wire_contract_roundtrip);
    RUN_TEST(test_wire_contract_tx_only_layout);

    return UNITY_END();
}
