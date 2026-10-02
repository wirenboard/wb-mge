#include "unity.h"

/*
 * wb_device: the Wiren Board MQTT convention of the access control (main/mqtt/wb_device.h).
 *
 * Underneath is a recording transport: every message handed to publish() is kept with its topic,
 * payload and retain flag, in order, and can be made to fail. Above are the store, fp_acs and the
 * lock as fakes. wb_device_init() in setUp() forgets everything wb_device knew, so every test
 * starts as the first session after a boot.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "fp_acs.h"
#include "fp_acs_fake.h"
#include "fp_lock_fake.h"
#include "fp_store.h"
#include "fp_store_fake.h"
#include "trace.h"
#include "wb_device.h"

#if !FP_ACS_ENABLED
    #error "wb_device_test needs FP_ACS_ENABLED: check DEFS in the Makefile"
#endif

#define WB_TEST_DEVICE              "fp-door"
#define WB_TEST_PREFIX              "/devices/" WB_TEST_DEVICE
#define WB_TEST_CONTROL(c)          WB_TEST_PREFIX "/controls/" c

#define WB_TEST_MSGS_MAX            512
#define WB_TEST_PAYLOAD_MAX         512
#define WB_TEST_SUBSCRIPTIONS_MAX   4
#define WB_TEST_FIXED_CONTROLS      6
// A session with no users, as the first after a start: device meta and error, six control metas,
// five values (the pushbutton has none), and three empty messages for each of the 100 user ids.
#define WB_TEST_FIRST_SESSION_MSGS  (2 + WB_TEST_FIXED_CONTROLS + (WB_TEST_FIXED_CONTROLS - 1) + \
                                     (FP_STORE_USERS_MAX * 3))

typedef struct {
    char topic[WB_DEVICE_TOPIC_MAX_LEN + 16];
    char payload[WB_TEST_PAYLOAD_MAX];
    bool retain;
} wb_test_msg_t;

static wb_test_msg_t wb_test_msgs[WB_TEST_MSGS_MAX];
static int           wb_test_msg_count;
static int           wb_test_failed_publishes;
static char          wb_test_subscriptions[WB_TEST_SUBSCRIPTIONS_MAX][WB_DEVICE_TOPIC_MAX_LEN + 16];
static int           wb_test_subscription_count;
static int           wb_test_ctx;
static bool          wb_test_wrong_ctx;
// Publishes to a topic that contains this fail, while wb_test_fail_left is not zero (-1: always).
static const char   *wb_test_fail_topic;
static int           wb_test_fail_left;

static esp_err_t wb_test_publish(void *ctx, const char *topic, const char *payload, bool retain)
{
    if (ctx != &wb_test_ctx) {
        wb_test_wrong_ctx = true;
    }
    if ((wb_test_fail_topic != NULL) && (wb_test_fail_left != 0) && (strstr(topic, wb_test_fail_topic) != NULL)) {
        if (wb_test_fail_left > 0) {
            wb_test_fail_left--;
        }
        wb_test_failed_publishes++;
        return ESP_FAIL;
    }
    TEST_ASSERT_LESS_THAN_INT_MESSAGE(WB_TEST_MSGS_MAX, wb_test_msg_count, "too many messages for the test");
    wb_test_msg_t *msg = &wb_test_msgs[wb_test_msg_count++];
    snprintf(msg->topic, sizeof(msg->topic), "%s", topic);
    snprintf(msg->payload, sizeof(msg->payload), "%s", payload);
    msg->retain = retain;
    return ESP_OK;
}

static esp_err_t wb_test_subscribe(void *ctx, const char *topic)
{
    if (ctx != &wb_test_ctx) {
        wb_test_wrong_ctx = true;
    }
    if (wb_test_subscription_count < WB_TEST_SUBSCRIPTIONS_MAX) {
        snprintf(wb_test_subscriptions[wb_test_subscription_count], sizeof(wb_test_subscriptions[0]), "%s", topic);
    }
    wb_test_subscription_count++;
    // A subscription after which anything was published would be in the middle of the session.
    trace_add("subscribe@%d", wb_test_msg_count);
    return ESP_OK;
}

static const wb_device_transport_t wb_test_transport = {
    .publish = wb_test_publish,
    .subscribe = wb_test_subscribe,
    .ctx = &wb_test_ctx,
};

static void wb_test_clear_msgs(void)
{
    memset(wb_test_msgs, 0, sizeof(wb_test_msgs));
    wb_test_msg_count = 0;
    wb_test_failed_publishes = 0;
    wb_test_subscription_count = 0;
    memset(wb_test_subscriptions, 0, sizeof(wb_test_subscriptions));
    trace_reset();
}

// Index of the first (or last) message to topic, -1 when there is none.
static int wb_test_first(const char *topic)
{
    for (int i = 0; i < wb_test_msg_count; i++) {
        if (strcmp(wb_test_msgs[i].topic, topic) == 0) {
            return i;
        }
    }
    return -1;
}

static int wb_test_last(const char *topic)
{
    for (int i = wb_test_msg_count - 1; i >= 0; i--) {
        if (strcmp(wb_test_msgs[i].topic, topic) == 0) {
            return i;
        }
    }
    return -1;
}

static int wb_test_count(const char *topic)
{
    int count = 0;
    for (int i = 0; i < wb_test_msg_count; i++) {
        if (strcmp(wb_test_msgs[i].topic, topic) == 0) {
            count++;
        }
    }
    return count;
}

// The payload last published to topic; fails the test when nothing was.
static const char *wb_test_payload(const char *topic)
{
    int i = wb_test_last(topic);
    TEST_ASSERT_TRUE_MESSAGE(i >= 0, topic);
    return wb_test_msgs[i].payload;
}

// The messages from index start on, one "<topic below the device>=<payload>" per line, for a
// whole-sequence compare.
static const char *wb_test_topics_from(int start)
{
    static char out[8192];
    out[0] = '\0';
    size_t used = 0;
    for (int i = start; (i < wb_test_msg_count) && (used < sizeof(out)); i++) {
        int n = snprintf(out + used, sizeof(out) - used, "%s%s=%s\n",
                         wb_test_msgs[i].topic + strlen(WB_TEST_PREFIX), wb_test_msgs[i].retain ? "" : "(not retained)",
                         wb_test_msgs[i].payload);
        used += (size_t)n;
    }
    return out;
}

static const char *wb_test_topics(void)
{
    return wb_test_topics_from(0);
}

// A whole first session, then a clean slate for what the test does next.
static void wb_test_session(void)
{
    wb_device_publish_all();
    wb_test_clear_msgs();
    mock_log_reset();
}

static void wb_test_set_event(uint32_t seq, fp_acs_result_t result, fp_acs_source_t source, const char *user)
{
    fp_acs_fake_has_event = true;
    memset(&fp_acs_fake_event, 0, sizeof(fp_acs_fake_event));
    fp_acs_fake_event.seq = seq;
    fp_acs_fake_event.result = result;
    fp_acs_fake_event.source = source;
    snprintf(fp_acs_fake_event.user_name, sizeof(fp_acs_fake_event.user_name), "%s", user);
}

static esp_err_t wb_test_command(const char *topic, const char *payload)
{
    return wb_device_handle_command(topic, (int)strlen(topic), payload, (int)strlen(payload));
}

// Every number in a meta is a whole one, and Unity here is built without doubles.
#define WB_TEST_ASSERT_NUMBER(expected, actual) do {                                                    \
        TEST_ASSERT_TRUE_MESSAGE((actual) == (double)(int64_t)(actual), "not a whole number");          \
        TEST_ASSERT_EQUAL_INT64((int64_t)(expected), (int64_t)(actual));                               \
    } while (0)

// The fields of a published control meta.
typedef struct {
    char   type[16];
    bool   readonly;
    bool   has_readonly;
    double order;
    bool   has_min;
    double min;
    bool   has_max;
    double max;
    bool   has_precision;
    double precision;
    char   title_en[64];
    char   title_ru[128];
    int    fields;
} wb_test_meta_t;

static wb_test_meta_t wb_test_meta(const char *topic)
{
    wb_test_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    cJSON *root = cJSON_Parse(wb_test_payload(topic));
    TEST_ASSERT_NOT_NULL_MESSAGE(root, topic);
    meta.fields = cJSON_GetArraySize(root);

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "type");
    TEST_ASSERT_TRUE(cJSON_IsString(item));
    snprintf(meta.type, sizeof(meta.type), "%s", item->valuestring);
    item = cJSON_GetObjectItemCaseSensitive(root, "readonly");
    meta.has_readonly = cJSON_IsBool(item);
    meta.readonly = cJSON_IsTrue(item);
    item = cJSON_GetObjectItemCaseSensitive(root, "order");
    TEST_ASSERT_TRUE(cJSON_IsNumber(item));
    meta.order = item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(root, "min");
    meta.has_min = cJSON_IsNumber(item);
    meta.min = meta.has_min ? item->valuedouble : 0;
    item = cJSON_GetObjectItemCaseSensitive(root, "max");
    meta.has_max = cJSON_IsNumber(item);
    meta.max = meta.has_max ? item->valuedouble : 0;
    item = cJSON_GetObjectItemCaseSensitive(root, "precision");
    meta.has_precision = cJSON_IsNumber(item);
    meta.precision = meta.has_precision ? item->valuedouble : 0;

    const cJSON *title = cJSON_GetObjectItemCaseSensitive(root, "title");
    TEST_ASSERT_TRUE(cJSON_IsObject(title));
    item = cJSON_GetObjectItemCaseSensitive(title, "en");
    TEST_ASSERT_TRUE(cJSON_IsString(item));
    snprintf(meta.title_en, sizeof(meta.title_en), "%s", item->valuestring);
    item = cJSON_GetObjectItemCaseSensitive(title, "ru");
    TEST_ASSERT_TRUE(cJSON_IsString(item));
    snprintf(meta.title_ru, sizeof(meta.title_ru), "%s", item->valuestring);

    cJSON_Delete(root);
    return meta;
}

void setUp(void)
{
    wb_test_clear_msgs();
    wb_test_fail_topic = NULL;
    wb_test_fail_left = 0;
    wb_test_wrong_ctx = false;
    fp_store_fake_reset();
    fp_acs_fake_reset();
    fp_lock_fake_reset();
    mock_heap_caps_reset();
    mock_esp_mac_reset();
    mock_log_reset();
    wb_device_init(WB_TEST_DEVICE, &wb_test_transport);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE_MESSAGE(wb_test_wrong_ctx, "the transport was called with another ctx");
}


// ── Device id and the error topic ────────────────────────────────────────────────────────

static void test_default_id_is_the_model_and_the_last_three_mac_bytes(void)
{
    const uint8_t mac[MAC_ADDRESS_SIZE] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E};
    memcpy(mock_mac_address, mac, sizeof(mac));
    char id[WB_DEVICE_ID_MAX_LEN];
    wb_device_build_default_id(id, sizeof(id));
    TEST_ASSERT_EQUAL_STRING(BASE_HOSTNAME "-3C4D5E", id);
}

static void test_default_id_without_a_mac_is_the_bare_model(void)
{
    mock_esp_read_mac_return = ESP_FAIL;
    char id[WB_DEVICE_ID_MAX_LEN];
    wb_device_build_default_id(id, sizeof(id));
    TEST_ASSERT_EQUAL_STRING(BASE_HOSTNAME, id);
}

static void test_default_id_needs_a_buffer(void)
{
    char id[4] = "abc";
    wb_device_build_default_id(NULL, sizeof(id));
    wb_device_build_default_id(id, 0);
    TEST_ASSERT_EQUAL_STRING("abc", id);
    TEST_ASSERT_EQUAL_INT(0, mock_esp_read_mac_called);
}

static void test_error_topic_of_a_device(void)
{
    char topic[WB_DEVICE_TOPIC_MAX_LEN];
    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_device_topic_error(WB_TEST_DEVICE, topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING(WB_TEST_PREFIX "/meta/error", topic);
}

static void test_error_topic_is_refused_rather_than_cut(void)
{
    char topic[WB_DEVICE_TOPIC_MAX_LEN];
    memset(topic, '#', sizeof(topic));
    size_t exact = strlen(WB_TEST_PREFIX "/meta/error");
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_SIZE, wb_device_topic_error(WB_TEST_DEVICE, topic, exact));
    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_device_topic_error(WB_TEST_DEVICE, topic, exact + 1));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, wb_device_topic_error("", topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, wb_device_topic_error(NULL, topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, wb_device_topic_error(WB_TEST_DEVICE, NULL, sizeof(topic)));
}


// ── A full session ───────────────────────────────────────────────────────────────────────

static void test_session_starts_with_the_device_meta_and_clears_the_will(void)
{
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_STRING(WB_TEST_PREFIX "/meta", wb_test_msgs[0].topic);
    TEST_ASSERT_EQUAL_STRING("{\"driver\":\"wb-mge-fp-acs\",\"title\":{\"en\":\"Fingerprint access\","
                             "\"ru\":\"Доступ по отпечатку\"}}", wb_test_msgs[0].payload);
    TEST_ASSERT_EQUAL_STRING(WB_TEST_PREFIX "/meta/error", wb_test_msgs[1].topic);
    TEST_ASSERT_EQUAL_STRING("", wb_test_msgs[1].payload);
}

static void test_session_publishes_the_fixed_controls_in_order(void)
{
    fp_acs_fake_online = true;
    wb_test_set_event(42, FP_ACS_RESULT_GRANTED, FP_ACS_SOURCE_FINGERPRINT, "Anna");
    wb_device_publish_all();
    // The device meta and error come first (checked above); the user controls after these.
    wb_test_msg_count = 2 + WB_TEST_FIXED_CONTROLS + (WB_TEST_FIXED_CONTROLS - 1);
    const char *expected =
        "/controls/open/meta=\n"
        "/controls/sensor_online/meta=\n"
        "/controls/sensor_online=1\n"
        "/controls/last_result/meta=\n"
        "/controls/last_result=granted\n"
        "/controls/last_user/meta=\n"
        "/controls/last_user=Anna\n"
        "/controls/last_source/meta=\n"
        "/controls/last_source=fingerprint\n"
        "/controls/event_count/meta=\n"
        "/controls/event_count=42\n";
    // The metas are compared field by field below; here only the order and the values count.
    for (int i = 2; i < wb_test_msg_count; i++) {
        size_t len = strlen(wb_test_msgs[i].topic);
        if ((len > 5) && (strcmp(wb_test_msgs[i].topic + len - 5, "/meta") == 0)) {
            wb_test_msgs[i].payload[0] = '\0';
        }
    }
    TEST_ASSERT_EQUAL_STRING(expected, wb_test_topics_from(2));
}

static void test_every_message_of_a_session_is_retained_and_none_is_a_command(void)
{
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_INT(WB_TEST_FIRST_SESSION_MSGS - 3 + 2, wb_test_msg_count);
    for (int i = 0; i < wb_test_msg_count; i++) {
        TEST_ASSERT_TRUE_MESSAGE(wb_test_msgs[i].retain, wb_test_msgs[i].topic);
        size_t len = strlen(wb_test_msgs[i].topic);
        TEST_ASSERT_FALSE_MESSAGE((len >= 3) && (strcmp(wb_test_msgs[i].topic + len - 3, "/on") == 0),
                                  wb_test_msgs[i].topic);
    }
}

static void test_session_ends_with_one_wildcard_subscription(void)
{
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_INT(1, wb_test_subscription_count);
    TEST_ASSERT_EQUAL_STRING(WB_TEST_PREFIX "/controls/+/on", wb_test_subscriptions[0]);
    char expected[32];
    snprintf(expected, sizeof(expected), "subscribe@%d", wb_test_msg_count);
    TEST_ASSERT_EQUAL_STRING(expected, trace_get());
}

static void test_session_without_a_subscribe_call_still_publishes(void)
{
    const wb_device_transport_t publish_only = {.publish = wb_test_publish, .subscribe = NULL, .ctx = &wb_test_ctx};
    wb_device_init(WB_TEST_DEVICE, &publish_only);
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_INT(WB_TEST_FIRST_SESSION_MSGS, wb_test_msg_count);
    TEST_ASSERT_EQUAL_INT(0, wb_test_subscription_count);
}

static void test_without_a_publish_call_nothing_is_sent(void)
{
    const wb_device_transport_t nothing = {.publish = NULL, .subscribe = NULL, .ctx = NULL};
    wb_device_init(WB_TEST_DEVICE, &nothing);
    wb_device_publish_all();
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_test_command(WB_TEST_CONTROL("open/on"), "1"));
    TEST_ASSERT_EQUAL_INT(0, wb_test_msg_count);
}

static void test_transport_is_copied(void)
{
    wb_device_transport_t on_stack = wb_test_transport;
    wb_device_init(WB_TEST_DEVICE, &on_stack);
    memset(&on_stack, 0, sizeof(on_stack));
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_INT(WB_TEST_FIRST_SESSION_MSGS, wb_test_msg_count);
}

static void test_init_without_an_id_or_a_transport_changes_nothing(void)
{
    wb_device_init(NULL, &wb_test_transport);
    wb_device_init("other", NULL);
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_STRING(WB_TEST_PREFIX "/meta", wb_test_msgs[0].topic);
}

static void test_longest_device_id_fits_every_topic(void)
{
    char id[WB_DEVICE_ID_MAX_LEN + 8];
    memset(id, 'd', sizeof(id));
    id[sizeof(id) - 1] = '\0';
    wb_device_init(id, &wb_test_transport);
    fp_store_fake_add_user(100, "Last", true, true, false, NULL);
    wb_device_publish_all();
    TEST_ASSERT_NULL(mock_log_find("does not fit"));
    // The id is cut to what the device keeps of it.
    char expected[WB_DEVICE_TOPIC_MAX_LEN];
    snprintf(expected, sizeof(expected), "/devices/%.*s/meta", WB_DEVICE_ID_MAX_LEN - 1, id);
    TEST_ASSERT_EQUAL_STRING(expected, wb_test_msgs[0].topic);
    snprintf(expected, sizeof(expected), "/devices/%.*s/controls/user_100/meta", WB_DEVICE_ID_MAX_LEN - 1, id);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, wb_test_first(expected));
}


// ── Control metas ────────────────────────────────────────────────────────────────────────

static void test_open_is_a_writable_pushbutton_without_a_value(void)
{
    wb_device_publish_all();
    wb_test_meta_t meta = wb_test_meta(WB_TEST_CONTROL("open/meta"));
    TEST_ASSERT_EQUAL_STRING("pushbutton", meta.type);
    TEST_ASSERT_TRUE(meta.has_readonly);
    TEST_ASSERT_FALSE(meta.readonly);
    WB_TEST_ASSERT_NUMBER(1, meta.order);
    TEST_ASSERT_FALSE(meta.has_min);
    TEST_ASSERT_FALSE(meta.has_max);
    TEST_ASSERT_FALSE(meta.has_precision);
    TEST_ASSERT_EQUAL_STRING("Open door", meta.title_en);
    TEST_ASSERT_EQUAL_STRING("Открыть дверь", meta.title_ru);
    TEST_ASSERT_EQUAL_INT(-1, wb_test_first(WB_TEST_CONTROL("open")));
}

static void test_sensor_online_is_a_read_only_switch_with_its_range(void)
{
    wb_device_publish_all();
    wb_test_meta_t meta = wb_test_meta(WB_TEST_CONTROL("sensor_online/meta"));
    TEST_ASSERT_EQUAL_STRING("switch", meta.type);
    TEST_ASSERT_TRUE(meta.has_readonly && meta.readonly);
    WB_TEST_ASSERT_NUMBER(2, meta.order);
    TEST_ASSERT_TRUE(meta.has_min && meta.has_max);
    WB_TEST_ASSERT_NUMBER(0, meta.min);
    WB_TEST_ASSERT_NUMBER(1, meta.max);
    TEST_ASSERT_FALSE(meta.has_precision);
    TEST_ASSERT_EQUAL_STRING("Sensor online", meta.title_en);
    TEST_ASSERT_EQUAL_STRING("Связь со сканером", meta.title_ru);
}

static void test_text_controls_are_read_only_without_a_range(void)
{
    wb_device_publish_all();
    const char *topics[] = {WB_TEST_CONTROL("last_result/meta"), WB_TEST_CONTROL("last_user/meta"),
                            WB_TEST_CONTROL("last_source/meta")};
    const char *titles[] = {"Last result", "Last user", "Last source"};
    for (int i = 0; i < 3; i++) {
        wb_test_meta_t meta = wb_test_meta(topics[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("text", meta.type, topics[i]);
        TEST_ASSERT_TRUE_MESSAGE(meta.has_readonly && meta.readonly, topics[i]);
        WB_TEST_ASSERT_NUMBER(3 + i, meta.order);
        TEST_ASSERT_FALSE_MESSAGE(meta.has_min || meta.has_max || meta.has_precision, topics[i]);
        TEST_ASSERT_EQUAL_STRING(titles[i], meta.title_en);
    }
    TEST_ASSERT_EQUAL_STRING("Последний результат", wb_test_meta(topics[0]).title_ru);
    TEST_ASSERT_EQUAL_STRING("Последний пользователь", wb_test_meta(topics[1]).title_ru);
    TEST_ASSERT_EQUAL_STRING("Последний способ", wb_test_meta(topics[2]).title_ru);
}

static void test_event_count_is_a_read_only_value_over_the_whole_uint32_range(void)
{
    wb_device_publish_all();
    wb_test_meta_t meta = wb_test_meta(WB_TEST_CONTROL("event_count/meta"));
    TEST_ASSERT_EQUAL_STRING("value", meta.type);
    TEST_ASSERT_TRUE(meta.has_readonly && meta.readonly);
    WB_TEST_ASSERT_NUMBER(6, meta.order);
    WB_TEST_ASSERT_NUMBER(0, meta.min);
    WB_TEST_ASSERT_NUMBER(4294967295.0, meta.max);
    TEST_ASSERT_TRUE(meta.has_precision);
    WB_TEST_ASSERT_NUMBER(1, meta.precision);
    TEST_ASSERT_EQUAL_STRING("Счётчик событий", meta.title_ru);
    // Printed as an integer, not as 4.29e9 that a panel would round.
    TEST_ASSERT_NOT_NULL(strstr(wb_test_payload(WB_TEST_CONTROL("event_count/meta")), "\"max\":4294967295"));
}

static void test_user_control_is_a_writable_switch_titled_with_the_name(void)
{
    fp_store_fake_add_user(7, "Иван Петров", true, true, false, NULL);
    wb_device_publish_all();
    wb_test_meta_t meta = wb_test_meta(WB_TEST_CONTROL("user_7/meta"));
    TEST_ASSERT_EQUAL_STRING("switch", meta.type);
    TEST_ASSERT_TRUE(meta.has_readonly);
    TEST_ASSERT_FALSE(meta.readonly);
    WB_TEST_ASSERT_NUMBER(WB_TEST_FIXED_CONTROLS + 7, meta.order);
    WB_TEST_ASSERT_NUMBER(0, meta.min);
    WB_TEST_ASSERT_NUMBER(1, meta.max);
    TEST_ASSERT_FALSE(meta.has_precision);
    TEST_ASSERT_EQUAL_STRING("Иван Петров", meta.title_en);
    TEST_ASSERT_EQUAL_STRING("Иван Петров", meta.title_ru);
    TEST_ASSERT_EQUAL_STRING("1", wb_test_payload(WB_TEST_CONTROL("user_7")));
}

static void test_user_name_with_quotes_is_escaped_in_the_meta(void)
{
    fp_store_fake_add_user(2, "Bob \"the door\" \\", true, true, false, NULL);
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_STRING("Bob \"the door\" \\", wb_test_meta(WB_TEST_CONTROL("user_2/meta")).title_en);
}


// ── Values ───────────────────────────────────────────────────────────────────────────────

static void test_values_before_the_first_event_are_empty_and_zero(void)
{
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_STRING("0", wb_test_payload(WB_TEST_CONTROL("sensor_online")));
    TEST_ASSERT_EQUAL_STRING("", wb_test_payload(WB_TEST_CONTROL("last_result")));
    TEST_ASSERT_EQUAL_STRING("", wb_test_payload(WB_TEST_CONTROL("last_user")));
    TEST_ASSERT_EQUAL_STRING("", wb_test_payload(WB_TEST_CONTROL("last_source")));
    TEST_ASSERT_EQUAL_STRING("0", wb_test_payload(WB_TEST_CONTROL("event_count")));
}

static void test_values_of_an_event_use_the_wire_names(void)
{
    wb_test_set_event(7, FP_ACS_RESULT_DENIED, FP_ACS_SOURCE_TELEGRAM, "");
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_STRING("denied", wb_test_payload(WB_TEST_CONTROL("last_result")));
    TEST_ASSERT_EQUAL_STRING("", wb_test_payload(WB_TEST_CONTROL("last_user")));
    TEST_ASSERT_EQUAL_STRING("telegram", wb_test_payload(WB_TEST_CONTROL("last_source")));

    wb_test_set_event(UINT32_MAX, FP_ACS_RESULT_UNKNOWN, FP_ACS_SOURCE_MQTT, "Ёлка");
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_STRING("unknown", wb_test_payload(WB_TEST_CONTROL("last_result")));
    TEST_ASSERT_EQUAL_STRING("Ёлка", wb_test_payload(WB_TEST_CONTROL("last_user")));
    TEST_ASSERT_EQUAL_STRING("mqtt", wb_test_payload(WB_TEST_CONTROL("last_source")));
    TEST_ASSERT_EQUAL_STRING("4294967295", wb_test_payload(WB_TEST_CONTROL("event_count")));
}

static void test_longest_user_name_is_published_whole(void)
{
    char name[FP_STORE_NAME_MAX + 1];
    memset(name, 'n', FP_STORE_NAME_MAX);
    name[FP_STORE_NAME_MAX] = '\0';
    wb_test_set_event(1, FP_ACS_RESULT_GRANTED, FP_ACS_SOURCE_FINGERPRINT, name);
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_STRING(name, wb_test_payload(WB_TEST_CONTROL("last_user")));
}


// ── The periodic pass ────────────────────────────────────────────────────────────────────

static void test_pass_with_nothing_changed_publishes_nothing(void)
{
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_test_set_event(5, FP_ACS_RESULT_GRANTED, FP_ACS_SOURCE_FINGERPRINT, "Anna");
    wb_test_session();
    int allocs = mock_heap_caps_alloc_calls;
    wb_device_publish_changed();
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(0, wb_test_msg_count);
    // users_rev did not move: the user list is not even copied.
    TEST_ASSERT_EQUAL_INT(allocs, mock_heap_caps_alloc_calls);
}

static void test_pass_publishes_only_what_changed(void)
{
    wb_test_session();
    fp_acs_fake_online = true;
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_STRING("/controls/sensor_online=1\n", wb_test_topics());
}

static void test_new_event_ends_with_event_count(void)
{
    wb_test_set_event(5, FP_ACS_RESULT_GRANTED, FP_ACS_SOURCE_FINGERPRINT, "Anna");
    wb_test_session();
    wb_test_set_event(6, FP_ACS_RESULT_DENIED, FP_ACS_SOURCE_TELEGRAM, "Bob");
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_STRING("/controls/last_result=denied\n"
                             "/controls/last_user=Bob\n"
                             "/controls/last_source=telegram\n"
                             "/controls/event_count=6\n", wb_test_topics());
}

static void test_repeated_event_moves_only_event_count(void)
{
    // The same user opening twice: a rule on event_count must still fire.
    wb_test_set_event(5, FP_ACS_RESULT_GRANTED, FP_ACS_SOURCE_FINGERPRINT, "Anna");
    wb_test_session();
    wb_test_set_event(6, FP_ACS_RESULT_GRANTED, FP_ACS_SOURCE_FINGERPRINT, "Anna");
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_STRING("/controls/event_count=6\n", wb_test_topics());
}

static void test_value_that_failed_to_publish_is_retried_on_the_next_pass(void)
{
    wb_test_session();
    fp_acs_fake_online = true;
    wb_test_fail_topic = "sensor_online";
    wb_test_fail_left = 1;
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(1, wb_test_failed_publishes);
    TEST_ASSERT_EQUAL_INT(0, wb_test_msg_count);
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_STRING("/controls/sensor_online=1\n", wb_test_topics());
}

static void test_session_republishes_every_value_even_unchanged(void)
{
    wb_test_set_event(5, FP_ACS_RESULT_GRANTED, FP_ACS_SOURCE_FINGERPRINT, "Anna");
    wb_test_session();
    wb_device_reset();
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_INT(1, wb_test_count(WB_TEST_CONTROL("event_count")));
    TEST_ASSERT_EQUAL_INT(1, wb_test_count(WB_TEST_CONTROL("last_user")));
    TEST_ASSERT_EQUAL_INT(1, wb_test_count(WB_TEST_PREFIX "/meta"));
}

static void test_pass_after_a_reset_publishes_every_value(void)
{
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_test_session();
    wb_device_reset();
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(1, wb_test_count(WB_TEST_CONTROL("sensor_online")));
    TEST_ASSERT_EQUAL_INT(1, wb_test_count(WB_TEST_CONTROL("event_count")));
    TEST_ASSERT_EQUAL_INT(1, wb_test_count(WB_TEST_CONTROL("user_3/meta")));
    TEST_ASSERT_EQUAL_INT(1, wb_test_count(WB_TEST_CONTROL("user_3")));
}


// ── User controls ────────────────────────────────────────────────────────────────────────

static void test_first_session_clears_every_user_id_the_store_does_not_have(void)
{
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_device_publish_all();
    for (int id = 1; id <= FP_STORE_USERS_MAX; id++) {
        char value[WB_DEVICE_TOPIC_MAX_LEN];
        char error[WB_DEVICE_TOPIC_MAX_LEN];
        char meta[WB_DEVICE_TOPIC_MAX_LEN];
        snprintf(value, sizeof(value), WB_TEST_PREFIX "/controls/user_%d", id);
        snprintf(error, sizeof(error), WB_TEST_PREFIX "/controls/user_%d/meta/error", id);
        snprintf(meta, sizeof(meta), WB_TEST_PREFIX "/controls/user_%d/meta", id);
        if (id == 3) {
            TEST_ASSERT_EQUAL_INT(-1, wb_test_first(error));
            continue;
        }
        // The value, then the error, then the meta - each deleted by an empty retained message.
        int i = wb_test_first(value);
        TEST_ASSERT_TRUE_MESSAGE(i >= 0, value);
        TEST_ASSERT_EQUAL_STRING("", wb_test_msgs[i].payload);
        TEST_ASSERT_EQUAL_STRING(error, wb_test_msgs[i + 1].topic);
        TEST_ASSERT_EQUAL_STRING("", wb_test_msgs[i + 1].payload);
        TEST_ASSERT_EQUAL_STRING(meta, wb_test_msgs[i + 2].topic);
        TEST_ASSERT_EQUAL_STRING("", wb_test_msgs[i + 2].payload);
    }
    // An id nobody used is swept quietly, not reported as a deleted user.
    TEST_ASSERT_EQUAL_size_t(0, mock_log_count_level(ESP_LOG_INFO, "is gone"));
}

static void test_later_sessions_clear_nothing_more(void)
{
    wb_test_session();
    wb_device_reset();
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_INT(2 + WB_TEST_FIXED_CONTROLS + (WB_TEST_FIXED_CONTROLS - 1), wb_test_msg_count);
}

static void test_restart_of_the_client_sweeps_again(void)
{
    wb_test_session();
    wb_device_init(WB_TEST_DEVICE, &wb_test_transport);
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_INT(WB_TEST_FIRST_SESSION_MSGS, wb_test_msg_count);
}

static void test_new_user_gets_a_control_on_the_next_pass(void)
{
    wb_test_session();
    fp_store_fake_add_user(4, "Bob", false, true, false, NULL);
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(2, wb_test_msg_count);
    TEST_ASSERT_EQUAL_STRING(WB_TEST_CONTROL("user_4/meta"), wb_test_msgs[0].topic);
    TEST_ASSERT_EQUAL_STRING(WB_TEST_CONTROL("user_4"), wb_test_msgs[1].topic);
    TEST_ASSERT_EQUAL_STRING("0", wb_test_msgs[1].payload);
}

static void test_rename_republishes_only_the_meta(void)
{
    fp_store_user_t *anna = fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_test_session();
    snprintf(anna->name, sizeof(anna->name), "Anna K");
    fp_store_fake_add_slot(3, 9);                  // moves users_rev, as fp_store does on a rename
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(1, wb_test_msg_count);
    TEST_ASSERT_EQUAL_STRING("Anna K", wb_test_meta(WB_TEST_CONTROL("user_3/meta")).title_en);
}

static void test_toggle_republishes_only_the_value(void)
{
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_test_session();
    TEST_ASSERT_EQUAL_INT(ESP_OK, fp_store_set_enabled(3, false));
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_STRING("/controls/user_3=0\n", wb_test_topics());
}

static void test_deleted_user_is_cleared_once(void)
{
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_test_session();
    TEST_ASSERT_EQUAL_INT(ESP_OK, fp_store_delete_user(3));
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_STRING("/controls/user_3=\n"
                             "/controls/user_3/meta/error=\n"
                             "/controls/user_3/meta=\n", wb_test_topics());
    TEST_ASSERT_NOT_NULL(mock_log_find("User 3 is gone"));

    fp_store_fake_add_user(5, "Eve", true, true, false, NULL);
    wb_test_clear_msgs();
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(-1, wb_test_first(WB_TEST_CONTROL("user_3")));
}

static void test_user_deleted_between_sessions_is_cleared_by_the_next_one(void)
{
    // A disconnect resets wb_device; what it has on the broker is kept.
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_test_session();
    wb_device_reset();
    TEST_ASSERT_EQUAL_INT(ESP_OK, fp_store_delete_user(3));
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_STRING("", wb_test_payload(WB_TEST_CONTROL("user_3/meta")));
    TEST_ASSERT_EQUAL_INT(1, wb_test_count(WB_TEST_CONTROL("user_3/meta")));
}

static void test_failed_user_publish_is_retried_without_a_new_change(void)
{
    wb_test_session();
    fp_store_fake_add_user(4, "Bob", true, true, false, NULL);
    wb_test_fail_topic = "user_4";
    wb_test_fail_left = 1;
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(1, wb_test_failed_publishes);
    // Only the value went out; users_rev is not taken as published.
    TEST_ASSERT_EQUAL_STRING("/controls/user_4=1\n", wb_test_topics());
    wb_test_clear_msgs();
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(1, wb_test_msg_count);
    TEST_ASSERT_EQUAL_STRING(WB_TEST_CONTROL("user_4/meta"), wb_test_msgs[0].topic);
    wb_test_clear_msgs();
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(0, wb_test_msg_count);
}

static void test_failed_clearing_is_retried(void)
{
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_test_session();
    TEST_ASSERT_EQUAL_INT(ESP_OK, fp_store_delete_user(3));
    wb_test_fail_topic = "user_3/meta/error";
    wb_test_fail_left = 1;
    wb_device_publish_changed();
    wb_test_clear_msgs();
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_STRING("/controls/user_3=\n"
                             "/controls/user_3/meta/error=\n"
                             "/controls/user_3/meta=\n", wb_test_topics());
}

static void test_user_list_without_memory_waits_for_the_next_pass(void)
{
    wb_test_session();
    fp_store_fake_add_user(4, "Bob", true, true, false, NULL);
    mock_heap_caps_fail_count = 1;
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(0, wb_test_msg_count);
    TEST_ASSERT_NOT_NULL(mock_log_find("No memory for the user list"));
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(2, wb_test_msg_count);
    TEST_ASSERT_EQUAL_UINT32(MALLOC_CAP_SPIRAM, mock_heap_caps_last_caps);
}

static void test_all_hundred_users_get_a_control(void)
{
    for (int id = 1; id <= FP_STORE_USERS_MAX; id++) {
        char name[16];
        snprintf(name, sizeof(name), "U%d", id);
        fp_store_fake_add_user((uint8_t)id, name, (id % 2) == 0, true, false, NULL);
    }
    wb_device_publish_all();
    TEST_ASSERT_EQUAL_INT(2 + WB_TEST_FIXED_CONTROLS + (WB_TEST_FIXED_CONTROLS - 1) + (FP_STORE_USERS_MAX * 2),
                          wb_test_msg_count);
    TEST_ASSERT_EQUAL_STRING("0", wb_test_payload(WB_TEST_CONTROL("user_1")));
    TEST_ASSERT_EQUAL_STRING("1", wb_test_payload(WB_TEST_CONTROL("user_100")));
    WB_TEST_ASSERT_NUMBER(WB_TEST_FIXED_CONTROLS + 100, wb_test_meta(WB_TEST_CONTROL("user_100/meta")).order);
}


// ── The open pushbutton ──────────────────────────────────────────────────────────────────

static void test_open_pressed_opens_and_is_reported_as_mqtt(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_test_command(WB_TEST_CONTROL("open/on"), "1"));
    TEST_ASSERT_EQUAL_STRING("open report.mqtt", trace_get());
    TEST_ASSERT_EQUAL_INT(1, fp_acs_fake_mqtt_reports);
    // A pushbutton has no value to echo.
    TEST_ASSERT_EQUAL_INT(0, wb_test_msg_count);
}

static void test_open_takes_1_and_nothing_else(void)
{
    const char *payloads[] = {"0", "", "2", "11", "true", "on", " 1", "1 "};
    for (size_t i = 0; i < (sizeof(payloads) / sizeof(payloads[0])); i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_INVALID_ARG, wb_test_command(WB_TEST_CONTROL("open/on"), payloads[i]),
                                      payloads[i]);
    }
    // "1" followed by a NUL is two bytes on the wire, not a press.
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, wb_device_handle_command(WB_TEST_CONTROL("open/on"),
                                                                         (int)strlen(WB_TEST_CONTROL("open/on")),
                                                                         "1", 2));
    TEST_ASSERT_EQUAL_INT(0, fp_lock_fake_open_calls);
    TEST_ASSERT_EQUAL_INT(0, fp_acs_fake_mqtt_reports);
}

static void test_open_reads_only_the_given_lengths(void)
{
    // esp-mqtt does not terminate either string.
    const char topic[] = WB_TEST_CONTROL("open/on") "/garbage";
    const char payload[] = "1garbage";
    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_device_handle_command(topic, (int)strlen(WB_TEST_CONTROL("open/on")), payload, 1));
    TEST_ASSERT_EQUAL_INT(1, fp_lock_fake_open_calls);
}

static void test_open_the_lock_refuses_is_still_accepted_and_reported(void)
{
    fp_lock_fake_open_result = false;
    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_test_command(WB_TEST_CONTROL("open/on"), "1"));
    TEST_ASSERT_EQUAL_INT(1, fp_acs_fake_mqtt_reports);
    TEST_ASSERT_NOT_NULL(mock_log_find("The lock did not take the request"));
}

static void test_open_topic_must_match_exactly(void)
{
    const char *topics[] = {
        "/devices/other/controls/open/on",
        WB_TEST_CONTROL("open/on/x"),
        WB_TEST_CONTROL("opener/on"),
        WB_TEST_CONTROL("open"),
        WB_TEST_CONTROL("open/meta"),
        WB_TEST_CONTROL("Open/on"),
        "/devices/" WB_TEST_DEVICE "x/controls/open/on",
        "devices/" WB_TEST_DEVICE "/controls/open/on",
    };
    for (size_t i = 0; i < (sizeof(topics) / sizeof(topics[0])); i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_NOT_FOUND, wb_test_command(topics[i], "1"), topics[i]);
    }
    // A topic cut short is not the open topic either.
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FOUND, wb_device_handle_command(WB_TEST_CONTROL("open/on"),
                                                                       (int)strlen(WB_TEST_CONTROL("open/o")), "1", 1));
    TEST_ASSERT_EQUAL_INT(0, fp_lock_fake_open_calls);
}

static void test_read_only_controls_refuse_commands(void)
{
    const char *topics[] = {WB_TEST_CONTROL("sensor_online/on"), WB_TEST_CONTROL("last_result/on"),
                            WB_TEST_CONTROL("last_user/on"), WB_TEST_CONTROL("last_source/on"),
                            WB_TEST_CONTROL("event_count/on")};
    for (size_t i = 0; i < (sizeof(topics) / sizeof(topics[0])); i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_NOT_FOUND, wb_test_command(topics[i], "1"), topics[i]);
    }
    TEST_ASSERT_EQUAL_INT(0, wb_test_msg_count);
    TEST_ASSERT_EQUAL_INT(0, fp_lock_fake_open_calls);
}

static void test_command_needs_a_topic_and_a_payload(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, wb_device_handle_command(NULL, 10, "1", 1));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, wb_device_handle_command(WB_TEST_CONTROL("open/on"), 0, "1", 1));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, wb_device_handle_command(WB_TEST_CONTROL("open/on"), -1, "1", 1));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, wb_device_handle_command(WB_TEST_CONTROL("open/on"),
                                                                         (int)strlen(WB_TEST_CONTROL("open/on")),
                                                                         NULL, 1));
    TEST_ASSERT_EQUAL_INT(0, fp_lock_fake_open_calls);
}


// ── User switches ────────────────────────────────────────────────────────────────────────

static void test_user_switch_sets_enabled_and_echoes_the_value(void)
{
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_test_session();
    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_test_command(WB_TEST_CONTROL("user_3/on"), "0"));
    fp_store_user_t user;
    TEST_ASSERT_TRUE(fp_store_get_user(3, &user));
    TEST_ASSERT_FALSE(user.enabled);
    TEST_ASSERT_EQUAL_STRING("/controls/user_3=0\n", wb_test_topics());

    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_test_command(WB_TEST_CONTROL("user_3/on"), "1"));
    TEST_ASSERT_TRUE(fp_store_get_user(3, &user));
    TEST_ASSERT_TRUE(user.enabled);
    TEST_ASSERT_EQUAL_STRING("1", wb_test_payload(WB_TEST_CONTROL("user_3")));
}

static void test_echoed_value_is_not_published_again_by_the_next_pass(void)
{
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_test_session();
    wb_test_command(WB_TEST_CONTROL("user_3/on"), "0");
    wb_test_clear_msgs();
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_INT(0, wb_test_msg_count);
}

static void test_user_switch_takes_0_or_1_and_nothing_else(void)
{
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    const char *payloads[] = {"", "2", "01", "10", "on", "off", "true"};
    for (size_t i = 0; i < (sizeof(payloads) / sizeof(payloads[0])); i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_INVALID_ARG, wb_test_command(WB_TEST_CONTROL("user_3/on"), payloads[i]),
                                      payloads[i]);
    }
    TEST_ASSERT_EQUAL_INT(0, fp_store_fake_set_enabled_calls);
    TEST_ASSERT_EQUAL_INT(0, wb_test_msg_count);
}

static void test_switch_of_a_user_that_does_not_exist_is_not_found_and_not_echoed(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FOUND, wb_test_command(WB_TEST_CONTROL("user_9/on"), "1"));
    TEST_ASSERT_EQUAL_INT(1, fp_store_fake_set_enabled_calls);
    TEST_ASSERT_EQUAL_INT(0, wb_test_msg_count);
}

static void test_user_ids_in_a_topic_are_1_to_100_without_leading_zeros(void)
{
    fp_store_fake_add_user(1, "First", true, true, false, NULL);
    fp_store_fake_add_user(100, "Last", true, true, false, NULL);
    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_test_command(WB_TEST_CONTROL("user_1/on"), "0"));
    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_test_command(WB_TEST_CONTROL("user_100/on"), "0"));

    const char *topics[] = {
        WB_TEST_CONTROL("user_0/on"),   WB_TEST_CONTROL("user_01/on"),  WB_TEST_CONTROL("user_001/on"),
        WB_TEST_CONTROL("user_101/on"), WB_TEST_CONTROL("user_1000/on"), WB_TEST_CONTROL("user_/on"),
        WB_TEST_CONTROL("user_1x/on"),  WB_TEST_CONTROL("user_-1/on"),  WB_TEST_CONTROL("user_+1/on"),
        WB_TEST_CONTROL("user_1/off"),  WB_TEST_CONTROL("user_1"),      WB_TEST_CONTROL("users_1/on"),
        "/devices/other/controls/user_1/on",
    };
    for (size_t i = 0; i < (sizeof(topics) / sizeof(topics[0])); i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_NOT_FOUND, wb_test_command(topics[i], "1"), topics[i]);
    }
    TEST_ASSERT_EQUAL_INT(2, fp_store_fake_set_enabled_calls);
}

static void test_echo_that_fails_still_accepts_the_command_and_the_pass_publishes_it(void)
{
    fp_store_fake_add_user(3, "Anna", true, true, false, NULL);
    wb_test_session();
    wb_test_fail_topic = "user_3";
    wb_test_fail_left = 1;
    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_test_command(WB_TEST_CONTROL("user_3/on"), "0"));
    TEST_ASSERT_EQUAL_INT(0, wb_test_msg_count);
    wb_device_publish_changed();
    TEST_ASSERT_EQUAL_STRING("/controls/user_3=0\n", wb_test_topics());
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_id_is_the_model_and_the_last_three_mac_bytes);
    RUN_TEST(test_default_id_without_a_mac_is_the_bare_model);
    RUN_TEST(test_default_id_needs_a_buffer);
    RUN_TEST(test_error_topic_of_a_device);
    RUN_TEST(test_error_topic_is_refused_rather_than_cut);

    RUN_TEST(test_session_starts_with_the_device_meta_and_clears_the_will);
    RUN_TEST(test_session_publishes_the_fixed_controls_in_order);
    RUN_TEST(test_every_message_of_a_session_is_retained_and_none_is_a_command);
    RUN_TEST(test_session_ends_with_one_wildcard_subscription);
    RUN_TEST(test_session_without_a_subscribe_call_still_publishes);
    RUN_TEST(test_without_a_publish_call_nothing_is_sent);
    RUN_TEST(test_transport_is_copied);
    RUN_TEST(test_init_without_an_id_or_a_transport_changes_nothing);
    RUN_TEST(test_longest_device_id_fits_every_topic);

    RUN_TEST(test_open_is_a_writable_pushbutton_without_a_value);
    RUN_TEST(test_sensor_online_is_a_read_only_switch_with_its_range);
    RUN_TEST(test_text_controls_are_read_only_without_a_range);
    RUN_TEST(test_event_count_is_a_read_only_value_over_the_whole_uint32_range);
    RUN_TEST(test_user_control_is_a_writable_switch_titled_with_the_name);
    RUN_TEST(test_user_name_with_quotes_is_escaped_in_the_meta);

    RUN_TEST(test_values_before_the_first_event_are_empty_and_zero);
    RUN_TEST(test_values_of_an_event_use_the_wire_names);
    RUN_TEST(test_longest_user_name_is_published_whole);

    RUN_TEST(test_pass_with_nothing_changed_publishes_nothing);
    RUN_TEST(test_pass_publishes_only_what_changed);
    RUN_TEST(test_new_event_ends_with_event_count);
    RUN_TEST(test_repeated_event_moves_only_event_count);
    RUN_TEST(test_value_that_failed_to_publish_is_retried_on_the_next_pass);
    RUN_TEST(test_session_republishes_every_value_even_unchanged);
    RUN_TEST(test_pass_after_a_reset_publishes_every_value);

    RUN_TEST(test_first_session_clears_every_user_id_the_store_does_not_have);
    RUN_TEST(test_later_sessions_clear_nothing_more);
    RUN_TEST(test_restart_of_the_client_sweeps_again);
    RUN_TEST(test_new_user_gets_a_control_on_the_next_pass);
    RUN_TEST(test_rename_republishes_only_the_meta);
    RUN_TEST(test_toggle_republishes_only_the_value);
    RUN_TEST(test_deleted_user_is_cleared_once);
    RUN_TEST(test_user_deleted_between_sessions_is_cleared_by_the_next_one);
    RUN_TEST(test_failed_user_publish_is_retried_without_a_new_change);
    RUN_TEST(test_failed_clearing_is_retried);
    RUN_TEST(test_user_list_without_memory_waits_for_the_next_pass);
    RUN_TEST(test_all_hundred_users_get_a_control);

    RUN_TEST(test_open_pressed_opens_and_is_reported_as_mqtt);
    RUN_TEST(test_open_takes_1_and_nothing_else);
    RUN_TEST(test_open_reads_only_the_given_lengths);
    RUN_TEST(test_open_the_lock_refuses_is_still_accepted_and_reported);
    RUN_TEST(test_open_topic_must_match_exactly);
    RUN_TEST(test_read_only_controls_refuse_commands);
    RUN_TEST(test_command_needs_a_topic_and_a_payload);

    RUN_TEST(test_user_switch_sets_enabled_and_echoes_the_value);
    RUN_TEST(test_echoed_value_is_not_published_again_by_the_next_pass);
    RUN_TEST(test_user_switch_takes_0_or_1_and_nothing_else);
    RUN_TEST(test_switch_of_a_user_that_does_not_exist_is_not_found_and_not_echoed);
    RUN_TEST(test_user_ids_in_a_topic_are_1_to_100_without_leading_zeros);
    RUN_TEST(test_echo_that_fails_still_accepts_the_command_and_the_pass_publishes_it);

    return UNITY_END();
}
