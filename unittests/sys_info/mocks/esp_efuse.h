#pragma once

#include "esp_err.h"
#include <stddef.h>

#define TEST_DEVICE_SIGNATURE                   "TEST_SIG"
#define SIGNATURE_BLOCK                         EFUSE_BLK3
#define SIGNATURE_OFFSET_BITS                   64

typedef enum {
    EFUSE_BLK0              = 0, /**< Number of eFuse block. Reserved. */

    EFUSE_BLK1              = 1, /**< Number of eFuse block. Used for Flash Encryption. If not using that Flash Encryption feature, they can be used for another purpose. */
    EFUSE_BLK_KEY0          = 1, /**< Number of eFuse block. Used for Flash Encryption. If not using that Flash Encryption feature, they can be used for another purpose. */
    EFUSE_BLK_ENCRYPT_FLASH = 1, /**< Number of eFuse block. Used for Flash Encryption. If not using that Flash Encryption feature, they can be used for another purpose. */

    EFUSE_BLK2              = 2, /**< Number of eFuse block. Used for Secure Boot. If not using that Secure Boot feature, they can be used for another purpose. */
    EFUSE_BLK_KEY1          = 2, /**< Number of eFuse block. Used for Secure Boot. If not using that Secure Boot feature, they can be used for another purpose. */
    EFUSE_BLK_SECURE_BOOT   = 2, /**< Number of eFuse block. Used for Secure Boot. If not using that Secure Boot feature, they can be used for another purpose. */

    EFUSE_BLK3              = 3, /**< Number of eFuse block. Uses for the purpose of the user. */
    EFUSE_BLK_KEY2          = 3, /**< Number of eFuse block. Uses for the purpose of the user. */
    EFUSE_BLK_KEY_MAX       = 4,

    EFUSE_BLK_MAX           = 4,
} esp_efuse_block_t;

esp_err_t esp_efuse_read_block(esp_efuse_block_t blk, void* dst_key, size_t offset_in_bits, size_t size_bits);

void mock_esp_efuse_set_signature(const char* signature);
