#include "unity.h"

#include "esp_efuse.h"
#include "wb_app_desc.h"
#include "sys_info.h"

#include <string.h>
#include <stdint.h>

#define BLOCK_SIZE                              32

#define SIGNATURE_EFUSE_BLOCK                   EFUSE_BLK3
#define SIGNATURE_EFUSE_OFFSET                  8

#define PROTECTION_CODE_EFUSE_BLOCK             EFUSE_BLK3
#define PROTECTION_CODE_EFUSE_OFFSET            (SIGNATURE_EFUSE_OFFSET + DEVICE_SIGNATURE_LEN)

#define WIFI_PASS_EFUSE_BLOCK                   EFUSE_BLK2
#define WIFI_PASS_EFUSE_OFFSET                  0
#define WIFI_PASS_EFUSE_MAX_LEN                 12

esp_err_t mock_esp_efuse_read_block_return = ESP_OK;

static uint8_t efuse_blocks[EFUSE_BLK_MAX][BLOCK_SIZE] = {0};

static void mock_esp_efuse_write_block(esp_efuse_block_t blk, const void* src_key, size_t offset_in_bits, size_t size_bits)
{
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, offset_in_bits % 8, "Offset in bits must be a multiple of 8");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, size_bits % 8, "Size in bits must be a multiple of 8");

    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(0, blk, "eFuse block number must be >= 0");
    TEST_ASSERT_LESS_THAN_INT_MESSAGE(EFUSE_BLK_MAX, blk, "eFuse block number must be < EFUSE_BLK_MAX");

    size_t offset_bytes = offset_in_bits / 8;
    size_t size_bytes = size_bits / 8;

    TEST_ASSERT_LESS_OR_EQUAL_size_t_MESSAGE(BLOCK_SIZE, offset_bytes + size_bytes, "eFuse block out of bounds while writing");

    memcpy(&efuse_blocks[blk][offset_bytes], src_key, size_bytes);
}

void mock_esp_efuse_set_signature(const char* signature)
{
    uint8_t sig_buffer[DEVICE_SIGNATURE_LEN];
    memset(sig_buffer, 0, DEVICE_SIGNATURE_LEN);

    size_t sig_len = strlen(signature);
    if (sig_len > DEVICE_SIGNATURE_LEN) {
        sig_len = DEVICE_SIGNATURE_LEN;
    }

    memcpy(sig_buffer, signature, sig_len);
    mock_esp_efuse_write_block(SIGNATURE_EFUSE_BLOCK, sig_buffer, SIGNATURE_EFUSE_OFFSET * 8, DEVICE_SIGNATURE_LEN * 8);
}

void mock_esp_efuse_set_protection_code(const uint8_t* prot_code)
{
    mock_esp_efuse_write_block(PROTECTION_CODE_EFUSE_BLOCK, prot_code, PROTECTION_CODE_EFUSE_OFFSET * 8, PROTECTION_CODE_LEN * 8);
}

void mock_esp_efuse_set_wifi_password(const char* wifi_pass)
{
    size_t pass_len = strlen(wifi_pass);

    mock_esp_efuse_write_block(WIFI_PASS_EFUSE_BLOCK, wifi_pass, WIFI_PASS_EFUSE_OFFSET * 8, pass_len * 8);

    if (pass_len < WIFI_PASS_EFUSE_MAX_LEN) {
        uint8_t zeros[WIFI_PASS_EFUSE_MAX_LEN] = {0};
        mock_esp_efuse_write_block(
            WIFI_PASS_EFUSE_BLOCK, zeros, (WIFI_PASS_EFUSE_OFFSET + pass_len) * 8, (WIFI_PASS_EFUSE_MAX_LEN - pass_len) * 8
        );
    }
}

esp_err_t esp_efuse_read_block(esp_efuse_block_t blk, void* dst_key, size_t offset_in_bits, size_t size_bits)
{
    if (mock_esp_efuse_read_block_return != ESP_OK) {
        return mock_esp_efuse_read_block_return;
    }

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, offset_in_bits % 8, "Offset in bits must be a multiple of 8");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, size_bits % 8, "Size in bits must be a multiple of 8");

    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(0, blk, "eFuse block number must be >= 0");
    TEST_ASSERT_LESS_THAN_INT_MESSAGE(EFUSE_BLK_MAX, blk, "eFuse block number must be < EFUSE_BLK_MAX");

    size_t offset_bytes = offset_in_bits / 8;
    size_t size_bytes = size_bits / 8;

    TEST_ASSERT_LESS_OR_EQUAL_size_t_MESSAGE(BLOCK_SIZE, offset_bytes + size_bytes, "eFuse block out of bounds while reading");

    memcpy(dst_key, &efuse_blocks[blk][offset_bytes], size_bytes);

    return mock_esp_efuse_read_block_return;
}

void mock_esp_efuse_reset(void)
{
    mock_esp_efuse_read_block_return = ESP_OK;
    memset(efuse_blocks, 0, sizeof(efuse_blocks));
    mock_esp_efuse_set_signature(TEST_DEVICE_SIGNATURE);
}
