#pragma once

#include "sys_info.h"
#include "freertos/FreeRTOS.h"


#define MAC_ADDR_LEN        6
#define KEY_LEN             32
#define HMAC_LEN            32


void calc_hmac(const uint8_t mac_addr[MAC_ADDR_LEN], const uint8_t key[KEY_LEN], uint8_t out_hmac[HMAC_LEN]);
void truncate_hmac(const uint8_t hmac[HMAC_LEN], const uint8_t swap_table[PROTECTION_CODE_LEN], uint8_t out_prot_code[PROTECTION_CODE_LEN]);
void unswap_array_values(const uint8_t* array, const uint8_t* swap_table, size_t len, uint8_t* out_array);
TickType_t get_random_time(unsigned min_ms, unsigned max_ms);
bool consttime_memeq(const void *a, const void *b, size_t n);
