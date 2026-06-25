/* esp_netif mock for the info_handlers unit test. Only the perm-disable=false
 * path with associated stations would reach these; the test keeps num == 0, so
 * these are link-only stubs returning benign values. */

#include "esp_netif.h"

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *if_key)
{
    (void)if_key;
    return NULL;
}

esp_err_t esp_netif_dhcps_get_clients_by_mac(esp_netif_t *esp_netif, int num,
                                             esp_netif_pair_mac_ip_t *mac_ip_pair)
{
    (void)esp_netif;
    (void)num;
    (void)mac_ip_pair;
    return ESP_OK;
}
