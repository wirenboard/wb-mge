#pragma once

/* Minimal esp_netif.h stub for the info_handlers unit test. Provides the
 * MAC/IP pair type, the IP-formatting macros and the two esp_netif entry points
 * info_handlers.c references. Functions are stubbed in mocks/esp_netif.c. */

#include "esp_err.h"
#include <stdint.h>

typedef void *esp_netif_t;

typedef struct {
    uint32_t addr;  /* IPv4 address in network byte order */
} esp_ip4_addr_t;

typedef struct {
    uint8_t        mac[6];
    esp_ip4_addr_t ip;
} esp_netif_pair_mac_ip_t;

#define IPSTR "%d.%d.%d.%d"
#define IP2STR(ipaddr) \
    (int)((ipaddr)->addr & 0xff), \
    (int)(((ipaddr)->addr >> 8) & 0xff), \
    (int)(((ipaddr)->addr >> 16) & 0xff), \
    (int)(((ipaddr)->addr >> 24) & 0xff)

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *if_key);
esp_err_t    esp_netif_dhcps_get_clients_by_mac(esp_netif_t *esp_netif, int num,
                                                esp_netif_pair_mac_ip_t *mac_ip_pair);
