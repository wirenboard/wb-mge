#include "tg_bot_fake.h"

#include <stdio.h>
#include <string.h>

#include "tg_bot.h"
#include "trace.h"

int  tg_bot_fake_notify_calls = 0;
char tg_bot_fake_notify_name[FP_STORE_NAME_MAX + 1];

void tg_bot_fake_reset(void)
{
    tg_bot_fake_notify_calls = 0;
    memset(tg_bot_fake_notify_name, 0, sizeof(tg_bot_fake_notify_name));
}

void tg_bot_start(void)
{
}

void tg_bot_notify_fp_open(const char *user_name)
{
    tg_bot_fake_notify_calls++;
    snprintf(tg_bot_fake_notify_name, sizeof(tg_bot_fake_notify_name), "%s", user_name);
    trace_add("notify");
}

void tg_bot_get_status(bool *enabled, bool *online, char *error, size_t error_size)
{
    *enabled = false;
    *online = false;
    if (error_size > 0) {
        error[0] = '\0';
    }
}
