#pragma once

/*
 * tg_bot (main/telegram/tg_bot.h) as the sensor task sees it: tg_bot_notify_fp_open() counts the
 * reports, keeps the last user name and adds "notify" to the trace.
 */

#include "fp_store.h"

extern int  tg_bot_fake_notify_calls;
extern char tg_bot_fake_notify_name[FP_STORE_NAME_MAX + 1];

void tg_bot_fake_reset(void);
