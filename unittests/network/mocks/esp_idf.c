// Stubs for the ESP-IDF entry points network.c reaches outside the modules this suite
// mocks by name: the event loop, esp_netif, the Ethernet driver, mDNS and the two
// esp_wifi calls used purely to fill sys_info. None of them influences the WiFi settings
// logic under test, so they only have to link and succeed.
//
// The sys_info global those calls write into lives in mocks/sys_info.c, which owns its
// reset as well.

#include <stddef.h>

#include "esp_err.h"
#include "esp_eth_driver.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"

esp_err_t esp_event_loop_create_default(void)
{
    return ESP_OK;
}

esp_err_t esp_netif_init(void)
{
    return ESP_OK;
}

esp_err_t mdns_init(void)
{
    return ESP_OK;
}

esp_err_t mdns_hostname_set(const char *hostname)
{
    (void)hostname;
    return ESP_OK;
}

esp_err_t ethernet_init(esp_event_handler_t eth_event_handler, esp_netif_ip_info_t *static_ip,
                        char *netif_hostname)
{
    (void)eth_event_handler;
    (void)static_ip;
    (void)netif_hostname;
    return ESP_OK;
}

// NULL keeps update_sys_info_eth_mac() on its "no handle" branch, so esp_eth_ioctl() below
// is never reached — it exists only to satisfy the linker.
esp_eth_handle_t ethernet_get_handle(void)
{
    return NULL;
}

esp_err_t ethernet_set_ip_hostname(esp_netif_ip_info_t *static_ip, char *netif_hostname)
{
    (void)static_ip;
    (void)netif_hostname;
    return ESP_OK;
}

esp_err_t esp_eth_ioctl(esp_eth_handle_t hdl, esp_eth_io_cmd_t cmd, void *data)
{
    (void)hdl;
    (void)cmd;
    (void)data;
    return ESP_OK;
}

esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6])
{
    (void)ifx;
    (void)mac;
    return ESP_OK;
}

// The other suites link the real esp_common/src/esp_err_to_name.c, but that file
// __has_include-probes every IDF component header within reach, and with esp_wifi on the
// include path it lands in esp_mesh.h -> lwip/ip_addr.h, which a host build does not have.
// network.c uses the name in one log message on a branch this suite never takes
// (ethernet_get_handle() returns NULL above), so a placeholder is enough.
const char *esp_err_to_name(esp_err_t code)
{
    (void)code;
    return "ESP_ERR";
}
