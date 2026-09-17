#include "wb_test.h"

#include "call_sequence.h"

bool mock_wb_test_clock_out_active_value = false;
int  mock_wb_test_clock_out_active_called = 0;
unsigned mock_wb_test_clock_out_active_call_seq = 0;

bool wb_test_clock_out_active(void)
{
    mock_wb_test_clock_out_active_called++;
    mock_wb_test_clock_out_active_call_seq = call_sequence_get_call_id();
    return mock_wb_test_clock_out_active_value;
}

void mock_wb_test_reset(void)
{
    mock_wb_test_clock_out_active_value = false;
    mock_wb_test_clock_out_active_called = 0;
    mock_wb_test_clock_out_active_call_seq = 0;
}
