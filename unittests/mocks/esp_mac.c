#include "esp_mac.h"

int esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
    for (uint8_t i = 0; i < 6; i++)
    {
        mac[i] = i;
    }
    return 0;
}
