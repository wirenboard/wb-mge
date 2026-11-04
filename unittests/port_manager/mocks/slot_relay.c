#include "slot_relay.h"

int mock_slot_relay_acquire_called = 0;
int mock_slot_relay_release_called = 0;

void slot_relay_acquire(void)
{
    mock_slot_relay_acquire_called++;
}

void slot_relay_release(void)
{
    mock_slot_relay_release_called++;
}

void mock_slot_relay_reset(void)
{
    mock_slot_relay_acquire_called = 0;
    mock_slot_relay_release_called = 0;
}
