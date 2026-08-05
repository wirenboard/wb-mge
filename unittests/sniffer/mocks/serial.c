#include "serial.h"
#include "serial_mock.h"
#include <string.h>

mock_serial_set_rx_timeout_t mock_serial_set_rx_timeout_data = {0};

esp_err_t serial_set_rx_timeout(serial_desc_t *desc, uint8_t tout_symbols)
{
    mock_serial_set_rx_timeout_data.called++;
    mock_serial_set_rx_timeout_data.desc = desc;
    mock_serial_set_rx_timeout_data.tout_symbols = tout_symbols;
    return mock_serial_set_rx_timeout_data.result;
}

const char *esp_err_to_name(esp_err_t code)
{
    if (code == 0) return "ESP_OK";
    if (code == -1) return "ESP_FAIL";
    return "ESP_UNKNOWN";
}

void mock_serial_reset(void)
{
    memset(&mock_serial_set_rx_timeout_data, 0, sizeof(mock_serial_set_rx_timeout_data));
}
