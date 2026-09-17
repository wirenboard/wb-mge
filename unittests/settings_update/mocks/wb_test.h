#pragma once

#include <stdbool.h>

// Only the guard settings_update.c reads. The real header also declares the two httpd
// handlers, which would drag esp_http_server.h into a suite that has no web server.
extern bool mock_wb_test_clock_out_active_value;
extern int  mock_wb_test_clock_out_active_called;
/* Call id of the LAST read (call_sequence.c), so a test can assert that the guard is read
 * INSIDE the ownership lock rather than before it. 0 = never read in this test. */
extern unsigned mock_wb_test_clock_out_active_call_seq;

void mock_wb_test_reset(void);

bool wb_test_clock_out_active(void);
