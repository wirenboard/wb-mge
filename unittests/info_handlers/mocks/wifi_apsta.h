#pragma once

/* Minimal wifi_apsta.h stub for the info_handlers unit test. info_handlers.c
 * includes it but only needs the Wi-Fi/netif types, which the mock esp_wifi.h
 * and esp_netif.h provide. The wifi_apsta control API is not exercised here. */

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi.h"
