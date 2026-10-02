#include "fp_lock_fake.h"

#include "fp_lock.h"
#include "trace.h"

int  fp_lock_fake_open_calls = 0;
bool fp_lock_fake_open_result = true;

void fp_lock_fake_reset(void)
{
    fp_lock_fake_open_calls = 0;
    fp_lock_fake_open_result = true;
}

void fp_lock_start(void)
{
}

bool fp_lock_open(void)
{
    fp_lock_fake_open_calls++;
    trace_add("open");
    return fp_lock_fake_open_result;
}

bool fp_lock_set(bool on)
{
    (void)on;
    return true;
}

void fp_lock_settings_changed(void)
{
}

bool fp_lock_relay_on(void)
{
    return false;
}

bool fp_lock_relay_online(void)
{
    return false;
}
