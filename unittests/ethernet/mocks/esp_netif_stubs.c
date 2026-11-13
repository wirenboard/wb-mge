// Stubs for the esp_netif and event loop calls ethernet_init() makes once the driver is
// installed. They do not influence which PHY driver gets installed, so they only have to
// link and succeed.

#include <stdio.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"

ESP_EVENT_DEFINE_BASE(ETH_EVENT);
ESP_EVENT_DEFINE_BASE(IP_EVENT);

const esp_netif_inherent_config_t _g_esp_netif_inherent_eth_config;
const esp_netif_netstack_config_t *_g_esp_netif_netstack_default_eth;

// esp_netif_t is opaque; ethernet.c only passes the pointer back to the calls below.
static char esp_netif_stub_object;

esp_netif_t *esp_netif_new(const esp_netif_config_t *esp_netif_config)
{
    (void)esp_netif_config;
    return (esp_netif_t *)&esp_netif_stub_object;
}

esp_err_t esp_netif_attach(esp_netif_t *esp_netif, esp_netif_iodriver_handle driver_handle)
{
    (void)esp_netif;
    (void)driver_handle;
    return ESP_OK;
}

esp_err_t esp_netif_set_hostname(esp_netif_t *esp_netif, const char *hostname)
{
    (void)esp_netif;
    (void)hostname;
    return ESP_OK;
}

esp_err_t esp_netif_dhcpc_start(esp_netif_t *esp_netif)
{
    (void)esp_netif;
    return ESP_OK;
}

esp_err_t esp_netif_dhcpc_stop(esp_netif_t *esp_netif)
{
    (void)esp_netif;
    return ESP_OK;
}

esp_err_t esp_netif_set_ip_info(esp_netif_t *esp_netif, const esp_netif_ip_info_t *ip_info)
{
    (void)esp_netif;
    (void)ip_info;
    return ESP_OK;
}

esp_err_t esp_event_handler_register(esp_event_base_t event_base, int32_t event_id,
                                     esp_event_handler_t event_handler, void *event_handler_arg)
{
    (void)event_base;
    (void)event_id;
    (void)event_handler;
    (void)event_handler_arg;
    return ESP_OK;
}

// ESP_ERROR_CHECK() lands here on failure; none of the stubs above fails.
void _esp_error_check_failed(esp_err_t rc, const char *file, int line, const char *function, const char *expression)
{
    printf("ESP_ERROR_CHECK failed: 0x%x at %s:%d in %s: %s\n", (unsigned)rc, file, line, function, expression);
    abort();
}
