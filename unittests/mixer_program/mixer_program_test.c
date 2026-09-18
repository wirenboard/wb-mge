#include "unity.h"
#include "console_log.h"

#include "program.h"

#include <string.h>

// Base64 constants used below, with the bytes they stand for:
//   "ARQAeA=="  -> 01 14 00 78            one forward step, 200 rpm, 120 s
//   "ARQAeAAAAB4CDwA8" -> 01 14 00 78 | 00 00 00 1E | 02 0F 00 3C
//                          forward 200 rpm 120 s, pause 30 s, reverse 150 rpm 60 s
#define B64_ONE_FORWARD     "ARQAeA=="
#define B64_THREE_STEPS     "ARQAeAAAAB4CDwA8"

// Eleven steps (the ceiling) and twelve (one too many), both of the shape
// {forward, speed i, duration i}.
#define B64_ELEVEN_STEPS    "AQEAAQECAAIBAwADAQQABAEFAAUBBgAGAQcABwEIAAgBCQAJAQoACgELAAs="
#define B64_TWELVE_STEPS    "AQEAAQECAAIBAwADAQQABAEFAAUBBgAGAQcABwEIAAgBCQAJAQoACgELAAsBDAAM"

// 01 14 00 00 — a forward step of zero seconds.
#define B64_ZERO_DURATION   "ARQAAA=="
// 01 00 00 0A / 02 00 00 0A — forward and reverse at no speed at all, ten seconds each.
#define B64_ZERO_SPEED_FWD  "AQAACg=="
#define B64_ZERO_SPEED_REV  "AgAACg=="
// 03 14 00 0A — step type 3, which is not a type.
#define B64_BAD_TYPE        "AxQACg=="
// Five raw bytes: decodes cleanly, but five is not a whole number of steps.
#define B64_FIVE_BYTES      "ARQAeAI="

void setUp(void)
{

}

void tearDown(void)
{

}

// The empty setting value is a program with no steps — the factory default, and what a user
// stores to clear a program. It must not be an error.
void test_decode_empty_program(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test decoding an empty program");
    LOG_MESSAGE();

    mixer_program_t program;
    memset(&program, 0xAA, sizeof(program));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, mixer_program_decode("", &program),
                              "The empty string is a valid, empty program");
    TEST_ASSERT_EQUAL_MESSAGE(0, program.count, "An empty program has no steps");
}

// The step layout itself: type, speed and a BIG-ENDIAN duration, four bytes each.
void test_decode_step_layout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test the decoded step layout");
    LOG_MESSAGE();

    mixer_program_t program;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, mixer_program_decode(B64_ONE_FORWARD, &program),
                              "A single forward step should decode");
    TEST_ASSERT_EQUAL_MESSAGE(1, program.count, "One step was encoded");
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_STEP_FORWARD, program.steps[0].type, "Type 1 is forward");
    TEST_ASSERT_EQUAL_MESSAGE(20, program.steps[0].speed, "Speed is stored in tens of rpm");
    TEST_ASSERT_EQUAL_MESSAGE(120, program.steps[0].duration_s,
                              "0x0078 read big-endian is 120 seconds");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, mixer_program_decode(B64_THREE_STEPS, &program),
                              "Three steps should decode");
    TEST_ASSERT_EQUAL_MESSAGE(3, program.count, "Three steps were encoded");

    TEST_ASSERT_EQUAL_MESSAGE(MIXER_STEP_FORWARD, program.steps[0].type, "Step 1 is forward");
    TEST_ASSERT_EQUAL_MESSAGE(20, program.steps[0].speed, "Step 1 runs at 200 rpm");
    TEST_ASSERT_EQUAL_MESSAGE(120, program.steps[0].duration_s, "Step 1 lasts 120 s");

    TEST_ASSERT_EQUAL_MESSAGE(MIXER_STEP_PAUSE, program.steps[1].type, "Step 2 is a pause");
    TEST_ASSERT_EQUAL_MESSAGE(30, program.steps[1].duration_s, "Step 2 lasts 30 s");

    TEST_ASSERT_EQUAL_MESSAGE(MIXER_STEP_REVERSE, program.steps[2].type, "Step 3 is reverse");
    TEST_ASSERT_EQUAL_MESSAGE(15, program.steps[2].speed, "Step 3 runs at 150 rpm");
    TEST_ASSERT_EQUAL_MESSAGE(60, program.steps[2].duration_s, "Step 3 lasts 60 s");
}

// Encoding is the exact inverse of decoding, and it produces the same string the device
// stored — a program exported by GET /settings has to be importable again unchanged.
void test_encode_round_trip(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test encoding back to the stored string");
    LOG_MESSAGE();

    mixer_program_t program;
    char encoded[MIXER_PROGRAM_MAX_B64_SIZE] = {0};

    TEST_ASSERT_EQUAL(ESP_OK, mixer_program_decode(B64_THREE_STEPS, &program));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, mixer_program_encode(&program, encoded, sizeof(encoded)),
                              "A decoded program should encode again");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(B64_THREE_STEPS, encoded,
                                     "Re-encoding must reproduce the stored value");

    // An empty program encodes to the empty string, not to base64 padding.
    mixer_program_t empty = {0};
    TEST_ASSERT_EQUAL(ESP_OK, mixer_program_encode(&empty, encoded, sizeof(encoded)));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", encoded, "An empty program encodes to an empty value");
}

// The ceiling: eleven steps fit in a setting value, twelve do not, and the twelfth is
// refused rather than silently dropped.
void test_step_count_ceiling(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test the 11-step ceiling");
    LOG_MESSAGE();

    mixer_program_t program;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, mixer_program_decode(B64_ELEVEN_STEPS, &program),
                              "Eleven steps is the largest program that fits");
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_PROGRAM_MAX_STEPS, program.count, "All eleven decoded");
    TEST_ASSERT_EQUAL_MESSAGE(11, program.steps[10].duration_s, "The last step survived");

    // The encoded form of a full program must still fit a setting value (63 bytes).
    TEST_ASSERT_EQUAL_MESSAGE(60, strlen(B64_ELEVEN_STEPS),
                              "A full program is 60 base64 characters");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG,
                              mixer_program_decode(B64_TWELVE_STEPS, &program),
                              "Twelve steps do not fit and must be refused");
    TEST_ASSERT_EQUAL_MESSAGE(0, program.count, "A refused program leaves no steps behind");

    // Encoding refuses the same thing from the other side.
    mixer_program_t oversized = {0};
    oversized.count = MIXER_PROGRAM_MAX_STEPS + 1;
    char encoded[MIXER_PROGRAM_MAX_B64_SIZE] = {0};
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG,
                              mixer_program_encode(&oversized, encoded, sizeof(encoded)),
                              "More steps than the program can hold is not encodable");
}

// Everything that is not a program: broken base64, a length that is not a whole number of
// steps, a step type that does not exist, and a step of no duration.
void test_decode_rejects_malformed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test decoding malformed programs");
    LOG_MESSAGE();

    mixer_program_t program;

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, mixer_program_decode("!!!!", &program),
                              "Characters outside the base64 alphabet are not a program");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, mixer_program_decode("ARQAeA=", &program),
                              "Truncated base64 padding is not a program");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, mixer_program_decode(B64_FIVE_BYTES, &program),
                              "Five bytes is not a whole number of four-byte steps");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, mixer_program_decode(B64_BAD_TYPE, &program),
                              "Step type 3 does not exist");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG,
                              mixer_program_decode(B64_ZERO_DURATION, &program),
                              "A step of zero seconds is a mistake, not a step");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, mixer_program_decode(NULL, &program),
                              "A NULL value is not a program");

    // A step that turns the shaft at no speed is not a way to stand still: the engine would
    // raise it to the drive's 1.0 Hz floor and the motor would turn. A pause is what standing
    // still is for, and it is the only type whose speed byte may be zero.
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG,
                              mixer_program_decode(B64_ZERO_SPEED_FWD, &program),
                              "Forward at zero rpm is not a step");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG,
                              mixer_program_decode(B64_ZERO_SPEED_REV, &program),
                              "Neither is reverse at zero rpm");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, mixer_program_decode(B64_THREE_STEPS, &program),
                              "But a pause keeps its zero speed byte and decodes");
    TEST_ASSERT_EQUAL_MESSAGE(0, program.steps[1].speed, "That pause has no speed");

    // And the encoder refuses the same step from the other side.
    mixer_program_t motionless = {
        .count = 1,
        .steps = {{ .type = MIXER_STEP_FORWARD, .speed = 0, .duration_s = 10 }},
    };
    char encoded[MIXER_PROGRAM_MAX_B64_SIZE] = {0};
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG,
                              mixer_program_encode(&motionless, encoded, sizeof(encoded)),
                              "A motion step at zero rpm is not encodable either");
}

// The setting_validators entry point: the same rules, as the yes/no the settings API needs.
void test_program_validator(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test the mixer program validator");
    LOG_MESSAGE();

    TEST_ASSERT_TRUE_MESSAGE(mixer_program_is_valid(""), "An empty program may be stored");
    TEST_ASSERT_TRUE_MESSAGE(mixer_program_is_valid(B64_ONE_FORWARD), "One step may be stored");
    TEST_ASSERT_TRUE_MESSAGE(mixer_program_is_valid(B64_ELEVEN_STEPS),
                             "Eleven steps may be stored");

    TEST_ASSERT_FALSE_MESSAGE(mixer_program_is_valid(B64_TWELVE_STEPS),
                              "Twelve steps may not be stored");
    TEST_ASSERT_FALSE_MESSAGE(mixer_program_is_valid("!!!!"), "Broken base64 may not be stored");
    TEST_ASSERT_FALSE_MESSAGE(mixer_program_is_valid(B64_FIVE_BYTES),
                              "A partial step may not be stored");
    TEST_ASSERT_FALSE_MESSAGE(mixer_program_is_valid(B64_ZERO_DURATION),
                              "A zero-duration step may not be stored");
    TEST_ASSERT_FALSE_MESSAGE(mixer_program_is_valid(B64_BAD_TYPE),
                              "An unknown step type may not be stored");
    TEST_ASSERT_FALSE_MESSAGE(mixer_program_is_valid(B64_ZERO_SPEED_FWD),
                              "A motion step at zero rpm may not be stored");
    TEST_ASSERT_FALSE_MESSAGE(mixer_program_is_valid(NULL), "NULL may not be stored");
}

// The encode buffer is the caller's, so a buffer too small for the program must be reported
// rather than truncated into a value that would decode as a shorter program.
void test_encode_buffer_too_small(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test encoding into a buffer that is too small");
    LOG_MESSAGE();

    mixer_program_t program;
    TEST_ASSERT_EQUAL(ESP_OK, mixer_program_decode(B64_ELEVEN_STEPS, &program));

    char small[16] = {0};
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_SIZE,
                              mixer_program_encode(&program, small, sizeof(small)),
                              "A full program does not fit 16 bytes");

    char exact[MIXER_PROGRAM_MAX_B64_SIZE] = {0};
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, mixer_program_encode(&program, exact, sizeof(exact)),
                              "MIXER_PROGRAM_MAX_B64_SIZE is enough for a full program");
    TEST_ASSERT_EQUAL_STRING(B64_ELEVEN_STEPS, exact);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_decode_empty_program);
    RUN_TEST(test_decode_step_layout);
    RUN_TEST(test_encode_round_trip);
    RUN_TEST(test_step_count_ceiling);
    RUN_TEST(test_decode_rejects_malformed);
    RUN_TEST(test_program_validator);
    RUN_TEST(test_encode_buffer_too_small);

    return UNITY_END();
}
