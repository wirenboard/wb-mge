#pragma once

#include "esp_err.h"
#include <stddef.h>

typedef enum {
    EFUSE_BLK0                 = 0,   /**< Number of eFuse BLOCK0. REPEAT_DATA */

    EFUSE_BLK1                 = 1,   /**< Number of eFuse BLOCK1. SYS_DATA_PART0 */
    EFUSE_BLK_SYS_DATA_PART0   = 2,   /**< Number of eFuse BLOCK2. SYS_DATA_PART0 */

    EFUSE_BLK2                 = 2,   /**< Number of eFuse BLOCK2. SYS_DATA_PART1 */
    EFUSE_BLK_SYS_DATA_PART1   = 2,   /**< Number of eFuse BLOCK2. SYS_DATA_PART1 */

    EFUSE_BLK3                 = 3,   /**< Number of eFuse BLOCK3. KEY0. whole block */
    EFUSE_BLK_KEY0             = 3,   /**< Number of eFuse BLOCK3. KEY0. whole block */
    EFUSE_BLK_SECURE_BOOT      = 3,
    EFUSE_BLK_KEY_MAX          = 4,

    EFUSE_BLK_MAX              = 4,   /**< Number of eFuse blocks */
} esp_efuse_block_t;

esp_err_t esp_efuse_read_block(esp_efuse_block_t blk, void* dst_key, size_t offset_in_bits, size_t size_bits);

// Mock control functions
void mock_esp_efuse_set_signature(const char* signature);
void mock_esp_efuse_set_read_return(esp_err_t ret);
