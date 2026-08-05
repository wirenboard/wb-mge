#include "unity.h"
#include "esp_psram.h"

bool mock_esp_psram_is_initialized_return = false;
size_t mock_esp_psram_get_size_return = 0;

bool esp_psram_is_initialized(void)
{
    return mock_esp_psram_is_initialized_return;
}

size_t esp_psram_get_size(void)
{
    return mock_esp_psram_get_size_return;
}

void mock_esp_psram_reset(void)
{
    mock_esp_psram_is_initialized_return = false;
    mock_esp_psram_get_size_return = 0;
}
