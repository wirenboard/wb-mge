#pragma once

#include "lwip/netif.h"
#include "lwip/pbuf.h"

#define LWIP_HOOK_IP4_INPUT ip4_input_hook_fn

// В lwip внутри esp-idf не реализован учет статистики по интерфейсам,
// поэтому приходится самим детектировать активность на интерфейсе

// Хук-функция, вызывается lwip при получении IP-пакета
// Увеличиваем один из счетчиков статистики интерфейса на длину полученного пакета
// Счетчик используется для отслеживания активности интерфейса
static inline u8_t ip4_input_hook_fn(struct pbuf *p, struct netif *inp)
{
    inp->mib2_counters.ifinoctets += p->tot_len;
    return 0;
}
