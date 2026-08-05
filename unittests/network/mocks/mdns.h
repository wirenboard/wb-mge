#pragma once

// mdns.h ships with the espressif/mdns managed component, which is not fetched for a host
// build. network.c uses exactly these two calls, so the suite carries its own declaration
// instead of pulling the component in.

#include "esp_err.h"

esp_err_t mdns_init(void);
esp_err_t mdns_hostname_set(const char *hostname);
