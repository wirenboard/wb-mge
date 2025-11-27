#include "esp_mac.h"

bool mock_esp_read_mac_should_fail = false;

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
    (void)type;

    if (mock_esp_read_mac_should_fail) {
        return ESP_FAIL;
    }

    for (int i = 0; i < 6; i++) {
        mac[i] = i;
    }
    return ESP_OK;
}
