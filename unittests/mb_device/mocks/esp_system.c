#include "esp_system.h"

esp_reset_reason_t mock_reset_reason = ESP_RST_UNKNOWN;

esp_reset_reason_t esp_reset_reason(void)
{
    return mock_reset_reason;
}

void mock_set_reset_reason(esp_reset_reason_t reason)
{
    mock_reset_reason = reason;
}

void mock_reset_reason_reset(void)
{
    mock_reset_reason = ESP_RST_UNKNOWN;
}
