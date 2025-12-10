#include "unity.h"
#include "esp_mac.h"
#include <string.h>

int mock_esp_read_mac_called = 0;
esp_err_t mock_esp_read_mac_return = ESP_OK;

uint8_t mock_mac_address[MAC_ADDRESS_SIZE] = {0};

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(mac, "MAC output pointer is NULL");
    TEST_ASSERT_LESS_THAN_MESSAGE(ESP_MAC_EFUSE_EXT + 1, type, "Invalid esp_mac_type_t");

    mock_esp_read_mac_called++;

    if (mock_esp_read_mac_return != ESP_OK) {
        return mock_esp_read_mac_return;
    }

    memcpy(mac, mock_mac_address, MAC_ADDRESS_SIZE);
    return mock_esp_read_mac_return;
}

void mock_esp_mac_reset(void)
{
    mock_esp_read_mac_called = 0;
    mock_esp_read_mac_return = ESP_OK;
    for (int i = 0; i < MAC_ADDRESS_SIZE; i++) {
        mock_mac_address[i] = i;
    }
}
