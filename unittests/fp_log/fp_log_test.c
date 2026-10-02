#include "unity.h"

/*
 * fp_log: the access log, a ring of FP_LOG_CAPACITY records kept in NVS with a RAM copy
 * (main/fp_acs/fp_log.h).
 *
 * fp_log.c is #included so that the test can reboot the log - drop its RAM copy and statics while
 * the fake NVS keeps what was written - and stamp records with a clock of its own: time() is
 * redirected to fp_log_test_time() for that file only.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "nvs.h"

time_t fp_log_test_time(time_t *out);

#define time(out)   fp_log_test_time(out)
#include "fp_log.c"
#undef time

#if !FP_ACS_ENABLED
    #error "fp_log_test needs FP_ACS_ENABLED: check DEFS in the Makefile"
#endif

#define FP_LOG_TEST_NOW             1767225600      // 2026-01-01T00:00:00Z
#define FP_LOG_TEST_KEY_SIZE        16

// The layout of version 1, before the chat: what a device updated from it still has in NVS.
typedef struct {
    uint8_t  version;
    uint8_t  source;
    uint8_t  result;
    uint8_t  user_id;
    uint32_t seq;
    uint32_t time;
    uint16_t score;
    uint8_t  has_score;
    char     user_name[FP_STORE_NAME_MAX + 1];
} fp_log_test_blob_v1_t;

_Static_assert(sizeof(fp_log_test_blob_v1_t) == 64, "the version 1 record was 64 bytes");

static time_t         fp_log_test_now;
static fp_log_entry_t fp_log_test_out[FP_LOG_CAPACITY + 8];

time_t fp_log_test_time(time_t *out)
{
    if (out != NULL) {
        *out = fp_log_test_now;
    }
    return fp_log_test_now;
}

// What a reboot leaves of the log: nothing but NVS.
static void fp_log_test_reboot(void)
{
    heap_caps_free(fp_log_entries);
    fp_log_entries = NULL;
    fp_log_mutex = NULL;
    fp_log_next = FP_LOG_FIRST_SEQ;
    fp_log_nvs_ok = false;
    fp_log_nvs = 0;
}

static fp_acs_event_t fp_log_test_event(fp_acs_source_t source, fp_acs_result_t result, uint8_t user_id,
                                        const char *user_name, const char *chat)
{
    fp_acs_event_t event;
    memset(&event, 0, sizeof(event));
    event.seq = 999;                                // not read by fp_log_append()
    event.source = source;
    event.result = result;
    event.user_id = user_id;
    snprintf(event.user_name, sizeof(event.user_name), "%s", user_name);
    snprintf(event.chat, sizeof(event.chat), "%s", chat);
    return event;
}

static uint32_t fp_log_test_append_mqtt(void)
{
    fp_acs_event_t event = fp_log_test_event(FP_ACS_SOURCE_MQTT, FP_ACS_RESULT_GRANTED, FP_STORE_NO_USER, "", "");
    return fp_log_append(&event);
}

static void fp_log_test_key(uint32_t slot, char key[FP_LOG_TEST_KEY_SIZE])
{
    snprintf(key, FP_LOG_TEST_KEY_SIZE, "e%u", (unsigned)slot);
}

static fp_log_blob_t fp_log_test_blob(uint32_t seq, fp_acs_source_t source, const char *user_name, const char *chat)
{
    fp_log_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.version = FP_LOG_BLOB_VERSION;
    blob.source = (uint8_t)source;
    blob.result = (uint8_t)FP_ACS_RESULT_GRANTED;
    blob.user_id = FP_STORE_NO_USER;
    blob.seq = seq;
    blob.time = FP_LOG_TEST_NOW;
    snprintf(blob.user_name, sizeof(blob.user_name), "%s", user_name);
    snprintf(blob.chat, sizeof(blob.chat), "%s", chat);
    return blob;
}

// Puts a record where fp_log keeps it: key "e<seq % capacity>".
static void fp_log_test_seed_blob(const fp_log_blob_t *blob)
{
    char key[FP_LOG_TEST_KEY_SIZE];
    fp_log_test_key(blob->seq % FP_LOG_CAPACITY, key);
    nvs_fake_put_blob(key, blob, sizeof(*blob));
}

static void fp_log_test_seed(uint32_t seq)
{
    fp_log_blob_t blob = fp_log_test_blob(seq, FP_ACS_SOURCE_FINGERPRINT, "", "");
    fp_log_test_seed_blob(&blob);
}

void setUp(void)
{
    nvs_fake_reset();
    mock_sntp_reset();
    mock_heap_caps_reset();
    mock_freertos_semaphore_reset();
    mock_log_reset();
    fp_log_test_reboot();
    fp_log_test_now = FP_LOG_TEST_NOW;
    memset(fp_log_test_out, 0, sizeof(fp_log_test_out));
}

void tearDown(void)
{
    fp_log_test_reboot();
}


// ── Before init ──────────────────────────────────────────────────────────────────────────

static void test_log_does_nothing_before_init(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, fp_log_test_append_mqtt());
    TEST_ASSERT_EQUAL_size_t(0, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY));
    TEST_ASSERT_EQUAL_INT(0, nvs_fake_set_blob_calls);
}

static void test_init_without_memory_leaves_the_log_off(void)
{
    mock_heap_caps_fail_count = 1;
    fp_log_init();
    TEST_ASSERT_EQUAL_UINT32(0, fp_log_test_append_mqtt());
    TEST_ASSERT_EQUAL_INT(0, nvs_fake_open_calls);
}

static void test_init_without_a_mutex_leaves_the_log_off(void)
{
    mock_xSemaphoreCreateMutex_return_value = NULL;
    fp_log_init();
    TEST_ASSERT_NULL(fp_log_entries);
    TEST_ASSERT_EQUAL_UINT32(0, fp_log_test_append_mqtt());
}

static void test_append_of_no_event_returns_0(void)
{
    fp_log_init();
    TEST_ASSERT_EQUAL_UINT32(0, fp_log_append(NULL));
}


// ── Layout ───────────────────────────────────────────────────────────────────────────────

static void test_record_is_version_2_in_96_bytes_without_padding(void)
{
    TEST_ASSERT_EQUAL_INT(2, FP_LOG_BLOB_VERSION);
    TEST_ASSERT_EQUAL_size_t(96, sizeof(fp_log_blob_t));
    TEST_ASSERT_EQUAL_size_t(0, offsetof(fp_log_blob_t, version));
    TEST_ASSERT_EQUAL_size_t(1, offsetof(fp_log_blob_t, source));
    TEST_ASSERT_EQUAL_size_t(2, offsetof(fp_log_blob_t, result));
    TEST_ASSERT_EQUAL_size_t(3, offsetof(fp_log_blob_t, user_id));
    TEST_ASSERT_EQUAL_size_t(4, offsetof(fp_log_blob_t, seq));
    TEST_ASSERT_EQUAL_size_t(8, offsetof(fp_log_blob_t, time));
    TEST_ASSERT_EQUAL_size_t(12, offsetof(fp_log_blob_t, score));
    TEST_ASSERT_EQUAL_size_t(14, offsetof(fp_log_blob_t, has_score));
    TEST_ASSERT_EQUAL_size_t(15, offsetof(fp_log_blob_t, user_name));
    TEST_ASSERT_EQUAL_size_t(64, offsetof(fp_log_blob_t, chat));
    TEST_ASSERT_EQUAL_size_t(32, sizeof(((fp_log_blob_t *)0)->chat));
}

static void test_record_is_written_byte_for_byte_in_the_version_2_layout(void)
{
    fp_log_init();
    fp_acs_event_t event = fp_log_test_event(FP_ACS_SOURCE_TELEGRAM, FP_ACS_RESULT_DENIED, 5, "Anna", "Office");
    event.has_score = true;
    event.score = 0x0102;
    TEST_ASSERT_EQUAL_UINT32(1, fp_log_append(&event));

    uint8_t raw[NVS_FAKE_BLOB_MAX];
    size_t len = sizeof(raw);
    TEST_ASSERT_TRUE(nvs_fake_peek_blob("e1", raw, &len));
    TEST_ASSERT_EQUAL_size_t(96, len);
    TEST_ASSERT_EQUAL_UINT8(2, raw[0]);
    TEST_ASSERT_EQUAL_UINT8(FP_ACS_SOURCE_TELEGRAM, raw[1]);
    TEST_ASSERT_EQUAL_UINT8(FP_ACS_RESULT_DENIED, raw[2]);
    TEST_ASSERT_EQUAL_UINT8(5, raw[3]);
    uint32_t seq = 0;
    uint32_t stamp = 0;
    uint16_t score = 0;
    memcpy(&seq, &raw[4], sizeof(seq));
    memcpy(&stamp, &raw[8], sizeof(stamp));
    memcpy(&score, &raw[12], sizeof(score));
    TEST_ASSERT_EQUAL_UINT32(1, seq);
    TEST_ASSERT_EQUAL_UINT32(FP_LOG_TEST_NOW, stamp);
    TEST_ASSERT_EQUAL_UINT16(0x0102, score);
    TEST_ASSERT_EQUAL_UINT8(1, raw[14]);
    TEST_ASSERT_EQUAL_STRING("Anna", (const char *)&raw[15]);
    TEST_ASSERT_EQUAL_STRING("Office", (const char *)&raw[64]);
}


// ── Appending and reading ────────────────────────────────────────────────────────────────

static void test_init_on_an_empty_partition_starts_at_seq_1(void)
{
    fp_log_init();
    TEST_ASSERT_EQUAL_STRING("fp_nvs", nvs_fake_opened_partition);
    TEST_ASSERT_EQUAL_STRING("fp_log", nvs_fake_opened_namespace);
    TEST_ASSERT_EQUAL_INT(NVS_READWRITE, nvs_fake_opened_mode);
    TEST_ASSERT_EQUAL_size_t(0, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY));

    TEST_ASSERT_EQUAL_UINT32(1, fp_log_test_append_mqtt());
    uint32_t next = 0;
    TEST_ASSERT_TRUE(nvs_fake_peek_u32("next", &next));
    TEST_ASSERT_EQUAL_UINT32(2, next);
    TEST_ASSERT_EQUAL_INT(1, nvs_fake_commit_calls);
}

static void test_record_keeps_every_field_of_the_event(void)
{
    fp_log_init();
    fp_acs_event_t scan = fp_log_test_event(FP_ACS_SOURCE_FINGERPRINT, FP_ACS_RESULT_GRANTED, 3, "Иван", "");
    scan.has_score = true;
    scan.score = 187;
    fp_acs_event_t bot = fp_log_test_event(FP_ACS_SOURCE_TELEGRAM, FP_ACS_RESULT_UNKNOWN, FP_STORE_NO_USER, "",
                                           "Офис на Ленина");
    TEST_ASSERT_EQUAL_UINT32(1, fp_log_append(&scan));
    TEST_ASSERT_EQUAL_UINT32(2, fp_log_append(&bot));

    TEST_ASSERT_EQUAL_size_t(2, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY));
    const fp_log_entry_t *newest = &fp_log_test_out[0];
    TEST_ASSERT_EQUAL_UINT32(2, newest->seq);
    TEST_ASSERT_EQUAL_UINT32(FP_LOG_TEST_NOW, newest->time);
    TEST_ASSERT_EQUAL_INT(FP_ACS_SOURCE_TELEGRAM, newest->source);
    TEST_ASSERT_EQUAL_INT(FP_ACS_RESULT_UNKNOWN, newest->result);
    TEST_ASSERT_EQUAL_UINT8(FP_STORE_NO_USER, newest->user_id);
    TEST_ASSERT_EQUAL_STRING("", newest->user_name);
    TEST_ASSERT_FALSE(newest->has_score);
    TEST_ASSERT_EQUAL_STRING("Офис на Ленина", newest->chat);

    const fp_log_entry_t *oldest = &fp_log_test_out[1];
    TEST_ASSERT_EQUAL_UINT32(1, oldest->seq);
    TEST_ASSERT_EQUAL_INT(FP_ACS_SOURCE_FINGERPRINT, oldest->source);
    TEST_ASSERT_EQUAL_UINT8(3, oldest->user_id);
    TEST_ASSERT_EQUAL_STRING("Иван", oldest->user_name);
    TEST_ASSERT_TRUE(oldest->has_score);
    TEST_ASSERT_EQUAL_UINT16(187, oldest->score);
    TEST_ASSERT_EQUAL_STRING("", oldest->chat);
}

static void test_clock_not_set_yet_stamps_time_unknown(void)
{
    fp_log_init();
    fp_log_test_now = 1704067199;                   // a second before 2024: still counting from boot
    fp_log_test_append_mqtt();
    fp_log_test_now = 1704067200;
    fp_log_test_append_mqtt();

    TEST_ASSERT_EQUAL_size_t(2, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY));
    TEST_ASSERT_EQUAL_UINT32(1704067200, fp_log_test_out[0].time);
    TEST_ASSERT_EQUAL_UINT32(FP_LOG_TIME_UNKNOWN, fp_log_test_out[1].time);
}

static void test_snapshot_is_newest_first_and_stops_at_max(void)
{
    fp_log_init();
    for (int i = 0; i < 5; i++) {
        fp_log_test_append_mqtt();
    }
    TEST_ASSERT_EQUAL_size_t(3, fp_log_snapshot(fp_log_test_out, 3));
    TEST_ASSERT_EQUAL_UINT32(5, fp_log_test_out[0].seq);
    TEST_ASSERT_EQUAL_UINT32(4, fp_log_test_out[1].seq);
    TEST_ASSERT_EQUAL_UINT32(3, fp_log_test_out[2].seq);
    TEST_ASSERT_EQUAL_size_t(0, fp_log_snapshot(NULL, 3));
}

static void test_ring_keeps_the_last_200_records(void)
{
    fp_log_init();
    for (uint32_t i = 1; i <= 205; i++) {
        TEST_ASSERT_EQUAL_UINT32(i, fp_log_test_append_mqtt());
    }
    TEST_ASSERT_EQUAL_size_t(FP_LOG_CAPACITY, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY + 8));
    TEST_ASSERT_EQUAL_UINT32(205, fp_log_test_out[0].seq);
    TEST_ASSERT_EQUAL_UINT32(6, fp_log_test_out[FP_LOG_CAPACITY - 1].seq);

    // Record 205 took the slot of record 5.
    fp_log_blob_t blob;
    size_t len = sizeof(blob);
    TEST_ASSERT_TRUE(nvs_fake_peek_blob("e5", &blob, &len));
    TEST_ASSERT_EQUAL_UINT32(205, blob.seq);
    uint32_t next = 0;
    TEST_ASSERT_TRUE(nvs_fake_peek_u32("next", &next));
    TEST_ASSERT_EQUAL_UINT32(206, next);
}

static void test_mqtt_record_is_readable_after_a_reboot(void)
{
    fp_log_init();
    fp_log_test_append_mqtt();
    fp_log_test_reboot();
    fp_log_init();
    TEST_ASSERT_EQUAL_size_t(1, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY));
    TEST_ASSERT_EQUAL_INT(FP_ACS_SOURCE_MQTT, fp_log_test_out[0].source);
}


// ── Across reboots ───────────────────────────────────────────────────────────────────────

static void test_records_and_seq_survive_a_reboot(void)
{
    fp_log_init();
    fp_acs_event_t bot = fp_log_test_event(FP_ACS_SOURCE_TELEGRAM, FP_ACS_RESULT_GRANTED, 2, "Олег", "Офис");
    fp_log_append(&bot);
    fp_log_test_append_mqtt();
    fp_log_test_append_mqtt();

    fp_log_test_reboot();
    fp_log_init();
    TEST_ASSERT_EQUAL_size_t(3, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY));
    TEST_ASSERT_EQUAL_UINT32(1, fp_log_test_out[2].seq);
    TEST_ASSERT_EQUAL_STRING("Олег", fp_log_test_out[2].user_name);
    TEST_ASSERT_EQUAL_STRING("Офис", fp_log_test_out[2].chat);
    TEST_ASSERT_EQUAL_UINT32(4, fp_log_test_append_mqtt());
}

static void test_next_ahead_of_the_records_is_kept(void)
{
    fp_log_test_seed(3);
    nvs_fake_put_u32("next", 50);
    fp_log_init();
    TEST_ASSERT_EQUAL_UINT32(50, fp_log_test_append_mqtt());
}

static void test_record_written_before_a_power_loss_moves_next_past_it(void)
{
    // The record went in, the "next" after it did not.
    fp_log_test_seed(6);
    fp_log_test_seed(7);
    nvs_fake_put_u32("next", 7);
    fp_log_init();
    TEST_ASSERT_EQUAL_UINT32(8, fp_log_test_append_mqtt());
}

static void test_missing_next_follows_the_newest_record(void)
{
    fp_log_test_seed(41);
    fp_log_init();
    TEST_ASSERT_EQUAL_UINT32(42, fp_log_test_append_mqtt());
}

static void test_unreadable_next_follows_the_newest_record(void)
{
    fp_log_test_seed(41);
    nvs_fake_put_u32("next", 60);
    nvs_fake_get_u32_result = ESP_ERR_NVS_TYPE_MISMATCH;
    fp_log_init();
    TEST_ASSERT_EQUAL_UINT32(42, fp_log_test_append_mqtt());
    TEST_ASSERT_NOT_NULL(mock_log_find("\"next\" is unreadable"));
}

static void test_version_1_records_are_skipped_and_counted_once(void)
{
    for (uint32_t seq = 1; seq <= 3; seq++) {
        fp_log_test_blob_v1_t old;
        memset(&old, 0, sizeof(old));
        old.version = 1;
        old.source = FP_ACS_SOURCE_TELEGRAM;
        old.seq = seq;
        old.time = FP_LOG_TEST_NOW;
        char key[FP_LOG_TEST_KEY_SIZE];
        fp_log_test_key(seq, key);
        nvs_fake_put_blob(key, &old, sizeof(old));
    }
    fp_log_test_seed(4);
    nvs_fake_put_u32("next", 5);

    fp_log_init();
    TEST_ASSERT_EQUAL_size_t(1, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY));
    TEST_ASSERT_EQUAL_UINT32(4, fp_log_test_out[0].seq);
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_level(ESP_LOG_WARN, "3 records are unreadable"));
    TEST_ASSERT_EQUAL_UINT32(5, fp_log_test_append_mqtt());
}

static void test_version_1_marked_record_of_the_new_size_is_skipped(void)
{
    fp_log_blob_t blob = fp_log_test_blob(2, FP_ACS_SOURCE_FINGERPRINT, "", "");
    blob.version = 1;
    fp_log_test_seed_blob(&blob);
    fp_log_init();
    TEST_ASSERT_EQUAL_size_t(0, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY));
    TEST_ASSERT_NOT_NULL(mock_log_find("1 records are unreadable"));
}

static void test_damaged_records_are_skipped_and_counted(void)
{
    fp_log_blob_t blob;

    blob = fp_log_test_blob(7, FP_ACS_SOURCE_FINGERPRINT, "", "");
    nvs_fake_put_blob("e5", &blob, sizeof(blob));               // a seq that belongs to another slot

    blob = fp_log_test_blob(0, FP_ACS_SOURCE_FINGERPRINT, "", "");
    nvs_fake_put_blob("e0", &blob, sizeof(blob));               // seq 0 is never handed out

    blob = fp_log_test_blob(10, FP_ACS_SOURCE_FINGERPRINT, "", "");
    blob.source = FP_ACS_SOURCE_MQTT + 1;
    fp_log_test_seed_blob(&blob);

    blob = fp_log_test_blob(11, FP_ACS_SOURCE_FINGERPRINT, "", "");
    blob.result = FP_ACS_RESULT_UNKNOWN + 1;
    fp_log_test_seed_blob(&blob);

    blob = fp_log_test_blob(12, FP_ACS_SOURCE_FINGERPRINT, "", "");
    memset(blob.user_name, 'x', sizeof(blob.user_name));        // no terminator
    fp_log_test_seed_blob(&blob);

    blob = fp_log_test_blob(13, FP_ACS_SOURCE_TELEGRAM, "", "");
    memset(blob.chat, 'y', sizeof(blob.chat));                  // no terminator
    fp_log_test_seed_blob(&blob);

    uint8_t big[NVS_FAKE_BLOB_MAX] = {0};
    nvs_fake_put_blob("e14", big, sizeof(big));                 // larger than a record

    fp_log_test_seed(15);

    fp_log_init();
    TEST_ASSERT_EQUAL_size_t(1, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY));
    TEST_ASSERT_EQUAL_UINT32(15, fp_log_test_out[0].seq);
    TEST_ASSERT_NOT_NULL(mock_log_find("7 records are unreadable"));
}

static void test_slot_still_holding_a_record_a_lap_old_is_not_shown(void)
{
    // Record 205 never made it into slot 5, which still has record 5.
    fp_log_test_seed(5);
    fp_log_test_seed(207);
    nvs_fake_put_u32("next", 208);
    fp_log_init();
    TEST_ASSERT_EQUAL_size_t(1, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY));
    TEST_ASSERT_EQUAL_UINT32(207, fp_log_test_out[0].seq);
}

static void test_second_init_does_not_reload(void)
{
    fp_log_init();
    fp_log_test_append_mqtt();
    fp_log_test_seed(100);
    fp_log_init();
    TEST_ASSERT_EQUAL_INT(1, nvs_fake_open_calls);
    TEST_ASSERT_EQUAL_UINT32(2, fp_log_test_append_mqtt());
}


// ── Without NVS ──────────────────────────────────────────────────────────────────────────

static void test_without_the_partition_the_log_is_kept_in_ram(void)
{
    nvs_fake_open_result = ESP_ERR_NVS_NOT_FOUND;
    fp_log_init();
    TEST_ASSERT_NOT_NULL(mock_log_find("kept in RAM only"));
    TEST_ASSERT_EQUAL_UINT32(1, fp_log_test_append_mqtt());
    TEST_ASSERT_EQUAL_UINT32(2, fp_log_test_append_mqtt());
    TEST_ASSERT_EQUAL_size_t(2, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY));
    TEST_ASSERT_EQUAL_INT(0, nvs_fake_set_blob_calls);
}

static void test_record_whose_write_failed_is_kept_in_ram(void)
{
    fp_log_init();
    nvs_fake_set_blob_result = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    TEST_ASSERT_EQUAL_UINT32(1, fp_log_test_append_mqtt());
    TEST_ASSERT_NOT_NULL(mock_log_find("Writing record 1 failed (ESP_ERR_NVS_NOT_ENOUGH_SPACE)"));
    TEST_ASSERT_EQUAL_size_t(1, fp_log_snapshot(fp_log_test_out, FP_LOG_CAPACITY));
    TEST_ASSERT_EQUAL_UINT32(2, fp_log_test_append_mqtt());
}

static void test_seq_keeps_growing_across_a_reboot_after_a_failed_write(void)
{
    // fp_log.h: "seq is persistent and keeps growing across reboots"; fp_acs.c relies on it so that
    // a page that outlived a reboot never takes an event of this boot for one of the last. When the
    // write of the newest record fails, "next" in NVS stays at that record's seq, and after a
    // reboot the next event is handed the same seq again.
    TEST_IGNORE_MESSAGE("BUG: a seq whose record failed to reach NVS is handed out again after a reboot");

    fp_log_init();
    TEST_ASSERT_EQUAL_UINT32(1, fp_log_test_append_mqtt());
    nvs_fake_set_blob_result = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    TEST_ASSERT_EQUAL_UINT32(2, fp_log_test_append_mqtt());
    nvs_fake_set_blob_result = ESP_OK;

    fp_log_test_reboot();
    fp_log_init();
    TEST_ASSERT_GREATER_THAN_UINT32(2, fp_log_test_append_mqtt());
}


// ── Clock ────────────────────────────────────────────────────────────────────────────────

static void test_clock_start_asks_pool_ntp_org(void)
{
    fp_log_clock_start();
    TEST_ASSERT_EQUAL_INT(1, mock_sntp_init_calls);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", mock_sntp_server);
}

static void test_clock_that_does_not_start_is_logged(void)
{
    mock_sntp_init_result = ESP_ERR_NO_MEM;
    fp_log_clock_start();
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_level(ESP_LOG_ERROR, "Starting SNTP failed (ESP_ERR_NO_MEM)"));
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_log_does_nothing_before_init);
    RUN_TEST(test_init_without_memory_leaves_the_log_off);
    RUN_TEST(test_init_without_a_mutex_leaves_the_log_off);
    RUN_TEST(test_append_of_no_event_returns_0);

    RUN_TEST(test_record_is_version_2_in_96_bytes_without_padding);
    RUN_TEST(test_record_is_written_byte_for_byte_in_the_version_2_layout);

    RUN_TEST(test_init_on_an_empty_partition_starts_at_seq_1);
    RUN_TEST(test_record_keeps_every_field_of_the_event);
    RUN_TEST(test_clock_not_set_yet_stamps_time_unknown);
    RUN_TEST(test_snapshot_is_newest_first_and_stops_at_max);
    RUN_TEST(test_ring_keeps_the_last_200_records);
    RUN_TEST(test_mqtt_record_is_readable_after_a_reboot);

    RUN_TEST(test_records_and_seq_survive_a_reboot);
    RUN_TEST(test_next_ahead_of_the_records_is_kept);
    RUN_TEST(test_record_written_before_a_power_loss_moves_next_past_it);
    RUN_TEST(test_missing_next_follows_the_newest_record);
    RUN_TEST(test_unreadable_next_follows_the_newest_record);
    RUN_TEST(test_version_1_records_are_skipped_and_counted_once);
    RUN_TEST(test_version_1_marked_record_of_the_new_size_is_skipped);
    RUN_TEST(test_damaged_records_are_skipped_and_counted);
    RUN_TEST(test_slot_still_holding_a_record_a_lap_old_is_not_shown);
    RUN_TEST(test_second_init_does_not_reload);

    RUN_TEST(test_without_the_partition_the_log_is_kept_in_ram);
    RUN_TEST(test_record_whose_write_failed_is_kept_in_ram);
    RUN_TEST(test_seq_keeps_growing_across_a_reboot_after_a_failed_write);

    RUN_TEST(test_clock_start_asks_pool_ntp_org);
    RUN_TEST(test_clock_that_does_not_start_is_logged);

    return UNITY_END();
}
