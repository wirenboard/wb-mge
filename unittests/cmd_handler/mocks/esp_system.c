#include "esp_system.h"

int mock_esp_restart_called = 0;

void mock_esp_system_reset(void)
{
    mock_esp_restart_called = 0;
}

void esp_restart(void)
{
    mock_esp_restart_called++;
}
