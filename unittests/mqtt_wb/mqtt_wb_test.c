#include "unity.h"
#include "console_log.h"

#include "cJSON.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mixer.h"
#include "mock_mixer.h"
#include "mock_setting_items.h"
#include "mqtt_client.h"
#include "mqtt_client_wb.h"
#include "setting_items.h"
#include "wb_device.h"

#include <stdio.h>
#include <string.h>

#define TEST_DEVICE_ID      "WB-MGE-TEST"
#define TEST_BROKER_HOST    "10.0.0.5"
#define TEST_BROKER_PORT    "1883"

#define DEV_TOPIC           "/devices/" TEST_DEVICE_ID
#define DEV_META_TOPIC      DEV_TOPIC "/meta"
#define DEV_ERROR_TOPIC     DEV_TOPIC "/meta/error"
#define CTRL(name)          DEV_TOPIC "/controls/" name
#define CTRL_META(name)     CTRL(name) "/meta"
#define CTRL_ERROR(name)    CTRL(name) "/meta/error"
#define CTRL_CMD(name)      CTRL(name) "/on"

// Every control the device publishes, so a test can walk them without repeating the list.
static const char *const all_controls[] = {
    "enabled", "program", "state", "step", "step_left", "speed",
    "step_type", "frequency", "voltage", "current", "temperature", "vfd_online",
};
#define ALL_CONTROLS_COUNT  ((int)(sizeof(all_controls) / sizeof(all_controls[0])))

// The four whose value is read out of the drive over Modbus.
static const char *const vfd_controls[] = {"frequency", "voltage", "current", "temperature"};
#define VFD_CONTROLS_COUNT  ((int)(sizeof(vfd_controls) / sizeof(vfd_controls[0])))

static void configure_mqtt(const char *host, const char *device_id)
{
    mock_setting_items_set(KEY_MQTT_ENABLED, "true");
    mock_setting_items_set(KEY_MQTT_HOST, host);
    mock_setting_items_set(KEY_MQTT_PORT, TEST_BROKER_PORT);
    mock_setting_items_set(KEY_MQTT_USER, "");
    mock_setting_items_set(KEY_MQTT_PASS, "");
    mock_setting_items_set(KEY_MQTT_ID, device_id);

    mock_setting_items_set(KEY_MIX_P0_NAME, "Clay");
    mock_setting_items_set(KEY_MIX_P1_NAME, "Slip");
    mock_setting_items_set(KEY_MIX_P2_NAME, "Program 3");
    mock_setting_items_set(KEY_MIX_P3_NAME, "Program 4");
}

// A device that is up, connected and has published everything once. Returns with the message log
// cleared, so what a test sees afterwards is only what its own step produced.
//
// The tick is what does the publishing: the esp-mqtt event handler only hands the session over
// to the publishing task, so nothing reaches the broker until that task runs (see the lock
// comment in main/mqtt/mqtt_client_wb.c).
static void connect_and_settle(void)
{
    configure_mqtt(TEST_BROKER_HOST, TEST_DEVICE_ID);
    TEST_ASSERT_EQUAL_INT(ESP_OK, mqtt_client_wb_start());
    mock_mqtt_fire_event(MQTT_EVENT_CONNECTED, NULL, NULL);
    mqtt_client_wb_test_tick();
    mock_mqtt_message_count = 0;
    mock_mqtt_subscription_count = 0;
}

static const char *payload_of(const char *topic)
{
    const mock_mqtt_message_t *message = mock_mqtt_find_last_message(topic);
    TEST_ASSERT_NOT_NULL_MESSAGE(message, topic);
    return message->payload;
}

// Unity is built here without its floating-point assertions, so the rounding step is compared by
// hand. The values are exact tenths and hundredths, so the tolerance only has to cover the
// round trip through the JSON text.
static void assert_precision(cJSON *meta, double expected, const char *message)
{
    cJSON *precision = cJSON_GetObjectItem(meta, "precision");
    TEST_ASSERT_NOT_NULL_MESSAGE(precision, message);

    double difference = precision->valuedouble - expected;
    if (difference < 0) {
        difference = -difference;
    }
    TEST_ASSERT_TRUE_MESSAGE(difference < 1e-9, message);
}

// One field of a control's meta, as the panel would read it.
static cJSON *meta_of(wb_control_id_t id)
{
    char *json = wb_device_control_meta_json(id);
    TEST_ASSERT_NOT_NULL_MESSAGE(json, "The control meta should have been built");

    cJSON *parsed = cJSON_Parse(json);
    free(json);
    TEST_ASSERT_NOT_NULL_MESSAGE(parsed, "The control meta should be valid JSON");
    return parsed;
}

void setUp(void)
{
    mock_setting_items_reset();
    mock_mixer_reset();
    mock_mqtt_reset();
    mock_esp_mac_reset();
    mock_freertos_task_reset();
    mock_freertos_semaphore_reset();
    mock_freertos_queue_reset();
    mqtt_client_wb_test_reset();
    wb_device_reset();
}

void tearDown(void)
{

}

/* The six topics of the convention, built from a device id and a control name. Spelled out in
 * full here rather than assembled the way the code does, so a change to either side shows up. */
void test_topics(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test building the topics of the convention");
    LOG_MESSAGE();

    char topic[WB_DEVICE_TOPIC_MAX_LEN];

    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_device_topic_meta(TEST_DEVICE_ID, topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING(DEV_META_TOPIC, topic);

    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_device_topic_error(TEST_DEVICE_ID, topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING(DEV_ERROR_TOPIC, topic);

    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_device_topic_control(TEST_DEVICE_ID, "speed", topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING(CTRL("speed"), topic);

    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_device_topic_control_meta(TEST_DEVICE_ID, "speed", topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING(CTRL_META("speed"), topic);

    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_device_topic_control_error(TEST_DEVICE_ID, "speed", topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING(CTRL_ERROR("speed"), topic);

    TEST_ASSERT_EQUAL_INT(ESP_OK, wb_device_topic_control_command(TEST_DEVICE_ID, "speed", topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING(CTRL_CMD("speed"), topic);

    // A buffer that cannot hold the topic gets no topic at all rather than half of one.
    char small[8];
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_INVALID_SIZE,
                                  wb_device_topic_meta(TEST_DEVICE_ID, small, sizeof(small)),
                                  "A topic that does not fit must be refused, not truncated");

    // The device id a device that was never named answers to: the model and the last three
    // bytes of the MAC, which the mock sets to 00:01:02:03:04:05.
    char id[WB_DEVICE_ID_MAX_LEN];
    wb_device_build_default_id(id, sizeof(id));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("WB-MGE-030405", id,
                                     "The default device id comes from the model and the MAC");
}

/* The device meta: the driver name and a title in both languages. */
void test_device_meta_json(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test the device meta document");
    LOG_MESSAGE();

    char *json = wb_device_meta_json();
    TEST_ASSERT_NOT_NULL(json);

    cJSON *meta = cJSON_Parse(json);
    free(json);
    TEST_ASSERT_NOT_NULL_MESSAGE(meta, "The device meta should be valid JSON");

    TEST_ASSERT_EQUAL_STRING("wb-mge-mixer", cJSON_GetObjectItem(meta, "driver")->valuestring);

    cJSON *title = cJSON_GetObjectItem(meta, "title");
    TEST_ASSERT_NOT_NULL_MESSAGE(title, "The device meta carries a title");
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(title, "en"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(title, "ru"));

    cJSON_Delete(meta);
}

/* A control's meta, one per kind: a switch, a plain measurement, an enumeration with fixed
 * labels, and the enumeration whose labels are settings. readonly, min and max are asserted on
 * every one of them - the specification contradicts itself on their defaults, so the firmware
 * must never leave them out. */
void test_control_meta_json(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test the meta document of each kind of control");
    LOG_MESSAGE();

    configure_mqtt(TEST_BROKER_HOST, TEST_DEVICE_ID);

    for (int i = 0; i < ALL_CONTROLS_COUNT; i++) {
        cJSON *meta = meta_of((wb_control_id_t)i);

        char message[128];
        snprintf(message, sizeof(message), "Control %s must publish readonly, min, max and order",
                 all_controls[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(cJSON_GetObjectItem(meta, "readonly"), message);
        TEST_ASSERT_NOT_NULL_MESSAGE(cJSON_GetObjectItem(meta, "min"), message);
        TEST_ASSERT_NOT_NULL_MESSAGE(cJSON_GetObjectItem(meta, "max"), message);
        TEST_ASSERT_EQUAL_INT_MESSAGE(i + 1, cJSON_GetObjectItem(meta, "order")->valueint, message);

        cJSON *title = cJSON_GetObjectItem(meta, "title");
        TEST_ASSERT_NOT_NULL_MESSAGE(title, message);
        TEST_ASSERT_NOT_NULL_MESSAGE(cJSON_GetObjectItem(title, "en"), message);
        TEST_ASSERT_NOT_NULL_MESSAGE(cJSON_GetObjectItem(title, "ru"), message);

        cJSON_Delete(meta);
    }

    // A switch the panel may flip.
    cJSON *enabled = meta_of(WB_CONTROL_ENABLED);
    TEST_ASSERT_EQUAL_STRING("switch", cJSON_GetObjectItem(enabled, "type")->valuestring);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsFalse(cJSON_GetObjectItem(enabled, "readonly")),
                             "The master switch is writable");
    TEST_ASSERT_NULL_MESSAGE(cJSON_GetObjectItem(enabled, "units"), "A switch has no units");
    cJSON_Delete(enabled);

    // A measurement: units out of the convention's list, and a precision that says the value is
    // carried in tenths.
    cJSON *frequency = meta_of(WB_CONTROL_FREQUENCY);
    TEST_ASSERT_EQUAL_STRING("value", cJSON_GetObjectItem(frequency, "type")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Hz", cJSON_GetObjectItem(frequency, "units")->valuestring);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsTrue(cJSON_GetObjectItem(frequency, "readonly")),
                             "A reading from the drive is read-only");
    assert_precision(frequency, 0.1, "The frequency is carried in tenths of a hertz");
    cJSON_Delete(frequency);

    cJSON *current = meta_of(WB_CONTROL_CURRENT);
    TEST_ASSERT_EQUAL_STRING("A", cJSON_GetObjectItem(current, "units")->valuestring);
    assert_precision(current, 0.01, "The current is carried in hundredths of an ampere");
    cJSON_Delete(current);

    cJSON *temperature = meta_of(WB_CONTROL_TEMPERATURE);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("deg C", cJSON_GetObjectItem(temperature, "units")->valuestring,
                                     "Units are spelled the way the convention spells them");
    cJSON_Delete(temperature);

    cJSON *step_left = meta_of(WB_CONTROL_STEP_LEFT);
    TEST_ASSERT_EQUAL_STRING("s", cJSON_GetObjectItem(step_left, "units")->valuestring);
    cJSON_Delete(step_left);

    cJSON *speed = meta_of(WB_CONTROL_SPEED);
    TEST_ASSERT_EQUAL_STRING("rpm", cJSON_GetObjectItem(speed, "units")->valuestring);
    cJSON_Delete(speed);

    // An enumeration is a value control with an enum field, not a type of its own: what goes on
    // the bus is the number, and this table is what the panel shows instead of it.
    cJSON *state = meta_of(WB_CONTROL_STATE);
    TEST_ASSERT_EQUAL_STRING("value", cJSON_GetObjectItem(state, "type")->valuestring);
    cJSON *state_enum = cJSON_GetObjectItem(state, "enum");
    TEST_ASSERT_NOT_NULL_MESSAGE(state_enum, "The state control is an enumeration");
    TEST_ASSERT_EQUAL_STRING("Stopped", cJSON_GetObjectItem(cJSON_GetObjectItem(state_enum, "0"), "en")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Running", cJSON_GetObjectItem(cJSON_GetObjectItem(state_enum, "1"), "en")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Error", cJSON_GetObjectItem(cJSON_GetObjectItem(state_enum, "2"), "en")->valuestring);
    cJSON_Delete(state);

    cJSON *step_type = meta_of(WB_CONTROL_STEP_TYPE);
    cJSON *step_type_enum = cJSON_GetObjectItem(step_type, "enum");
    TEST_ASSERT_NOT_NULL(step_type_enum);
    TEST_ASSERT_EQUAL_STRING("Pause", cJSON_GetObjectItem(cJSON_GetObjectItem(step_type_enum, "0"), "en")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Forward", cJSON_GetObjectItem(cJSON_GetObjectItem(step_type_enum, "1"), "en")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Reverse", cJSON_GetObjectItem(cJSON_GetObjectItem(step_type_enum, "2"), "en")->valuestring);
    cJSON_Delete(step_type);

    // The program selector's labels are the mix_pN_name settings.
    cJSON *program = meta_of(WB_CONTROL_PROGRAM);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsFalse(cJSON_GetObjectItem(program, "readonly")),
                             "The program selector is writable");
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetObjectItem(program, "min")->valueint);
    TEST_ASSERT_EQUAL_INT(MIXER_PROGRAM_COUNT - 1, cJSON_GetObjectItem(program, "max")->valueint);
    cJSON *program_enum = cJSON_GetObjectItem(program, "enum");
    TEST_ASSERT_NOT_NULL(program_enum);
    TEST_ASSERT_EQUAL_STRING("Clay", cJSON_GetObjectItem(cJSON_GetObjectItem(program_enum, "0"), "en")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Slip", cJSON_GetObjectItem(cJSON_GetObjectItem(program_enum, "1"), "ru")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Program 4", cJSON_GetObjectItem(cJSON_GetObjectItem(program_enum, "3"), "en")->valuestring);
    cJSON_Delete(program);
}

/* The bring-up, in the order the convention asks for: a will registered inside CONNECT, then on
 * the CONNACK the device meta, the empty device error, and for every control its meta, its error
 * and its value - and only then the subscriptions, which cover the writable controls alone. */
void test_startup_publishes_the_whole_device(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test the order of a fresh session");
    LOG_MESSAGE();

    configure_mqtt(TEST_BROKER_HOST, TEST_DEVICE_ID);
    mock_setting_items_set(KEY_MQTT_USER, "wirenboard");
    mock_setting_items_set(KEY_MQTT_PASS, "secret");

    TEST_ASSERT_EQUAL_INT(ESP_OK, mqtt_client_wb_start());

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_init_called, "The client is created once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_start_called, "And started once");
    TEST_ASSERT_EQUAL_STRING(TEST_BROKER_HOST, mock_mqtt_config.host);
    TEST_ASSERT_EQUAL_UINT32(1883, mock_mqtt_config.port);
    TEST_ASSERT_EQUAL_STRING("wirenboard", mock_mqtt_config.username);
    TEST_ASSERT_EQUAL_STRING("secret", mock_mqtt_config.password);

    // The will is part of CONNECT and cannot be published later, so it has to be in the
    // configuration the client was built with.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(DEV_ERROR_TOPIC, mock_mqtt_config.will_topic,
                                     "The will goes to the device error topic");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("rw", mock_mqtt_config.will_msg,
                                     "A device that drops off is neither readable nor writable");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_config.will_retain, "The will is retained");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_messages_before_start,
                                  "Nothing is published before the client is started");

    mock_mixer_status.vfd_online = true;
    mock_mixer_status.freq_dhz = 123;
    mock_mixer_enabled = true;

    mock_mqtt_fire_event(MQTT_EVENT_CONNECTED, NULL, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_message_count,
                                  "The esp-mqtt event handler itself publishes nothing: it may "
                                  "not take the lock the publishing task holds while publishing");
    mqtt_client_wb_test_tick();

    // The device before its controls.
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_message_index(DEV_META_TOPIC),
                                  "The device meta is the first thing a session publishes");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_message_index(DEV_ERROR_TOPIC),
                                  "The empty device error follows it");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", payload_of(DEV_ERROR_TOPIC),
                                     "A connected device clears the will's error explicitly");

    for (int i = 0; i < ALL_CONTROLS_COUNT; i++) {
        char meta_topic[WB_DEVICE_TOPIC_MAX_LEN];
        char error_topic[WB_DEVICE_TOPIC_MAX_LEN];
        char value_topic[WB_DEVICE_TOPIC_MAX_LEN];

        wb_device_topic_control_meta(TEST_DEVICE_ID, all_controls[i], meta_topic, sizeof(meta_topic));
        wb_device_topic_control_error(TEST_DEVICE_ID, all_controls[i], error_topic, sizeof(error_topic));
        wb_device_topic_control(TEST_DEVICE_ID, all_controls[i], value_topic, sizeof(value_topic));

        int meta_index = mock_mqtt_message_index(meta_topic);
        int error_index = mock_mqtt_message_index(error_topic);
        int value_index = mock_mqtt_message_index(value_topic);

        char message[160];
        snprintf(message, sizeof(message), "Control %s publishes meta, then error, then value",
                 all_controls[i]);
        TEST_ASSERT_TRUE_MESSAGE(meta_index >= 0, message);
        TEST_ASSERT_TRUE_MESSAGE(error_index > meta_index, message);
        TEST_ASSERT_TRUE_MESSAGE(value_index > error_index, message);

        // A control with no value published is not shown by the panel at all, so every one of
        // them gets a value even on the very first session.
        TEST_ASSERT_TRUE_MESSAGE(mock_mqtt_messages[value_index].retain,
                                 "A control value is retained");
    }

    TEST_ASSERT_EQUAL_STRING_MESSAGE("1", payload_of(CTRL("enabled")), "A switch travels as 0 or 1");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("12.3", payload_of(CTRL("frequency")),
                                     "0.1 Hz from the drive is published in Hz");

    // Only what the panel may write is subscribed.
    TEST_ASSERT_TRUE_MESSAGE(mock_mqtt_is_subscribed(CTRL_CMD("enabled")),
                             "The master switch takes commands");
    TEST_ASSERT_TRUE_MESSAGE(mock_mqtt_is_subscribed(CTRL_CMD("program")),
                             "The program selector takes commands");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_mqtt_subscription_count,
                                  "And nothing else does - a read-only control has no /on");

    // A reconnect is a full republish: the broker may have been restarted and kept none of it.
    mock_mqtt_message_count = 0;
    mock_mqtt_fire_event(MQTT_EVENT_DISCONNECTED, NULL, NULL);
    mock_mqtt_fire_event(MQTT_EVENT_CONNECTED, NULL, NULL);
    mqtt_client_wb_test_tick();
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_mqtt_find_message(DEV_META_TOPIC),
                                 "A new session publishes the device meta again");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_mqtt_find_message(CTRL("speed")),
                                 "And every control value again");
}

/* The periodic pass publishes what changed and nothing else: an idle mixer costs no traffic. */
void test_only_changed_values_are_published(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test that only changed values are published");
    LOG_MESSAGE();

    mock_mixer_status.vfd_online = true;
    mock_mixer_status.speed_rpm = 200;
    connect_and_settle();

    mqtt_client_wb_test_tick();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_message_count,
                                  "A pass over an unchanged device publishes nothing at all");

    mock_mixer_status.speed_rpm = 300;
    mqtt_client_wb_test_tick();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_message_count, "Only the control that moved is published");
    TEST_ASSERT_EQUAL_STRING("300", payload_of(CTRL("speed")));

    // The same value again is not a change.
    mqtt_client_wb_test_tick();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_message_count,
                                  "A value that stays put is not republished");

    // Several at once, and each exactly once.
    mock_mqtt_message_count = 0;
    mock_mixer_status.state = MIXER_RUNNING;
    mock_mixer_status.step = 2;
    mock_mixer_status.step_left_s = 41;
    mqtt_client_wb_test_tick();
    TEST_ASSERT_EQUAL_INT(3, mock_mqtt_message_count);
    TEST_ASSERT_EQUAL_STRING("1", payload_of(CTRL("state")));
    TEST_ASSERT_EQUAL_STRING("2", payload_of(CTRL("step")));
    TEST_ASSERT_EQUAL_STRING("41", payload_of(CTRL("step_left")));

    // Nothing is published while there is no connection to publish onto.
    mock_mqtt_message_count = 0;
    mock_mqtt_fire_event(MQTT_EVENT_DISCONNECTED, NULL, NULL);
    mock_mixer_status.speed_rpm = 500;
    mqtt_client_wb_test_tick();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_message_count,
                                  "A disconnected client publishes nothing");
}

/* A drive that has stopped answering: the four controls fed from it are marked unreadable and
 * keep the last value they had. When it comes back the error is cleared BEFORE the new value, so
 * the panel never shows a fresh reading under a stale "r". */
void test_unreadable_controls_while_the_drive_is_silent(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test the r error while the drive is silent");
    LOG_MESSAGE();

    mock_mixer_status.vfd_online = true;
    mock_mixer_status.freq_dhz = 250;
    mock_mixer_status.bus_voltage_dv = 3110;
    mock_mixer_status.current_ca = 145;
    mock_mixer_status.temp_c = 42;
    connect_and_settle();

    // The drive goes silent, and its registers would read as zeroes if anyone published them.
    mock_mixer_status.vfd_online = false;
    mock_mixer_status.freq_dhz = 0;
    mock_mixer_status.bus_voltage_dv = 0;
    mock_mixer_status.current_ca = 0;
    mock_mixer_status.temp_c = 0;
    mqtt_client_wb_test_tick();

    for (int i = 0; i < VFD_CONTROLS_COUNT; i++) {
        char error_topic[WB_DEVICE_TOPIC_MAX_LEN];
        char value_topic[WB_DEVICE_TOPIC_MAX_LEN];
        wb_device_topic_control_error(TEST_DEVICE_ID, vfd_controls[i], error_topic, sizeof(error_topic));
        wb_device_topic_control(TEST_DEVICE_ID, vfd_controls[i], value_topic, sizeof(value_topic));

        char message[160];
        snprintf(message, sizeof(message), "Control %s is unreadable while the drive is silent",
                 vfd_controls[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("r", payload_of(error_topic), message);

        snprintf(message, sizeof(message), "Control %s keeps its last good value", vfd_controls[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_count_messages(value_topic), message);
    }

    // Everything that does not come from the drive carries no error and keeps being published.
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_count_messages(CTRL_ERROR("state")),
                                  "A control that does not depend on the drive is unaffected");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("0", payload_of(CTRL("vfd_online")),
                                     "The link itself is a value, and it changed");

    // The drive answers again, with new readings.
    mock_mqtt_message_count = 0;
    mock_mixer_status.vfd_online = true;
    mock_mixer_status.freq_dhz = 300;
    mock_mixer_status.bus_voltage_dv = 3050;
    mock_mixer_status.current_ca = 7;
    mock_mixer_status.temp_c = 44;
    mqtt_client_wb_test_tick();

    for (int i = 0; i < VFD_CONTROLS_COUNT; i++) {
        char error_topic[WB_DEVICE_TOPIC_MAX_LEN];
        char value_topic[WB_DEVICE_TOPIC_MAX_LEN];
        wb_device_topic_control_error(TEST_DEVICE_ID, vfd_controls[i], error_topic, sizeof(error_topic));
        wb_device_topic_control(TEST_DEVICE_ID, vfd_controls[i], value_topic, sizeof(value_topic));

        char message[160];
        snprintf(message, sizeof(message), "Control %s is cleared before it is updated",
                 vfd_controls[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", payload_of(error_topic), message);
        TEST_ASSERT_TRUE_MESSAGE(mock_mqtt_message_index(error_topic) <
                                 mock_mqtt_message_index(value_topic), message);
    }

    TEST_ASSERT_EQUAL_STRING("30.0", payload_of(CTRL("frequency")));
    TEST_ASSERT_EQUAL_STRING("305.0", payload_of(CTRL("voltage")));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("0.07", payload_of(CTRL("current")),
                                     "0.01 A units are published with two decimals");
    TEST_ASSERT_EQUAL_STRING("44", payload_of(CTRL("temperature")));
}

/* Commands from the panel. What is accepted is a bare number in the control's range; everything
 * else is refused and nothing reaches the engine. */
void test_incoming_commands(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test parsing the commands from the panel");
    LOG_MESSAGE();

    connect_and_settle();

    // The whole path, through the event the client delivers. The event handler only queues it -
    // touching the engine from there would mean holding the device lock under esp-mqtt's own
    // API lock - so the tick is what applies it.
    mock_mqtt_fire_event(MQTT_EVENT_DATA, CTRL_CMD("enabled"), "1");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mixer_set_enabled_called,
                                  "The event handler hands the command over rather than applying it");
    mqtt_client_wb_test_tick();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mixer_set_enabled_called, "A switch command reaches the engine");
    TEST_ASSERT_TRUE(mock_mixer_set_enabled_value);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("1", payload_of(CTRL("enabled")),
                                     "And the accepted value is echoed into the value topic");

    mock_mqtt_fire_event(MQTT_EVENT_DATA, CTRL_CMD("enabled"), "0");
    mqtt_client_wb_test_tick();
    TEST_ASSERT_EQUAL_INT(2, mock_mixer_set_enabled_called);
    TEST_ASSERT_FALSE(mock_mixer_set_enabled_value);

    mock_mqtt_fire_event(MQTT_EVENT_DATA, CTRL_CMD("program"), "3");
    mqtt_client_wb_test_tick();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mixer_set_program_called, "A program command reaches the engine");
    TEST_ASSERT_EQUAL_INT(3, mock_mixer_set_program_value);
    TEST_ASSERT_EQUAL_STRING("3", payload_of(CTRL("program")));

    // The number the panel is shown comes from the same place as the switch - the setting - so
    // the pass that follows the command does not put the old one back for a second. Taking it
    // from the engine's snapshot, which only moves on the engine's own tick, is what made the
    // control blink.
    mqtt_client_wb_test_tick();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_count_messages(CTRL("program")),
                                  "An accepted program number is published once and stays put");

    // Everything below is refused, and the counters above must not move.
    int enabled_calls = mock_mixer_set_enabled_called;
    int program_calls = mock_mixer_set_program_called;

    struct {
        const char *topic;
        const char *payload;
        esp_err_t expected;
        const char *why;
    } refused[] = {
        {CTRL_CMD("enabled"),  "2",      ESP_ERR_INVALID_ARG, "A switch takes 0 and 1 and nothing else"},
        {CTRL_CMD("enabled"),  "true",   ESP_ERR_INVALID_ARG, "A switch is a number, not a word"},
        {CTRL_CMD("enabled"),  "",       ESP_ERR_INVALID_ARG, "An empty payload is not a command"},
        {CTRL_CMD("enabled"),  " 1",     ESP_ERR_INVALID_ARG, "A leading space is not a number"},
        {CTRL_CMD("enabled"),  "1x",     ESP_ERR_INVALID_ARG, "Trailing rubbish is not a number"},
        {CTRL_CMD("enabled"),  "-1",     ESP_ERR_INVALID_ARG, "A switch has no negative position"},
        {CTRL_CMD("program"),  "4",      ESP_ERR_INVALID_ARG, "There is no fifth program"},
        {CTRL_CMD("program"),  "abc",    ESP_ERR_INVALID_ARG, "A program index is a number"},
        {CTRL_CMD("program"),  "999999", ESP_ERR_INVALID_ARG, "And a small one"},
        {CTRL_CMD("state"),    "1",      ESP_ERR_NOT_FOUND,   "A read-only control takes no command"},
        {CTRL_CMD("speed"),    "100",    ESP_ERR_NOT_FOUND,   "Nor does a measurement"},
        {CTRL("enabled"),      "1",      ESP_ERR_NOT_FOUND,   "The value topic is not a command topic"},
        {"/devices/other/controls/enabled/on", "1", ESP_ERR_NOT_FOUND, "Another device's command is not ours"},
    };

    for (size_t i = 0; i < (sizeof(refused) / sizeof(refused[0])); i++) {
        esp_err_t ret = wb_device_handle_command(refused[i].topic, (int)strlen(refused[i].topic),
                                                 refused[i].payload, (int)strlen(refused[i].payload));
        TEST_ASSERT_EQUAL_INT_MESSAGE(refused[i].expected, ret, refused[i].why);
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(enabled_calls, mock_mixer_set_enabled_called,
                                  "Not one refused command reached the engine");
    TEST_ASSERT_EQUAL_INT_MESSAGE(program_calls, mock_mixer_set_program_called,
                                  "Not one refused command reached the engine");

    // An engine that refuses the command leaves the value topic alone: the panel must not be
    // told the switch moved when it did not.
    mock_mqtt_message_count = 0;
    mock_mixer_set_enabled_return = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(ESP_FAIL, wb_device_handle_command(CTRL_CMD("enabled"), (int)strlen(CTRL_CMD("enabled")),
                                                             "1", 1));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_count_messages(CTRL("enabled")),
                                  "A refused command is not echoed");
}

/* A command is never published with the retain flag: the panel's writes are events, and a
 * retained one would be replayed to the device on every reconnect. */
void test_commands_are_not_retained(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test that this device never retains a command");
    LOG_MESSAGE();

    mock_mixer_status.vfd_online = true;
    connect_and_settle();

    mock_mqtt_fire_event(MQTT_EVENT_DATA, CTRL_CMD("enabled"), "1");
    mqtt_client_wb_test_tick();

    for (int i = 0; i < mock_mqtt_message_count; i++) {
        char message[220];
        snprintf(message, sizeof(message), "Message to %s must be retained state, never a command",
                 mock_mqtt_messages[i].topic);
        TEST_ASSERT_TRUE_MESSAGE(mock_mqtt_messages[i].retain, message);
        TEST_ASSERT_NULL_MESSAGE(strstr(mock_mqtt_messages[i].topic, "/on"), message);
    }
}

/* Renaming a program changes no value, only the labels of the selector - which live in the meta,
 * so the meta is what has to go out again. */
void test_renamed_program_republishes_the_meta(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test that a renamed program updates the selector");
    LOG_MESSAGE();

    mock_mixer_status.vfd_online = true;
    connect_and_settle();

    mqtt_client_wb_test_tick();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_count_messages(CTRL_META("program")),
                                  "Unchanged names do not republish the meta");

    mock_setting_items_set(KEY_MIX_P1_NAME, "Porcelain");
    mqtt_client_wb_test_tick();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_count_messages(CTRL_META("program")),
                                  "A renamed program republishes the selector's meta");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload_of(CTRL_META("program")), "Porcelain"),
                                 "With the new name in it");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_count_messages(CTRL_META("state")),
                                  "And nothing else is republished");

    // Once is enough: the next pass sees the names it has already published.
    mqtt_client_wb_test_tick();
    TEST_ASSERT_EQUAL_INT(1, mock_mqtt_count_messages(CTRL_META("program")));
}

/* What is started, and what is not: MQTT switched off, or switched on with no broker, connects
 * to nothing. Changing any of the six settings is what makes the apply restart the client. */
void test_configuration_and_restart(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test starting, not starting and restarting");
    LOG_MESSAGE();

    // Switched off: nothing is created, and that is not an error.
    configure_mqtt(TEST_BROKER_HOST, TEST_DEVICE_ID);
    mock_setting_items_set(KEY_MQTT_ENABLED, "false");
    TEST_ASSERT_EQUAL_INT(ESP_OK, mqtt_client_wb_start());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_init_called, "MQTT switched off creates no client");

    // Switching it on is a change the apply has to notice.
    mock_setting_items_set(KEY_MQTT_ENABLED, "true");
    TEST_ASSERT_TRUE_MESSAGE(mqtt_client_wb_check_settings_changed(),
                             "Switching MQTT on is a settings change");
    mqtt_client_wb_restart();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_init_called, "And the restart brings the client up");
    TEST_ASSERT_FALSE_MESSAGE(mqtt_client_wb_check_settings_changed(),
                              "Nothing has changed since the client was started");

    // Every one of the six settings counts.
    mock_setting_items_set(KEY_MQTT_HOST, "10.0.0.6");
    TEST_ASSERT_TRUE(mqtt_client_wb_check_settings_changed());
    mqtt_client_wb_restart();
    TEST_ASSERT_EQUAL_STRING("10.0.0.6", mock_mqtt_config.host);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_stop_called, "A restart stops the old client");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_destroy_called, "And destroys it");

    mock_setting_items_set(KEY_MQTT_PORT, "8883");
    TEST_ASSERT_TRUE(mqtt_client_wb_check_settings_changed());
    mqtt_client_wb_restart();
    TEST_ASSERT_EQUAL_UINT32(8883, mock_mqtt_config.port);

    // An empty id is not a name: the device answers to the one built from the model and the MAC.
    mock_setting_items_set(KEY_MQTT_ID, "");
    TEST_ASSERT_TRUE(mqtt_client_wb_check_settings_changed());
    mqtt_client_wb_restart();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("/devices/WB-MGE-030405/meta/error", mock_mqtt_config.will_topic,
                                     "An unnamed device falls back to the model and the MAC");

    // Switched on with nowhere to connect to: still not an error, still no client.
    mock_mqtt_reset();
    mqtt_client_wb_stop();
    mock_setting_items_set(KEY_MQTT_HOST, "");
    TEST_ASSERT_EQUAL_INT(ESP_OK, mqtt_client_wb_start());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_init_called, "No broker configured, no client");
}

/* Switching MQTT off in the panel is a clean DISCONNECT, and a broker does not publish the will
 * of a client that left properly - so the device has to say it is gone itself, with the same
 * marker the will carries, before the connection is closed. */
void test_stop_marks_the_device_offline(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test the goodbye marker a stop publishes");
    LOG_MESSAGE();

    connect_and_settle();

    mqtt_client_wb_stop();

    const mock_mqtt_message_t *goodbye = mock_mqtt_find_last_message(DEV_ERROR_TOPIC);
    TEST_ASSERT_NOT_NULL_MESSAGE(goodbye, "A stop publishes into the device error topic");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("rw", goodbye->payload,
                                     "With the same marker the will would have carried");
    TEST_ASSERT_TRUE_MESSAGE(goodbye->retain,
                             "Retained, or the panel would never see it");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_stop_called, "And only then is the client stopped");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mqtt_destroy_called, "And destroyed");

    // A client that never connected has nothing to say goodbye on: the broker holds no retained
    // topic of this session, and a publish would be made into a socket that is not there.
    mock_mqtt_reset();
    configure_mqtt(TEST_BROKER_HOST, TEST_DEVICE_ID);
    TEST_ASSERT_EQUAL_INT(ESP_OK, mqtt_client_wb_start());
    mqtt_client_wb_stop();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mqtt_message_count,
                                  "A client that never connected publishes nothing on the way out");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_topics);
    RUN_TEST(test_device_meta_json);
    RUN_TEST(test_control_meta_json);
    RUN_TEST(test_startup_publishes_the_whole_device);
    RUN_TEST(test_only_changed_values_are_published);
    RUN_TEST(test_unreadable_controls_while_the_drive_is_silent);
    RUN_TEST(test_incoming_commands);
    RUN_TEST(test_commands_are_not_retained);
    RUN_TEST(test_renamed_program_republishes_the_meta);
    RUN_TEST(test_configuration_and_restart);
    RUN_TEST(test_stop_marks_the_device_offline);

    return UNITY_END();
}
