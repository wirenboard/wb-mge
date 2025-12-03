#pragma once

#include "lwip/netif.h"
#include "lwip/pbuf.h"

#define LWIP_HOOK_IP4_INPUT ip4_input_hook_fn

// lwip inside esp-idf does not implement interface statistics tracking,
// so we have to detect interface activity ourselves

// Hook function, called by lwip when receiving an IP packet
// Increment one of the interface statistics counters by the received packet length
// Counter is used to track interface activity
static inline u8_t ip4_input_hook_fn(struct pbuf *p, struct netif *inp)
{
    inp->mib2_counters.ifinoctets += p->tot_len;
    return 0;
}
