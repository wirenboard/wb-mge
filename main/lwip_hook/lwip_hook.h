#pragma once

#include "lwip/netif.h"
#include "lwip/pbuf.h"

#define LWIP_HOOK_IP4_INPUT ip4_input_hook_fn

static inline u8_t ip4_input_hook_fn(struct pbuf *p, struct netif *inp)
{
    inp->mib2_counters.ifinoctets += p->tot_len;
    return 0;
}
