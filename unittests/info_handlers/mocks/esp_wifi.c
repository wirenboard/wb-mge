/* esp_wifi mock for the info_handlers unit test.
 *
 * On the perm-disable=false path, info_build_ap_clients_json() calls
 * esp_wifi_ap_get_sta_list(). The mock reports zero associated stations
 * (num = 0) so the normal path produces an empty clients array without needing
 * the full DHCP/netif client enumeration. mock_esp_wifi_get_sta_list_called
 * lets the test confirm the normal (non-early-return) path was taken. */

#include "esp_wifi.h"
#include <string.h>

int mock_esp_wifi_get_sta_list_called = 0;

void mock_esp_wifi_reset(void)
{
    mock_esp_wifi_get_sta_list_called = 0;
}

esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *sta_list)
{
    mock_esp_wifi_get_sta_list_called++;
    if (sta_list != NULL) {
        memset(sta_list, 0, sizeof(*sta_list));
        sta_list->num = 0;  /* no associated stations */
    }
    return ESP_OK;
}

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info)
{
    if (ap_info != NULL) {
        memset(ap_info, 0, sizeof(*ap_info));
    }
    return ESP_OK;
}
