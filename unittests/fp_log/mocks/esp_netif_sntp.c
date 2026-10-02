#include "esp_netif_sntp.h"

#include <stddef.h>

int         mock_sntp_init_calls = 0;
const char *mock_sntp_server = NULL;
esp_err_t   mock_sntp_init_result = ESP_OK;

esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config)
{
    mock_sntp_init_calls++;
    mock_sntp_server = (config->num_of_servers > 0) ? config->servers[0] : NULL;
    return mock_sntp_init_result;
}

void mock_sntp_reset(void)
{
    mock_sntp_init_calls = 0;
    mock_sntp_server = NULL;
    mock_sntp_init_result = ESP_OK;
}
