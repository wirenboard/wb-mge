#include "copy_protection_helpers.h"
#include "mbedtls/md.h"
#include "freertos/FreeRTOS.h"
#include "esp_random.h"


void calc_hmac(const uint8_t mac_addr[MAC_ADDR_LEN], const uint8_t key[KEY_LEN], uint8_t out_hmac[HMAC_LEN])
{
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1); // 1 — enable HMAC mode
    mbedtls_md_hmac_starts(&ctx, key, KEY_LEN);
    mbedtls_md_hmac_update(&ctx, mac_addr, MAC_ADDR_LEN);
    mbedtls_md_hmac_finish(&ctx, out_hmac);
    mbedtls_md_free(&ctx);
}


void truncate_hmac(const uint8_t hmac[HMAC_LEN], const uint8_t swap_table[PROTECTION_CODE_LEN], uint8_t out_prot_code[PROTECTION_CODE_LEN])
{
    for (unsigned index = 0; index < PROTECTION_CODE_LEN; index++) {
        unsigned pos = swap_table[index];
        if (pos >= HMAC_LEN) {
            pos = HMAC_LEN;
        }
        out_prot_code[index] = hmac[pos];
    }
}


void unswap_array_values(const uint8_t* array, const uint8_t* swap_table, size_t len, uint8_t* out_array)
{
    memset(out_array, 0, len);
    for (unsigned index = 0; index < len; index++) {
        unsigned pos = swap_table[index];
        if (pos >= len) {
            pos = len;
        }
        out_array[pos] = array[index];
    }
}


TickType_t get_random_time(unsigned min_ms, unsigned max_ms)
{
    unsigned range = max_ms - min_ms;
    unsigned value = min_ms + esp_random() % (range + 1);

    return pdMS_TO_TICKS(value);
}


bool consttime_memeq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    uint8_t r = 0;
    for (size_t i = 0; i < n; ++i) {
        r |= x[i] ^ y[i];
    }
    return (r == 0);
}
