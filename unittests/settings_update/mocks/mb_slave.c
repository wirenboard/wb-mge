#include "mb_slave.h"

#include "call_sequence.h"

int  mock_mb_slave_start_called = 0;
int  mock_mb_slave_stop_called = 0;
int  mock_mb_slave_check_settings_changed_called = 0;
bool mock_mb_slave_check_settings_changed_result = false;

unsigned mock_mb_slave_start_call_seq = 0;
unsigned mock_mb_slave_stop_call_seq = 0;

int      mock_mb_slave_lock_init_called = 0;
int      mock_mb_slave_lock_take_called = 0;
int      mock_mb_slave_lock_give_called = 0;
unsigned mock_mb_slave_lock_take_call_seq = 0;
unsigned mock_mb_slave_lock_give_call_seq = 0;
uint32_t mock_mb_slave_lock_take_timeout_ms = 0;
bool     mock_mb_slave_lock_take_result = true;
int      mock_mb_slave_lock_held_count = 0;

esp_err_t mb_slave_start(void)
{
    mock_mb_slave_start_called++;
    mock_mb_slave_start_call_seq = call_sequence_get_call_id();
    return ESP_OK;
}

esp_err_t mb_slave_stop(void)
{
    mock_mb_slave_stop_called++;
    mock_mb_slave_stop_call_seq = call_sequence_get_call_id();
    return ESP_OK;
}

bool mb_slave_check_settings_changed(void)
{
    mock_mb_slave_check_settings_changed_called++;
    return mock_mb_slave_check_settings_changed_result;
}

esp_err_t mb_slave_set_tx_disabled(unsigned index, bool disabled)
{
    (void)index;
    (void)disabled;
    return ESP_OK;
}

void mb_slave_lock_init(void)
{
    mock_mb_slave_lock_init_called++;
}

bool mb_slave_lock_take(uint32_t timeout_ms)
{
    mock_mb_slave_lock_take_called++;
    mock_mb_slave_lock_take_call_seq = call_sequence_get_call_id();
    mock_mb_slave_lock_take_timeout_ms = timeout_ms;
    if (mock_mb_slave_lock_take_result) {
        mock_mb_slave_lock_held_count++;
    }
    return mock_mb_slave_lock_take_result;
}

void mb_slave_lock_give(void)
{
    mock_mb_slave_lock_give_called++;
    mock_mb_slave_lock_give_call_seq = call_sequence_get_call_id();
    if (mock_mb_slave_lock_held_count > 0) {
        mock_mb_slave_lock_held_count--;
    }
}

void mock_mb_slave_reset(void)
{
    mock_mb_slave_start_called = 0;
    mock_mb_slave_stop_called = 0;
    mock_mb_slave_check_settings_changed_called = 0;
    mock_mb_slave_check_settings_changed_result = false;

    mock_mb_slave_start_call_seq = 0;
    mock_mb_slave_stop_call_seq = 0;

    mock_mb_slave_lock_init_called = 0;
    mock_mb_slave_lock_take_called = 0;
    mock_mb_slave_lock_give_called = 0;
    mock_mb_slave_lock_take_call_seq = 0;
    mock_mb_slave_lock_give_call_seq = 0;
    mock_mb_slave_lock_take_timeout_ms = 0;
    mock_mb_slave_lock_take_result = true;
    mock_mb_slave_lock_held_count = 0;
}
