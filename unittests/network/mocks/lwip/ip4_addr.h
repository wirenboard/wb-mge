#pragma once

// Deliberately empty. network.c includes lwip/ip4_addr.h, but every name it actually uses
// (IPSTR / IP2STR, esp_netif_ip_info_t) comes from esp_netif_ip_addr.h. The real lwip header
// pulls in lwipopts.h — the IDF port configuration, which does not exist in a host build —
// so the include is satisfied here rather than dragging the whole TCP/IP stack in.
