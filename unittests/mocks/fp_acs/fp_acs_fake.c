#include "fp_acs_fake.h"

#include <stdio.h>
#include <string.h>

#include "trace.h"

int             fp_acs_fake_telegram_reports = 0;
fp_acs_result_t fp_acs_fake_telegram_result = FP_ACS_RESULT_UNKNOWN;
uint8_t         fp_acs_fake_telegram_user_id = FP_STORE_NO_USER;
char            fp_acs_fake_telegram_chat[128];
int             fp_acs_fake_mqtt_reports = 0;

bool            fp_acs_fake_online = false;
bool            fp_acs_fake_has_event = false;
fp_acs_event_t  fp_acs_fake_event;

void fp_acs_fake_reset(void)
{
    fp_acs_fake_telegram_reports = 0;
    fp_acs_fake_telegram_result = FP_ACS_RESULT_UNKNOWN;
    fp_acs_fake_telegram_user_id = FP_STORE_NO_USER;
    fp_acs_fake_telegram_chat[0] = '\0';
    fp_acs_fake_mqtt_reports = 0;
    fp_acs_fake_online = false;
    fp_acs_fake_has_event = false;
    memset(&fp_acs_fake_event, 0, sizeof(fp_acs_fake_event));
}

void fp_acs_report_telegram_event(fp_acs_result_t result, uint8_t user_id, const char *chat)
{
    fp_acs_fake_telegram_reports++;
    fp_acs_fake_telegram_result = result;
    fp_acs_fake_telegram_user_id = user_id;
    snprintf(fp_acs_fake_telegram_chat, sizeof(fp_acs_fake_telegram_chat), "%s", (chat != NULL) ? chat : "");
    trace_add("report.%s", fp_acs_result_str(result));
}

void fp_acs_report_mqtt_event(void)
{
    fp_acs_fake_mqtt_reports++;
    trace_add("report.mqtt");
}

bool fp_acs_sensor_online(void)
{
    return fp_acs_fake_online;
}

bool fp_acs_get_last_event(fp_acs_event_t *out)
{
    *out = fp_acs_fake_event;
    return fp_acs_fake_has_event;
}

const char *fp_acs_source_str(fp_acs_source_t source)
{
    switch (source) {
    case FP_ACS_SOURCE_TELEGRAM: return "telegram";
    case FP_ACS_SOURCE_MQTT:     return "mqtt";
    default:                     return "fingerprint";
    }
}

const char *fp_acs_result_str(fp_acs_result_t result)
{
    switch (result) {
    case FP_ACS_RESULT_GRANTED: return "granted";
    case FP_ACS_RESULT_DENIED:  return "denied";
    default:                    return "unknown";
    }
}
