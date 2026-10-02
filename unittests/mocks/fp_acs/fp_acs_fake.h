#pragma once

/*
 * fp_acs (main/fp_acs/fp_acs.h) as the Telegram bot and the MQTT device see it: the two reports
 * are recorded (and added to the trace as "report.<result>" and "report.mqtt"), the sensor state
 * and the last event are whatever the test sets. The wire names are fp_acs's own; fp_acs_test
 * checks the real functions against the same strings.
 */

#include <stdbool.h>
#include <stdint.h>

#include "fp_acs.h"

extern int             fp_acs_fake_telegram_reports;
extern fp_acs_result_t fp_acs_fake_telegram_result;
extern uint8_t         fp_acs_fake_telegram_user_id;
extern char            fp_acs_fake_telegram_chat[128];
extern int             fp_acs_fake_mqtt_reports;

extern bool            fp_acs_fake_online;
extern bool            fp_acs_fake_has_event;
extern fp_acs_event_t  fp_acs_fake_event;

void fp_acs_fake_reset(void);
