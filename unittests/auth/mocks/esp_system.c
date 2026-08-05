#include "esp_system.h"

int mock_esp_reset_reason_called = 0;

static esp_reset_reason_t mock_reset_reason = ESP_RST_POWERON;

esp_reset_reason_t esp_reset_reason(void)
{
    mock_esp_reset_reason_called++;
    return mock_reset_reason;
}

void mock_esp_system_reset(void)
{
    mock_esp_reset_reason_called = 0;
    mock_reset_reason = ESP_RST_POWERON;
}

void mock_esp_system_set_reset_reason(esp_reset_reason_t reason)
{
    mock_reset_reason = reason;
}
