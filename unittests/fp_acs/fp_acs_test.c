#include "unity.h"

/*
 * fp_acs: the sensor task, its events and requests (main/fp_acs/fp_acs.h).
 *
 * fp_acs.c is #included so that the test can reach its helpers (fp_acs_copy_chat(),
 * fp_acs_record_event(), ...), run one scan or one enrollment at a time, and reset its statics.
 * The sensor is a scripted fake (mocks/r503_fake.h), the store, the lock, the Telegram bot and the
 * access log are fakes too. Everything the code does to them lands in one trace, e.g. a granted scan:
 *     "GenImg Img2Tz1 Search open notify Led2.4 GenImg log UpImage"
 *
 * Delays advance the mock tick count, so the 30 s enrollment timeout passes in no time; the task
 * loop itself is driven like fp_lock's: every queue receive is one turn, and the receive after the
 * last turn longjmps back to the test.
 */

#include <setjmp.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fp_lock_fake.h"
#include "fp_log_fake.h"
#include "fp_store_fake.h"
#include "r503_fake.h"
#include "tg_bot_fake.h"
#include "trace.h"

static BaseType_t fp_acs_test_receive(QueueHandle_t queue, void *const buffer, TickType_t ticks);
static void fp_acs_test_delay(const TickType_t ticks);

#define xQueueReceive   fp_acs_test_receive
#define vTaskDelay      fp_acs_test_delay
#include "fp_acs.c"
#undef xQueueReceive
#undef vTaskDelay

#if !FP_ACS_ENABLED
    #error "fp_acs_test needs FP_ACS_ENABLED: check DEFS in the Makefile"
#endif

#define FP_ACS_TEST_USER            3
#define FP_ACS_TEST_SLOT            12
#define FP_ACS_TEST_OTHER_USER      4

static jmp_buf  fp_acs_test_exit;
static unsigned fp_acs_test_turns;
static unsigned fp_acs_test_turn;
static bool     fp_acs_test_loop;               // the task loop is under test: receives are turns
static bool     fp_acs_test_trace_delays;
static uint8_t  fp_acs_test_image[FP_ACS_IMAGE_SIZE];

// What fp_acs looked like from inside fp_log_append().
static uint32_t fp_acs_test_published_seq_while_logging;
static int      fp_acs_test_held_while_logging;


// ── Harness ──────────────────────────────────────────────────────────────────────────────

static BaseType_t fp_acs_test_receive(QueueHandle_t queue, void *const buffer, TickType_t ticks)
{
    if (fp_acs_test_loop) {
        if (fp_acs_test_turn >= fp_acs_test_turns) {
            longjmp(fp_acs_test_exit, 1);
        }
        fp_acs_test_turn++;
        trace_add("|W%u", (unsigned)pdTICKS_TO_MS(ticks));
    }
    return xQueueReceive(queue, buffer, ticks);
}

static void fp_acs_test_delay(const TickType_t ticks)
{
    if (fp_acs_test_trace_delays) {
        trace_add("D%u", (unsigned)pdTICKS_TO_MS(ticks));
    }
    mock_tick_count += ticks;
    vTaskDelay(ticks);
}

static void fp_acs_test_run_task(unsigned turns)
{
    fp_acs_test_loop = true;
    fp_acs_test_turns = turns;
    fp_acs_test_turn = 0;
    if (setjmp(fp_acs_test_exit) == 0) {
        fp_acs_task(NULL);
    }
    fp_acs_test_loop = false;
}

static void fp_acs_test_reset_statics(void)
{
    heap_caps_free(fp_acs_image);
    heap_caps_free(fp_acs_image_scratch);
    fp_acs_image = NULL;
    fp_acs_image_scratch = NULL;
    fp_acs_queue = NULL;
    fp_acs_mutex = NULL;
    fp_acs_online = false;
    fp_acs_capacity = 0;
    memset(&fp_acs_enroll, 0, sizeof(fp_acs_enroll));
    fp_acs_cancel_requested = false;
    memset(&fp_acs_last_event, 0, sizeof(fp_acs_last_event));
    fp_acs_image_seq = 0;
    fp_acs_image_event_seq = 0;
    fp_acs_transport_errors = 0;
    fp_acs_awaiting_lift = false;
    fp_acs_store_ok = false;
}

// Five placements: the finger comes, Img2Tz, the finger leaves.
static void fp_acs_test_push_takes(unsigned takes)
{
    for (unsigned i = 0; i < takes; i++) {
        r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 1);
        r503_fake_push_gen_img(ESP_OK, R503_CODE_NO_FINGER, 1);
    }
}

static size_t fp_acs_test_count(const char *haystack, const char *needle)
{
    size_t count = 0;
    for (const char *at = strstr(haystack, needle); at != NULL; at = strstr(at + 1, needle)) {
        count++;
    }
    return count;
}

static void fp_acs_test_log_hook(const fp_acs_event_t *event)
{
    (void)event;
    fp_acs_test_published_seq_while_logging = fp_acs_last_event.seq;
    fp_acs_test_held_while_logging = mock_xSemaphore_held_count;
}

static void fp_acs_test_cancel_on_third_poll(int call)
{
    if (call == 3) {
        fp_acs_enroll_cancel();
    }
}

void setUp(void)
{
    mock_freertos_queue_reset();
    mock_freertos_semaphore_reset();
    mock_freertos_task_reset();
    mock_esp_timer_reset();
    mock_heap_caps_reset();
    mock_log_reset();
    trace_reset();
    fp_store_fake_reset();
    fp_lock_fake_reset();
    fp_log_fake_reset();
    r503_fake_reset();
    tg_bot_fake_reset();
    fp_acs_test_reset_statics();
    fp_acs_test_loop = false;
    fp_acs_test_trace_delays = false;
    fp_acs_test_published_seq_while_logging = 0;
    fp_acs_test_held_while_logging = -1;
    memset(fp_acs_test_image, 0, sizeof(fp_acs_test_image));

    fp_store_fake_add_user(FP_ACS_TEST_USER, "Иван", true, true, true, "ivan");
    fp_store_fake_add_slot(FP_ACS_TEST_USER, FP_ACS_TEST_SLOT);

    // Started and online, as after a boot with the sensor answering.
    fp_acs_start();
    TEST_ASSERT_TRUE(fp_acs_connect());
    trace_reset();
    mock_log_reset();
}

void tearDown(void)
{
    fp_acs_test_reset_statics();
    mock_freertos_queue_reset();
}


// ── fp_acs_copy_chat() ───────────────────────────────────────────────────────────────────

static void fp_acs_test_copy(const char *chat, char out[FP_ACS_CHAT_MAX + 1])
{
    memset(out, '#', FP_ACS_CHAT_MAX + 1);
    fp_acs_copy_chat(out, chat);
}

static void test_chat_shorter_than_the_limit_is_copied_whole(void)
{
    char out[FP_ACS_CHAT_MAX + 1];
    fp_acs_test_copy("Office", out);
    TEST_ASSERT_EQUAL_STRING("Office", out);
    fp_acs_test_copy("", out);
    TEST_ASSERT_EQUAL_STRING("", out);
    fp_acs_test_copy("-1001234567890", out);
    TEST_ASSERT_EQUAL_STRING("-1001234567890", out);
}

static void test_chat_of_31_bytes_is_kept_whole(void)
{
    char out[FP_ACS_CHAT_MAX + 1];
    fp_acs_test_copy("0123456789012345678901234567890", out);
    TEST_ASSERT_EQUAL_STRING("0123456789012345678901234567890", out);
}

static void test_ascii_chat_is_cut_at_31_bytes(void)
{
    char out[FP_ACS_CHAT_MAX + 1];
    fp_acs_test_copy("0123456789012345678901234567890123456789", out);
    TEST_ASSERT_EQUAL_STRING("0123456789012345678901234567890", out);
}

static void test_two_byte_character_that_would_not_fit_is_left_out_whole(void)
{
    // 30 bytes and a Cyrillic letter (2 bytes) that would end at byte 32.
    char out[FP_ACS_CHAT_MAX + 1];
    fp_acs_test_copy("012345678901234567890123456789Ж", out);
    TEST_ASSERT_EQUAL_STRING("012345678901234567890123456789", out);
}

static void test_two_byte_character_ending_at_31_bytes_is_kept(void)
{
    char out[FP_ACS_CHAT_MAX + 1];
    fp_acs_test_copy("01234567890123456789012345678ЖЖ", out);
    TEST_ASSERT_EQUAL_STRING("01234567890123456789012345678Ж", out);
}

static void test_three_and_four_byte_characters_are_never_split(void)
{
    char out[FP_ACS_CHAT_MAX + 1];
    fp_acs_test_copy("01234567890123456789012345678€", out);       // the euro sign: 3 bytes, to byte 32
    TEST_ASSERT_EQUAL_STRING("01234567890123456789012345678", out);
    fp_acs_test_copy("0123456789012345678901234567😀", out);       // an emoji: 4 bytes, to byte 32
    TEST_ASSERT_EQUAL_STRING("0123456789012345678901234567", out);
    fp_acs_test_copy("012345678901234567890123456😀", out);        // to byte 31: fits
    TEST_ASSERT_EQUAL_STRING("012345678901234567890123456😀", out);
}

static void test_cyrillic_title_is_cut_to_the_longest_whole_prefix(void)
{
    // 2-byte letters and 1-byte spaces: whatever the cut, the copy is the longest prefix of whole
    // characters that fits in 31 bytes.
    const char *titles[] = {"Офис на Ленина, 5 этаж", "Склад № 2 — ночная смена", "ЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖ"};
    for (size_t t = 0; t < (sizeof(titles) / sizeof(titles[0])); t++) {
        char out[FP_ACS_CHAT_MAX + 1];
        fp_acs_test_copy(titles[t], out);
        size_t len = strlen(out);
        TEST_ASSERT_LESS_OR_EQUAL_size_t(FP_ACS_CHAT_MAX, len);
        TEST_ASSERT_EQUAL_MEMORY(titles[t], out, len);
        // The byte after the copy starts a character (or ends the string)...
        TEST_ASSERT_NOT_EQUAL(0x80, ((uint8_t)titles[t][len]) & 0xC0);
        // ...and that character does not fit.
        if (titles[t][len] != '\0') {
            size_t next = len + 1;
            while ((((uint8_t)titles[t][next]) & 0xC0) == 0x80) {
                next++;
            }
            TEST_ASSERT_GREATER_THAN_size_t(FP_ACS_CHAT_MAX, next);
        }
    }
}


// ── Events ───────────────────────────────────────────────────────────────────────────────

static void test_reports_before_start_record_nothing(void)
{
    fp_acs_test_reset_statics();
    fp_acs_report_telegram_event(FP_ACS_RESULT_GRANTED, FP_ACS_TEST_USER, "Office");
    fp_acs_report_mqtt_event();
    fp_acs_event_t event;
    TEST_ASSERT_FALSE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_INT(0, fp_log_fake_append_calls);
    TEST_ASSERT_FALSE(fp_acs_sensor_online());
}

static void test_no_event_since_boot(void)
{
    fp_acs_event_t event;
    TEST_ASSERT_FALSE(fp_acs_get_last_event(&event));
}

static void test_telegram_event_of_a_known_user_carries_the_name_and_the_chat(void)
{
    fp_log_fake_push_seq(17);
    fp_acs_report_telegram_event(FP_ACS_RESULT_GRANTED, FP_ACS_TEST_USER, "Office");

    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_UINT32(17, event.seq);
    TEST_ASSERT_EQUAL_INT(FP_ACS_SOURCE_TELEGRAM, event.source);
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_GRANTED, event.result);
    TEST_ASSERT_EQUAL_UINT8(FP_ACS_TEST_USER, event.user_id);
    TEST_ASSERT_EQUAL_STRING("Иван", event.user_name);
    TEST_ASSERT_FALSE(event.has_score);
    TEST_ASSERT_EQUAL_STRING("Office", event.chat);
    TEST_ASSERT_EQUAL_STRING("Office", fp_log_fake_events[0].chat);
    TEST_ASSERT_EQUAL_INT(FP_ACS_SOURCE_TELEGRAM, fp_log_fake_events[0].source);
    // Recording an attempt opens nothing: that is the caller's.
    TEST_ASSERT_EQUAL_INT(0, fp_lock_fake_open_calls);
}

static void test_telegram_event_of_an_unknown_user_id_names_nobody(void)
{
    fp_acs_report_telegram_event(FP_ACS_RESULT_DENIED, 77, "Office");
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_UINT8(FP_STORE_NO_USER, event.user_id);
    TEST_ASSERT_EQUAL_STRING("", event.user_name);
}

static void test_telegram_event_without_a_user_does_not_look_one_up(void)
{
    fp_store_fake_get_user_calls = 0;
    fp_acs_report_telegram_event(FP_ACS_RESULT_UNKNOWN, FP_STORE_NO_USER, "-1001234567890");
    TEST_ASSERT_EQUAL_INT(0, fp_store_fake_get_user_calls);
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_UNKNOWN, event.result);
    TEST_ASSERT_EQUAL_STRING("-1001234567890", event.chat);
}

static void test_telegram_event_chat_is_cut_at_a_character_boundary(void)
{
    fp_acs_report_telegram_event(FP_ACS_RESULT_GRANTED, FP_STORE_NO_USER, "Офис на Ленина, 5 этаж");
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    // 30 bytes: the next letter, two bytes of UTF-8, would end at byte 32.
    TEST_ASSERT_EQUAL_STRING("Офис на Ленина, 5 ", event.chat);
}

static void test_telegram_event_without_a_chat_has_an_empty_one(void)
{
    fp_acs_report_telegram_event(FP_ACS_RESULT_GRANTED, FP_STORE_NO_USER, NULL);
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_STRING("", event.chat);
}

static void test_mqtt_event_is_granted_with_no_user_and_no_chat(void)
{
    fp_acs_report_mqtt_event();
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_INT(FP_ACS_SOURCE_MQTT, event.source);
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_GRANTED, event.result);
    TEST_ASSERT_EQUAL_UINT8(FP_STORE_NO_USER, event.user_id);
    TEST_ASSERT_EQUAL_STRING("", event.user_name);
    TEST_ASSERT_FALSE(event.has_score);
    TEST_ASSERT_EQUAL_STRING("", event.chat);
    TEST_ASSERT_EQUAL_INT(0, fp_lock_fake_open_calls);
}

static void test_event_is_logged_before_it_is_published_and_outside_the_mutex(void)
{
    fp_acs_report_mqtt_event();
    fp_log_fake_append_hook = fp_acs_test_log_hook;
    fp_acs_report_mqtt_event();
    TEST_ASSERT_EQUAL_UINT32(1, fp_acs_test_published_seq_while_logging);
    TEST_ASSERT_EQUAL_INT(0, fp_acs_test_held_while_logging);
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_UINT32(2, event.seq);
}

static void test_event_that_lost_the_race_to_publish_does_not_replace_a_newer_one(void)
{
    // Logged as 5 by one task, then as 4 by another that published later.
    fp_log_fake_push_seq(5);
    fp_log_fake_push_seq(4);
    fp_acs_report_telegram_event(FP_ACS_RESULT_GRANTED, FP_ACS_TEST_USER, "Newer");
    fp_acs_report_mqtt_event();

    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_UINT32(5, event.seq);
    TEST_ASSERT_EQUAL_INT(FP_ACS_SOURCE_TELEGRAM, event.source);
    TEST_ASSERT_EQUAL_STRING("Newer", event.chat);
}

static void test_without_the_log_events_are_numbered_per_boot(void)
{
    fp_log_fake_running = false;
    fp_acs_report_mqtt_event();
    fp_acs_report_mqtt_event();
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_UINT32(2, event.seq);
}

static void test_event_carries_the_uptime_in_seconds(void)
{
    mock_esp_timer_get_time_value = 12999999;
    fp_acs_report_mqtt_event();
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_UINT32(12, event.uptime_s);
}


// ── Wire names ───────────────────────────────────────────────────────────────────────────

static void test_source_names(void)
{
    TEST_ASSERT_EQUAL_STRING("fingerprint", fp_acs_source_str(FP_ACS_SOURCE_FINGERPRINT));
    TEST_ASSERT_EQUAL_STRING("telegram", fp_acs_source_str(FP_ACS_SOURCE_TELEGRAM));
    TEST_ASSERT_EQUAL_STRING("mqtt", fp_acs_source_str(FP_ACS_SOURCE_MQTT));
    TEST_ASSERT_EQUAL_STRING("fingerprint", fp_acs_source_str((fp_acs_source_t)42));
}

static void test_result_names(void)
{
    TEST_ASSERT_EQUAL_STRING("granted", fp_acs_result_str(FP_ACS_RESULT_GRANTED));
    TEST_ASSERT_EQUAL_STRING("denied", fp_acs_result_str(FP_ACS_RESULT_DENIED));
    TEST_ASSERT_EQUAL_STRING("unknown", fp_acs_result_str(FP_ACS_RESULT_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("unknown", fp_acs_result_str((fp_acs_result_t)42));
}

static void test_enrollment_state_names(void)
{
    TEST_ASSERT_EQUAL_STRING("idle", fp_acs_enroll_state_str(FP_ACS_ENROLL_STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("place", fp_acs_enroll_state_str(FP_ACS_ENROLL_STATE_PLACE));
    TEST_ASSERT_EQUAL_STRING("lift", fp_acs_enroll_state_str(FP_ACS_ENROLL_STATE_LIFT));
    TEST_ASSERT_EQUAL_STRING("processing", fp_acs_enroll_state_str(FP_ACS_ENROLL_STATE_PROCESSING));
    TEST_ASSERT_EQUAL_STRING("done", fp_acs_enroll_state_str(FP_ACS_ENROLL_STATE_DONE));
    TEST_ASSERT_EQUAL_STRING("error", fp_acs_enroll_state_str(FP_ACS_ENROLL_STATE_ERROR));
    TEST_ASSERT_EQUAL_STRING("cancelled", fp_acs_enroll_state_str(FP_ACS_ENROLL_STATE_CANCELLED));
    TEST_ASSERT_EQUAL_STRING("idle", fp_acs_enroll_state_str((fp_acs_enroll_state_t)42));
}

static void test_request_refusal_names(void)
{
    TEST_ASSERT_EQUAL_STRING("ok", fp_acs_req_str(FP_ACS_REQ_OK));
    TEST_ASSERT_EQUAL_STRING("unknown user", fp_acs_req_str(FP_ACS_REQ_UNKNOWN_USER));
    TEST_ASSERT_EQUAL_STRING("unknown finger slot", fp_acs_req_str(FP_ACS_REQ_UNKNOWN_SLOT));
    TEST_ASSERT_EQUAL_STRING("sensor offline", fp_acs_req_str(FP_ACS_REQ_OFFLINE));
    TEST_ASSERT_EQUAL_STRING("busy", fp_acs_req_str(FP_ACS_REQ_BUSY));
    TEST_ASSERT_EQUAL_STRING("no room for another finger", fp_acs_req_str(FP_ACS_REQ_FULL));
    TEST_ASSERT_EQUAL_STRING("error", fp_acs_req_str((fp_acs_req_t)42));
}


// ── Scanning ─────────────────────────────────────────────────────────────────────────────

static void test_scan_without_a_finger_decides_nothing(void)
{
    fp_acs_scan();
    TEST_ASSERT_EQUAL_STRING("GenImg", trace_get());
    TEST_ASSERT_EQUAL_INT(0, fp_log_fake_append_calls);
}

static void test_granted_finger_opens_then_is_captured_then_logged_then_uploaded(void)
{
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 2);
    r503_fake_push_search(ESP_OK, R503_CODE_OK, FP_ACS_TEST_SLOT, 150);
    fp_acs_scan();

    TEST_ASSERT_EQUAL_STRING("GenImg Img2Tz1 Search open notify Led2.4 GenImg log UpImage", trace_get());
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_INT(FP_ACS_SOURCE_FINGERPRINT, event.source);
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_GRANTED, event.result);
    TEST_ASSERT_EQUAL_UINT8(FP_ACS_TEST_USER, event.user_id);
    TEST_ASSERT_EQUAL_STRING("Иван", event.user_name);
    TEST_ASSERT_TRUE(event.has_score);
    TEST_ASSERT_EQUAL_UINT16(150, event.score);
    TEST_ASSERT_EQUAL_STRING("", event.chat);

    // The image is the one taken for this event.
    fp_acs_status_t status;
    fp_acs_get_status(&status);
    TEST_ASSERT_EQUAL_UINT32(1, status.image_seq);
    TEST_ASSERT_EQUAL_UINT32(event.seq, status.image_event_seq);
    TEST_ASSERT_TRUE(fp_acs_awaiting_lift);
}

static void test_granted_finger_is_reported_to_the_bot_by_the_user_name(void)
{
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 2);
    r503_fake_push_search(ESP_OK, R503_CODE_OK, FP_ACS_TEST_SLOT, 150);
    fp_acs_scan();
    // Queued once the lock has the request: the opening does not wait for the bot.
    TEST_ASSERT_NOT_NULL(strstr(trace_get(), "open notify"));
    TEST_ASSERT_EQUAL_INT(1, tg_bot_fake_notify_calls);
    TEST_ASSERT_EQUAL_STRING("Иван", tg_bot_fake_notify_name);
}

static void test_finger_not_granted_is_not_reported_to_the_bot(void)
{
    fp_store_fake_add_user(FP_ACS_TEST_OTHER_USER, "Anna", false, true, true, "");
    fp_store_fake_add_slot(FP_ACS_TEST_OTHER_USER, 20);
    fp_store_fake_add_user(5, "Boris", true, false, true, "");
    fp_store_fake_add_slot(5, 21);
    // Disabled, not allowed fingerprints, a template nobody owns.
    const uint16_t pages[] = {20, 21, 99};
    for (size_t i = 0; i < (sizeof(pages) / sizeof(pages[0])); i++) {
        fp_acs_awaiting_lift = false;
        r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 2);
        r503_fake_push_search(ESP_OK, R503_CODE_OK, pages[i], 90);
        fp_acs_scan();
    }
    TEST_ASSERT_EQUAL_INT(3, fp_log_fake_append_calls);
    TEST_ASSERT_EQUAL_INT(0, fp_lock_fake_open_calls);
    TEST_ASSERT_EQUAL_INT(0, tg_bot_fake_notify_calls);
}

static void test_disabled_user_is_denied_and_the_door_stays_shut(void)
{
    fp_store_fake_add_user(FP_ACS_TEST_OTHER_USER, "Anna", false, true, true, "");
    fp_store_fake_add_slot(FP_ACS_TEST_OTHER_USER, 20);
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 2);
    r503_fake_push_search(ESP_OK, R503_CODE_OK, 20, 90);
    fp_acs_scan();

    TEST_ASSERT_EQUAL_STRING("GenImg Img2Tz1 Search Led2.1 GenImg log UpImage", trace_get());
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_DENIED, event.result);
    TEST_ASSERT_EQUAL_UINT8(FP_ACS_TEST_OTHER_USER, event.user_id);
    TEST_ASSERT_TRUE(event.has_score);
}

static void test_user_not_allowed_to_use_fingerprints_is_denied(void)
{
    fp_store_fake_add_user(FP_ACS_TEST_OTHER_USER, "Anna", true, false, true, "");
    fp_store_fake_add_slot(FP_ACS_TEST_OTHER_USER, 20);
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 2);
    r503_fake_push_search(ESP_OK, R503_CODE_OK, 20, 90);
    fp_acs_scan();
    TEST_ASSERT_EQUAL_INT(0, fp_lock_fake_open_calls);
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_DENIED, event.result);
}

static void test_template_nobody_owns_is_unknown(void)
{
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 2);
    r503_fake_push_search(ESP_OK, R503_CODE_OK, 99, 80);
    fp_acs_scan();
    TEST_ASSERT_EQUAL_INT(0, fp_lock_fake_open_calls);
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_UNKNOWN, event.result);
    TEST_ASSERT_EQUAL_UINT8(FP_STORE_NO_USER, event.user_id);
    TEST_ASSERT_FALSE(event.has_score);
}

static void test_unmatched_finger_is_searched_five_times_then_unknown(void)
{
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 6);
    fp_acs_scan();
    TEST_ASSERT_EQUAL_size_t(5, fp_acs_test_count(trace_get(), "Search"));
    TEST_ASSERT_NOT_NULL(strstr(trace_get(), "Search Led2.1 GenImg log UpImage"));
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_UNKNOWN, event.result);
}

static void test_match_on_a_later_pass_is_granted(void)
{
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 4);
    r503_fake_push_search(ESP_OK, R503_CODE_NOT_FOUND, 0, 0);
    r503_fake_push_search(ESP_OK, R503_CODE_NOT_FOUND, 0, 0);
    r503_fake_push_search(ESP_OK, R503_CODE_OK, FP_ACS_TEST_SLOT, 60);
    fp_acs_scan();
    TEST_ASSERT_EQUAL_INT(1, fp_lock_fake_open_calls);
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_GRANTED, event.result);
    TEST_ASSERT_EQUAL_UINT16(60, event.score);
}

static void test_finger_lifted_between_passes_ends_as_unknown(void)
{
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 1);
    fp_acs_scan();
    TEST_ASSERT_EQUAL_STRING("GenImg Img2Tz1 Search GenImg Led2.1 GenImg log", trace_get());
    fp_acs_event_t event;
    TEST_ASSERT_TRUE(fp_acs_get_last_event(&event));
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_UNKNOWN, event.result);
}

static void test_finger_lifted_before_the_capture_leaves_the_event_without_an_image(void)
{
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 1);
    r503_fake_push_search(ESP_OK, R503_CODE_OK, FP_ACS_TEST_SLOT, 150);
    fp_acs_scan();
    TEST_ASSERT_EQUAL_STRING("GenImg Img2Tz1 Search open notify Led2.4 GenImg log", trace_get());
    fp_acs_status_t status;
    fp_acs_get_status(&status);
    TEST_ASSERT_EQUAL_UINT32(0, status.image_seq);
    TEST_ASSERT_EQUAL_UINT32(FP_ACS_IMAGE_NO_EVENT, status.image_event_seq);
}

static void test_nothing_is_scanned_again_until_the_finger_leaves(void)
{
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 2);
    r503_fake_push_search(ESP_OK, R503_CODE_OK, FP_ACS_TEST_SLOT, 150);
    fp_acs_scan();
    trace_reset();

    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 1);           // still there
    fp_acs_scan();
    TEST_ASSERT_EQUAL_STRING("GenImg", trace_get());
    fp_acs_scan();                                              // gone: back to the idle light
    TEST_ASSERT_EQUAL_STRING("GenImg GenImg Led3.4", trace_get());
    TEST_ASSERT_FALSE(fp_acs_awaiting_lift);
    TEST_ASSERT_EQUAL_INT(1, fp_lock_fake_open_calls);
}

static void test_unreadable_image_decides_nothing(void)
{
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 1);
    r503_fake_push_img2tz(ESP_OK, 0x06);
    fp_acs_scan();
    TEST_ASSERT_EQUAL_STRING("GenImg Img2Tz1", trace_get());
    TEST_ASSERT_EQUAL_INT(0, fp_log_fake_append_calls);
    TEST_ASSERT_FALSE(fp_acs_awaiting_lift);
}

static void test_three_transport_errors_in_a_row_take_the_sensor_offline(void)
{
    snprintf(r503_fake_last_error, sizeof(r503_fake_last_error), "no answer");
    r503_fake_push_gen_img(ESP_ERR_TIMEOUT, 0, 2);
    r503_fake_push_gen_img(ESP_OK, R503_CODE_NO_FINGER, 1);
    r503_fake_push_gen_img(ESP_ERR_TIMEOUT, 0, 2);
    for (int i = 0; i < 5; i++) {
        fp_acs_scan();
    }
    // Two, a success, two: never three in a row.
    TEST_ASSERT_TRUE(fp_acs_sensor_online());
    TEST_ASSERT_EQUAL_size_t(4, mock_log_count_matching("GenImg: no answer"));

    r503_fake_push_gen_img(ESP_ERR_TIMEOUT, 0, 1);
    fp_acs_scan();
    TEST_ASSERT_FALSE(fp_acs_sensor_online());
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_matching("Sensor offline"));
}

static void test_store_unavailable_shows_a_red_idle_light(void)
{
    fp_acs_test_reset_statics();
    fp_store_fake_init_result = ESP_FAIL;
    fp_acs_start();
    trace_reset();
    TEST_ASSERT_TRUE(fp_acs_connect());
    TEST_ASSERT_EQUAL_STRING("VfyPwd ReadSysPara Led3.1", trace_get());
}


// ── The last image ───────────────────────────────────────────────────────────────────────

static void test_image_is_expanded_from_4_to_8_bits(void)
{
    fp_acs_upload_image(42);
    uint32_t seq = 0;
    TEST_ASSERT_EQUAL_size_t(FP_ACS_IMAGE_SIZE, fp_acs_copy_image(fp_acs_test_image, sizeof(fp_acs_test_image), &seq));
    TEST_ASSERT_EQUAL_UINT32(1, seq);
    TEST_ASSERT_EQUAL_UINT32(42, fp_acs_image_event_seq);
    for (size_t k = 0; k < R503_IMAGE_RAW_SIZE; k++) {
        uint8_t raw = r503_fake_up_image_raw(k);
        TEST_ASSERT_EQUAL_HEX8((raw >> 4) * 17, fp_acs_test_image[2 * k]);
        TEST_ASSERT_EQUAL_HEX8((raw & 0x0F) * 17, fp_acs_test_image[(2 * k) + 1]);
    }
}

static void test_newer_image_replaces_the_published_one(void)
{
    fp_acs_upload_image(1);
    r503_fake_up_image_byte = 0x00;
    fp_acs_upload_image(2);
    uint32_t seq = 0;
    TEST_ASSERT_EQUAL_size_t(FP_ACS_IMAGE_SIZE, fp_acs_copy_image(fp_acs_test_image, sizeof(fp_acs_test_image), &seq));
    TEST_ASSERT_EQUAL_UINT32(2, seq);
    TEST_ASSERT_EQUAL_HEX8(0x00, fp_acs_test_image[0]);
    TEST_ASSERT_EQUAL_UINT32(2, fp_acs_image_event_seq);
}

static void test_incomplete_image_does_not_replace_the_published_one(void)
{
    fp_acs_upload_image(1);
    r503_fake_up_image_len = R503_IMAGE_RAW_SIZE - 1;
    fp_acs_upload_image(2);
    r503_fake_up_image_len = R503_IMAGE_RAW_SIZE;
    r503_fake_up_image_result = (r503_fake_result_t){ESP_OK, 0x0F};
    fp_acs_upload_image(3);
    r503_fake_up_image_result = (r503_fake_result_t){ESP_ERR_TIMEOUT, 0};
    fp_acs_upload_image(4);

    fp_acs_status_t status;
    fp_acs_get_status(&status);
    TEST_ASSERT_EQUAL_UINT32(1, status.image_seq);
    TEST_ASSERT_EQUAL_UINT32(1, status.image_event_seq);
}

static void test_copy_image_needs_an_image_and_a_full_size_buffer(void)
{
    uint32_t seq = 77;
    TEST_ASSERT_EQUAL_size_t(0, fp_acs_copy_image(fp_acs_test_image, sizeof(fp_acs_test_image), &seq));
    TEST_ASSERT_EQUAL_UINT32(77, seq);
    fp_acs_upload_image(1);
    TEST_ASSERT_EQUAL_size_t(0, fp_acs_copy_image(fp_acs_test_image, FP_ACS_IMAGE_SIZE - 1, &seq));
    TEST_ASSERT_EQUAL_size_t(0, fp_acs_copy_image(NULL, FP_ACS_IMAGE_SIZE, &seq));
    TEST_ASSERT_EQUAL_size_t(FP_ACS_IMAGE_SIZE, fp_acs_copy_image(fp_acs_test_image, FP_ACS_IMAGE_SIZE, NULL));
}

static void test_without_psram_there_is_no_image(void)
{
    fp_acs_test_reset_statics();
    mock_heap_caps_fail_count = 1;
    fp_acs_start();
    TEST_ASSERT_NULL(fp_acs_image);
    TEST_ASSERT_NULL(fp_acs_image_scratch);
    fp_acs_upload_image(1);
    TEST_ASSERT_EQUAL_size_t(0, fp_acs_copy_image(fp_acs_test_image, sizeof(fp_acs_test_image), NULL));
}


// ── Enrollment ───────────────────────────────────────────────────────────────────────────

static void test_enrollment_takes_five_placements_and_stores_in_the_lowest_free_slot(void)
{
    fp_store_fake_add_user(FP_ACS_TEST_OTHER_USER, "Anna", true, true, false, "");
    fp_store_fake_add_slot(FP_ACS_TEST_OTHER_USER, 0);
    fp_acs_test_push_takes(5);
    fp_acs_run_enroll(FP_ACS_TEST_OTHER_USER);

    // The last take is uploaded before Img2Tz turns ImageBuffer into a ridge map.
    TEST_ASSERT_EQUAL_STRING("Led2.3 GenImg Img2Tz1 GenImg GenImg Img2Tz2 GenImg GenImg Img2Tz3 GenImg GenImg Img2Tz4 "
                             "GenImg GenImg UpImage Img2Tz5 GenImg RegModel Store1 Led3.4", trace_get());
    fp_acs_status_t status;
    fp_acs_get_status(&status);
    TEST_ASSERT_EQUAL_INT(FP_ACS_ENROLL_STATE_DONE, status.enroll.state);
    TEST_ASSERT_TRUE(status.enroll.has_slot);
    TEST_ASSERT_EQUAL_UINT16(1, status.enroll.slot);
    TEST_ASSERT_EQUAL_STRING("", status.enroll.error);
    TEST_ASSERT_EQUAL_UINT32(FP_ACS_IMAGE_NO_EVENT, status.image_event_seq);
    TEST_ASSERT_EQUAL_UINT32(1, status.image_seq);
    fp_store_user_t user;
    TEST_ASSERT_TRUE(fp_store_find_by_slot(1, &user));
    TEST_ASSERT_EQUAL_UINT8(FP_ACS_TEST_OTHER_USER, user.id);
    // The finger may still be on the sensor: it is not taken for someone at the door.
    TEST_ASSERT_TRUE(fp_acs_awaiting_lift);
}

static void test_enrollment_refusal_is_read_after_the_command_that_answers_it(void)
{
    // Regression (613be460a): the code was passed by value next to the call that fills it.
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 1);
    r503_fake_push_img2tz(ESP_OK, 0x07);
    fp_acs_run_enroll(FP_ACS_TEST_USER);

    char expected[FP_ACS_ENROLL_ERROR_MAX];
    snprintf(expected, sizeof(expected), "Img2Tz: %s", r503_code_str(0x07));
    TEST_ASSERT_EQUAL_INT(FP_ACS_ENROLL_STATE_ERROR, fp_acs_enroll.state);
    TEST_ASSERT_EQUAL_STRING(expected, fp_acs_enroll.error);
    // Solid red for 2 s, then the idle light.
    TEST_ASSERT_NOT_NULL(strstr(trace_get(), "Img2Tz1 Led3.1 Led3.4"));
}

static void test_enrollment_transport_error_names_the_drivers_reason(void)
{
    snprintf(r503_fake_last_error, sizeof(r503_fake_last_error), "checksum mismatch: received 0x000B, computed 0x000A");
    fp_acs_test_push_takes(5);
    r503_fake_push_reg_model(ESP_ERR_INVALID_RESPONSE, 0);
    fp_acs_run_enroll(FP_ACS_TEST_USER);
    // The longest reason the driver gives still fits after "RegModel: ".
    TEST_ASSERT_EQUAL_STRING("RegModel: checksum mismatch: received 0x000B, computed 0x000A", fp_acs_enroll.error);
}

static void test_enrollment_transport_error_without_a_reason_names_the_error(void)
{
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 1);
    r503_fake_push_img2tz(ESP_ERR_TIMEOUT, 0);
    fp_acs_run_enroll(FP_ACS_TEST_USER);
    TEST_ASSERT_EQUAL_STRING("Img2Tz: ESP_ERR_TIMEOUT", fp_acs_enroll.error);
}

static void test_enrollment_without_a_finger_for_30_s_times_out(void)
{
    TickType_t started = mock_tick_count;
    fp_acs_run_enroll(FP_ACS_TEST_USER);
    TEST_ASSERT_EQUAL_INT(FP_ACS_ENROLL_STATE_ERROR, fp_acs_enroll.state);
    TEST_ASSERT_EQUAL_STRING("timeout", fp_acs_enroll.error);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(pdMS_TO_TICKS(30000), mock_tick_count - started);
    TEST_ASSERT_LESS_THAN_UINT32(pdMS_TO_TICKS(30000 + 2000 + 200), mock_tick_count - started);
}

static void test_enrollment_waiting_for_the_lift_does_not_time_out(void)
{
    // The finger stays for a minute after the first take, then leaves; the rest go normally.
    r503_fake_push_gen_img(ESP_OK, R503_CODE_OK, 1 + 600);
    r503_fake_push_gen_img(ESP_OK, R503_CODE_NO_FINGER, 1);
    fp_acs_test_push_takes(4);
    fp_acs_run_enroll(FP_ACS_TEST_USER);
    TEST_ASSERT_EQUAL_INT(FP_ACS_ENROLL_STATE_DONE, fp_acs_enroll.state);
}

static void test_enrollment_can_be_cancelled_while_it_waits(void)
{
    r503_fake_gen_img_hook = fp_acs_test_cancel_on_third_poll;
    fp_acs_run_enroll(FP_ACS_TEST_USER);
    TEST_ASSERT_EQUAL_INT(FP_ACS_ENROLL_STATE_CANCELLED, fp_acs_enroll.state);
    TEST_ASSERT_EQUAL_STRING("", fp_acs_enroll.error);
    TEST_ASSERT_EQUAL_INT(3, r503_fake_gen_img_calls);
    TEST_ASSERT_FALSE(fp_acs_cancel_requested);
    TEST_ASSERT_NULL(strstr(trace_get(), "Led3.1"));
}

static void test_enrollment_fails_when_the_sensor_goes_offline(void)
{
    r503_fake_push_gen_img(ESP_ERR_TIMEOUT, 0, 3);
    fp_acs_run_enroll(FP_ACS_TEST_USER);
    TEST_ASSERT_EQUAL_INT(FP_ACS_ENROLL_STATE_ERROR, fp_acs_enroll.state);
    TEST_ASSERT_EQUAL_STRING("sensor offline", fp_acs_enroll.error);
    // No light to set on a sensor that does not answer.
    TEST_ASSERT_NULL(strstr(trace_get(), "Led3"));
}

static void test_enrollment_whose_takes_do_not_combine_fails(void)
{
    fp_acs_test_push_takes(5);
    r503_fake_push_reg_model(ESP_OK, 0x0A);
    fp_acs_run_enroll(FP_ACS_TEST_USER);
    char expected[FP_ACS_ENROLL_ERROR_MAX];
    snprintf(expected, sizeof(expected), "RegModel: %s", r503_code_str(0x0A));
    TEST_ASSERT_EQUAL_STRING(expected, fp_acs_enroll.error);
    TEST_ASSERT_NULL(strstr(trace_get(), "Store"));
}

static void test_enrollment_without_a_free_slot_fails(void)
{
    r503_fake_capacity = 1;
    fp_store_fake_add_user(FP_ACS_TEST_OTHER_USER, "Anna", true, true, false, "");
    fp_store_fake_add_slot(FP_ACS_TEST_OTHER_USER, 0);
    TEST_ASSERT_TRUE(fp_acs_connect());
    fp_acs_test_push_takes(5);
    fp_acs_run_enroll(FP_ACS_TEST_USER);
    TEST_ASSERT_EQUAL_STRING("no free slot on the sensor", fp_acs_enroll.error);
}

static void test_finger_that_cannot_be_saved_is_deleted_from_the_sensor(void)
{
    fp_store_fake_add_finger_result = ESP_ERR_NOT_FOUND;
    fp_acs_test_push_takes(5);
    fp_acs_run_enroll(FP_ACS_TEST_USER);
    TEST_ASSERT_NOT_NULL(strstr(trace_get(), "RegModel Store0 Delete0"));
    TEST_ASSERT_EQUAL_STRING("saving the finger: ESP_ERR_NOT_FOUND", fp_acs_enroll.error);
    TEST_ASSERT_FALSE(fp_acs_enroll.has_slot);
}

static void test_enrollment_checks_cancel_sensor_and_user_before_it_starts(void)
{
    fp_acs_cancel_requested = true;
    fp_acs_run_enroll(FP_ACS_TEST_USER);
    TEST_ASSERT_EQUAL_INT(FP_ACS_ENROLL_STATE_CANCELLED, fp_acs_enroll.state);

    fp_acs_run_enroll(FP_ACS_TEST_OTHER_USER);
    TEST_ASSERT_EQUAL_INT(FP_ACS_ENROLL_STATE_ERROR, fp_acs_enroll.state);
    TEST_ASSERT_EQUAL_STRING("unknown user", fp_acs_enroll.error);

    fp_acs_online = false;
    fp_acs_run_enroll(FP_ACS_TEST_USER);
    TEST_ASSERT_EQUAL_STRING("sensor offline", fp_acs_enroll.error);
    TEST_ASSERT_EQUAL_INT(0, r503_fake_gen_img_calls);
}


// ── Requests ─────────────────────────────────────────────────────────────────────────────

static void test_enroll_start_checks_the_user_its_room_and_the_sensor(void)
{
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_UNKNOWN_USER, fp_acs_enroll_start(FP_ACS_TEST_OTHER_USER));

    fp_store_fake_add_user(FP_ACS_TEST_OTHER_USER, "Anna", true, true, false, "");
    for (uint16_t slot = 100; slot < (100 + FP_STORE_FINGERS_MAX); slot++) {
        fp_store_fake_add_slot(FP_ACS_TEST_OTHER_USER, slot);
    }
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_FULL, fp_acs_enroll_start(FP_ACS_TEST_OTHER_USER));

    fp_acs_online = false;
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_OFFLINE, fp_acs_enroll_start(FP_ACS_TEST_USER));
    fp_acs_online = true;

    fp_acs_capacity = 1;                                        // slot 0 free...
    fp_store_fake_add_slot(FP_ACS_TEST_USER, 0);                // ...no longer
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_FULL, fp_acs_enroll_start(FP_ACS_TEST_USER));
    TEST_ASSERT_EQUAL_INT(0, mock_xQueueSend_data.called);
}

static void test_enroll_start_resets_the_status_at_once_and_queues_the_enrollment(void)
{
    fp_acs_enroll.state = FP_ACS_ENROLL_STATE_DONE;
    fp_acs_enroll.has_slot = true;
    fp_acs_enroll.slot = 9;
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_OK, fp_acs_enroll_start(FP_ACS_TEST_USER));

    fp_acs_status_t status;
    fp_acs_get_status(&status);
    TEST_ASSERT_EQUAL_INT(FP_ACS_ENROLL_STATE_PLACE, status.enroll.state);
    TEST_ASSERT_EQUAL_UINT8(FP_ACS_TEST_USER, status.enroll.user_id);
    TEST_ASSERT_EQUAL_UINT8(1, status.enroll.step);
    TEST_ASSERT_FALSE(status.enroll.has_slot);

    fp_acs_cmd_t cmd;
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueReceive(fp_acs_queue, &cmd, 0));
    TEST_ASSERT_EQUAL_INT(FP_ACS_CMD_ENROLL, cmd.type);
    TEST_ASSERT_EQUAL_UINT8(FP_ACS_TEST_USER, cmd.user_id);
}

static void test_enroll_start_refuses_while_one_runs_or_the_queue_is_full(void)
{
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_OK, fp_acs_enroll_start(FP_ACS_TEST_USER));
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_BUSY, fp_acs_enroll_start(FP_ACS_TEST_USER));

    fp_acs_enroll.state = FP_ACS_ENROLL_STATE_ERROR;
    mock_xQueueSend_data.should_fail = true;
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_BUSY, fp_acs_enroll_start(FP_ACS_TEST_USER));
    TEST_ASSERT_EQUAL_INT(FP_ACS_ENROLL_STATE_ERROR, fp_acs_enroll.state);
}

static void test_requests_before_start_are_refused(void)
{
    fp_acs_test_reset_statics();
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_OFFLINE, fp_acs_enroll_start(FP_ACS_TEST_USER));
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_OFFLINE, fp_acs_delete_user(FP_ACS_TEST_USER));
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_OFFLINE, fp_acs_delete_finger(FP_ACS_TEST_SLOT));
    fp_acs_enroll_cancel();
    fp_acs_status_t status;
    fp_acs_get_status(&status);
    TEST_ASSERT_FALSE(status.online);
    TEST_ASSERT_EQUAL_size_t(1, status.used);
}

static void test_cancel_marks_only_a_running_enrollment(void)
{
    fp_acs_enroll_cancel();
    TEST_ASSERT_FALSE(fp_acs_cancel_requested);
    fp_acs_enroll.state = FP_ACS_ENROLL_STATE_LIFT;
    fp_acs_enroll_cancel();
    TEST_ASSERT_TRUE(fp_acs_cancel_requested);
}

static void test_deletion_requests_check_what_they_delete(void)
{
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_UNKNOWN_USER, fp_acs_delete_user(FP_ACS_TEST_OTHER_USER));
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_UNKNOWN_SLOT, fp_acs_delete_finger(99));
    TEST_ASSERT_EQUAL_INT(0, mock_xQueueSend_data.called);

    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_OK, fp_acs_delete_user(FP_ACS_TEST_USER));
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_OK, fp_acs_delete_finger(FP_ACS_TEST_SLOT));
    mock_xQueueSend_data.should_fail = true;
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_BUSY, fp_acs_delete_finger(FP_ACS_TEST_SLOT));
}

static void test_user_deletion_removes_the_templates_then_the_user(void)
{
    fp_store_fake_add_slot(FP_ACS_TEST_USER, 30);
    fp_acs_cmd_t cmd = {.type = FP_ACS_CMD_DELETE_USER, .user_id = FP_ACS_TEST_USER};
    fp_acs_handle_command(&cmd);
    TEST_ASSERT_EQUAL_STRING("Delete12 Delete30", trace_get());
    TEST_ASSERT_FALSE(fp_store_get_user(FP_ACS_TEST_USER, NULL));
}

static void test_user_deletion_with_the_sensor_offline_still_updates_the_store(void)
{
    fp_acs_online = false;
    fp_acs_cmd_t cmd = {.type = FP_ACS_CMD_DELETE_USER, .user_id = FP_ACS_TEST_USER};
    fp_acs_handle_command(&cmd);
    TEST_ASSERT_EQUAL_STRING("", trace_get());
    TEST_ASSERT_FALSE(fp_store_get_user(FP_ACS_TEST_USER, NULL));
    TEST_ASSERT_NOT_NULL(mock_log_find("template 12 stays on it until the slot is reused"));
}

static void test_finger_deletion_removes_the_template_then_the_slot(void)
{
    fp_acs_cmd_t cmd = {.type = FP_ACS_CMD_DELETE_FINGER, .slot = FP_ACS_TEST_SLOT};
    fp_acs_handle_command(&cmd);
    TEST_ASSERT_EQUAL_STRING("Delete12", trace_get());
    TEST_ASSERT_FALSE(fp_store_find_by_slot(FP_ACS_TEST_SLOT, NULL));
    TEST_ASSERT_TRUE(fp_store_get_user(FP_ACS_TEST_USER, NULL));
}

static void test_status_reports_the_sensor_and_the_store(void)
{
    fp_acs_status_t status;
    fp_acs_get_status(&status);
    TEST_ASSERT_TRUE(status.online);
    TEST_ASSERT_EQUAL_UINT16(200, status.capacity);
    TEST_ASSERT_EQUAL_UINT16(1, status.used);
    TEST_ASSERT_EQUAL_UINT32(fp_store_users_rev(), status.users_rev);
    TEST_ASSERT_EQUAL_INT(FP_ACS_ENROLL_STATE_IDLE, status.enroll.state);
    TEST_ASSERT_EQUAL_UINT32(0, status.image_seq);
}


// ── The task loop ────────────────────────────────────────────────────────────────────────

static void test_boot_tries_three_handshakes_then_retries_every_3_s(void)
{
    r503_fake_connect_err = ESP_ERR_TIMEOUT;
    fp_acs_online = false;
    fp_acs_test_trace_delays = true;
    fp_acs_test_run_task(2);
    TEST_ASSERT_EQUAL_STRING("VfyPwd D1000 VfyPwd D1000 VfyPwd D1000 |W3000 VfyPwd |W3000 VfyPwd", trace_get());
    TEST_ASSERT_NOT_NULL(mock_log_find("No answer from the sensor on UART2"));
}

static void test_online_task_polls_every_100_ms_and_serves_requests_first(void)
{
    fp_acs_online = false;
    TEST_ASSERT_EQUAL_INT(FP_ACS_REQ_OK, fp_acs_delete_finger(FP_ACS_TEST_SLOT));
    fp_acs_test_run_task(2);
    TEST_ASSERT_EQUAL_STRING("VfyPwd ReadSysPara Led3.4 |W100 Delete12 |W100 GenImg", trace_get());
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_chat_shorter_than_the_limit_is_copied_whole);
    RUN_TEST(test_chat_of_31_bytes_is_kept_whole);
    RUN_TEST(test_ascii_chat_is_cut_at_31_bytes);
    RUN_TEST(test_two_byte_character_that_would_not_fit_is_left_out_whole);
    RUN_TEST(test_two_byte_character_ending_at_31_bytes_is_kept);
    RUN_TEST(test_three_and_four_byte_characters_are_never_split);
    RUN_TEST(test_cyrillic_title_is_cut_to_the_longest_whole_prefix);

    RUN_TEST(test_reports_before_start_record_nothing);
    RUN_TEST(test_no_event_since_boot);
    RUN_TEST(test_telegram_event_of_a_known_user_carries_the_name_and_the_chat);
    RUN_TEST(test_telegram_event_of_an_unknown_user_id_names_nobody);
    RUN_TEST(test_telegram_event_without_a_user_does_not_look_one_up);
    RUN_TEST(test_telegram_event_chat_is_cut_at_a_character_boundary);
    RUN_TEST(test_telegram_event_without_a_chat_has_an_empty_one);
    RUN_TEST(test_mqtt_event_is_granted_with_no_user_and_no_chat);
    RUN_TEST(test_event_is_logged_before_it_is_published_and_outside_the_mutex);
    RUN_TEST(test_event_that_lost_the_race_to_publish_does_not_replace_a_newer_one);
    RUN_TEST(test_without_the_log_events_are_numbered_per_boot);
    RUN_TEST(test_event_carries_the_uptime_in_seconds);

    RUN_TEST(test_source_names);
    RUN_TEST(test_result_names);
    RUN_TEST(test_enrollment_state_names);
    RUN_TEST(test_request_refusal_names);

    RUN_TEST(test_scan_without_a_finger_decides_nothing);
    RUN_TEST(test_granted_finger_opens_then_is_captured_then_logged_then_uploaded);
    RUN_TEST(test_granted_finger_is_reported_to_the_bot_by_the_user_name);
    RUN_TEST(test_finger_not_granted_is_not_reported_to_the_bot);
    RUN_TEST(test_disabled_user_is_denied_and_the_door_stays_shut);
    RUN_TEST(test_user_not_allowed_to_use_fingerprints_is_denied);
    RUN_TEST(test_template_nobody_owns_is_unknown);
    RUN_TEST(test_unmatched_finger_is_searched_five_times_then_unknown);
    RUN_TEST(test_match_on_a_later_pass_is_granted);
    RUN_TEST(test_finger_lifted_between_passes_ends_as_unknown);
    RUN_TEST(test_finger_lifted_before_the_capture_leaves_the_event_without_an_image);
    RUN_TEST(test_nothing_is_scanned_again_until_the_finger_leaves);
    RUN_TEST(test_unreadable_image_decides_nothing);
    RUN_TEST(test_three_transport_errors_in_a_row_take_the_sensor_offline);
    RUN_TEST(test_store_unavailable_shows_a_red_idle_light);

    RUN_TEST(test_image_is_expanded_from_4_to_8_bits);
    RUN_TEST(test_newer_image_replaces_the_published_one);
    RUN_TEST(test_incomplete_image_does_not_replace_the_published_one);
    RUN_TEST(test_copy_image_needs_an_image_and_a_full_size_buffer);
    RUN_TEST(test_without_psram_there_is_no_image);

    RUN_TEST(test_enrollment_takes_five_placements_and_stores_in_the_lowest_free_slot);
    RUN_TEST(test_enrollment_refusal_is_read_after_the_command_that_answers_it);
    RUN_TEST(test_enrollment_transport_error_names_the_drivers_reason);
    RUN_TEST(test_enrollment_transport_error_without_a_reason_names_the_error);
    RUN_TEST(test_enrollment_without_a_finger_for_30_s_times_out);
    RUN_TEST(test_enrollment_waiting_for_the_lift_does_not_time_out);
    RUN_TEST(test_enrollment_can_be_cancelled_while_it_waits);
    RUN_TEST(test_enrollment_fails_when_the_sensor_goes_offline);
    RUN_TEST(test_enrollment_whose_takes_do_not_combine_fails);
    RUN_TEST(test_enrollment_without_a_free_slot_fails);
    RUN_TEST(test_finger_that_cannot_be_saved_is_deleted_from_the_sensor);
    RUN_TEST(test_enrollment_checks_cancel_sensor_and_user_before_it_starts);

    RUN_TEST(test_enroll_start_checks_the_user_its_room_and_the_sensor);
    RUN_TEST(test_enroll_start_resets_the_status_at_once_and_queues_the_enrollment);
    RUN_TEST(test_enroll_start_refuses_while_one_runs_or_the_queue_is_full);
    RUN_TEST(test_requests_before_start_are_refused);
    RUN_TEST(test_cancel_marks_only_a_running_enrollment);
    RUN_TEST(test_deletion_requests_check_what_they_delete);
    RUN_TEST(test_user_deletion_removes_the_templates_then_the_user);
    RUN_TEST(test_user_deletion_with_the_sensor_offline_still_updates_the_store);
    RUN_TEST(test_finger_deletion_removes_the_template_then_the_slot);
    RUN_TEST(test_status_reports_the_sensor_and_the_store);

    RUN_TEST(test_boot_tries_three_handshakes_then_retries_every_3_s);
    RUN_TEST(test_online_task_polls_every_100_ms_and_serves_requests_first);

    return UNITY_END();
}
