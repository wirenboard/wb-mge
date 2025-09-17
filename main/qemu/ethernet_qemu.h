#pragma once

#include "esp_eth_driver.h"
#include "esp_netif.h"

esp_err_t ethernet_init(esp_event_handler_t eth_event_handler, esp_netif_ip_info_t* static_ip, char * netif_hostname);
esp_eth_handle_t ethernet_get_handle(void);

// QEMU-specific functions
#if QEMU_BUILD
    esp_err_t ethernet_init_qemu(esp_event_handler_t eth_event_handler, esp_netif_ip_info_t* static_ip, char * netif_hostname);
#endif
