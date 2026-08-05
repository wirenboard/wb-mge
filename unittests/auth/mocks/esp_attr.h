#pragma once

/* auth.c keeps its session backup in RTC memory (RTC_NOINIT_ATTR), which survives
 * esp_restart(). In the firmware build that macro places the variables in a dedicated linker
 * section; on the host there is no RTC memory and no such section, so define it away — the
 * backup then lives in plain .bss, which is exactly the "survives within one run, reset between
 * runs" behaviour the tests need. */
#define RTC_NOINIT_ATTR
