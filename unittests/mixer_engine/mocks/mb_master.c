#include "mb_master.h"

#include <string.h>

mock_mb_master_write_t mock_mb_master_writes[MOCK_MB_MASTER_MAX_WRITES];
int mock_mb_master_write_called = 0;
esp_err_t mock_mb_master_write_result = ESP_OK;

uint16_t  mock_mb_master_regs[MOCK_MB_MASTER_REG_COUNT];
esp_err_t mock_mb_master_read_result = ESP_OK;
int       mock_mb_master_read_called = 0;
uint8_t   mock_mb_master_last_read_port = 0;
uint8_t   mock_mb_master_last_read_slave_id = 0;
uint16_t  mock_mb_master_last_read_addr = 0;
uint16_t  mock_mb_master_last_read_count = 0;

esp_err_t mb_master_read_holding(uint8_t port, uint8_t slave_id, uint16_t addr,
                                 uint16_t count, uint16_t *out)
{
    mock_mb_master_read_called++;
    mock_mb_master_last_read_port = port;
    mock_mb_master_last_read_slave_id = slave_id;
    mock_mb_master_last_read_addr = addr;
    mock_mb_master_last_read_count = count;

    if (mock_mb_master_read_result != ESP_OK) {
        return mock_mb_master_read_result;
    }

    if ((out == NULL) || ((size_t)addr + count > MOCK_MB_MASTER_REG_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint16_t i = 0; i < count; i++) {
        out[i] = mock_mb_master_regs[addr + i];
    }
    return ESP_OK;
}

esp_err_t mb_master_write_holding(uint8_t port, uint8_t slave_id, uint16_t addr,
                                  uint16_t value)
{
    if (mock_mb_master_write_called < MOCK_MB_MASTER_MAX_WRITES) {
        mock_mb_master_writes[mock_mb_master_write_called] = (mock_mb_master_write_t){
            .port = port,
            .slave_id = slave_id,
            .addr = addr,
            .value = value,
        };
    }
    mock_mb_master_write_called++;

    return mock_mb_master_write_result;
}

void mock_mb_master_reset(void)
{
    memset(mock_mb_master_writes, 0, sizeof(mock_mb_master_writes));
    mock_mb_master_write_called = 0;
    mock_mb_master_write_result = ESP_OK;

    memset(mock_mb_master_regs, 0, sizeof(mock_mb_master_regs));
    mock_mb_master_read_result = ESP_OK;
    mock_mb_master_read_called = 0;
    mock_mb_master_last_read_port = 0;
    mock_mb_master_last_read_slave_id = 0;
    mock_mb_master_last_read_addr = 0;
    mock_mb_master_last_read_count = 0;
}
