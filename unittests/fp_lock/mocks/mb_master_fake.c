#include "mb_master_fake.h"

#include <string.h>

#include "mb_master.h"
#include "trace.h"
#include "unity.h"

#define MB_FAKE_RESULTS_MAX         64
#define MB_FAKE_RELAY_PORT          0
#define MB_FAKE_REGS_MAX            128     // registers set, written or given an error, all units together
#define MB_FAKE_COILS_MAX           32
#define MB_FAKE_READ_REGS_MAX       125     // the most one FC03 response carries, as mb_master.c allows

typedef struct {
    uint8_t   unit;
    uint16_t  reg;
    uint16_t  value;
    esp_err_t read_err;
    esp_err_t write_err;
} mb_fake_reg_t;

typedef struct {
    uint8_t  unit;
    uint16_t coil;
    bool     on;
} mb_fake_coil_t;

esp_err_t       mb_fake_default_result = ESP_OK;
int             mb_fake_wrong_port_count = 0;
int             mb_fake_exchange_count = 0;
bool            mb_fake_trace_reads = false;
mb_fake_write_t mb_fake_writes[MB_FAKE_WRITES_MAX];
unsigned        mb_fake_write_count = 0;
unsigned        mb_fake_read_count = 0;

static esp_err_t      mb_fake_results[MB_FAKE_RESULTS_MAX];
static unsigned       mb_fake_results_len;
static unsigned       mb_fake_results_next;
static mb_fake_reg_t  mb_fake_regs[MB_FAKE_REGS_MAX];
static unsigned       mb_fake_regs_len;
static mb_fake_coil_t mb_fake_coils[MB_FAKE_COILS_MAX];
static unsigned       mb_fake_coils_len;

void mb_fake_push_results(esp_err_t result, unsigned times)
{
    for (unsigned i = 0; (i < times) && (mb_fake_results_len < MB_FAKE_RESULTS_MAX); i++) {
        mb_fake_results[mb_fake_results_len++] = result;
    }
}

void mb_fake_reset(void)
{
    mb_fake_default_result = ESP_OK;
    mb_fake_wrong_port_count = 0;
    mb_fake_exchange_count = 0;
    mb_fake_trace_reads = false;
    memset(mb_fake_writes, 0, sizeof(mb_fake_writes));
    mb_fake_write_count = 0;
    mb_fake_read_count = 0;
    memset(mb_fake_results, 0, sizeof(mb_fake_results));
    mb_fake_results_len = 0;
    mb_fake_results_next = 0;
    memset(mb_fake_regs, 0, sizeof(mb_fake_regs));
    mb_fake_regs_len = 0;
    memset(mb_fake_coils, 0, sizeof(mb_fake_coils));
    mb_fake_coils_len = 0;
}

// The register's entry, added when add is set and it has none yet; NULL otherwise.
static mb_fake_reg_t *mb_fake_find_reg(uint8_t unit, uint16_t reg, bool add)
{
    for (unsigned i = 0; i < mb_fake_regs_len; i++) {
        if ((mb_fake_regs[i].unit == unit) && (mb_fake_regs[i].reg == reg)) {
            return &mb_fake_regs[i];
        }
    }
    if (!add) {
        return NULL;
    }
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(MB_FAKE_REGS_MAX, mb_fake_regs_len, "mb_master_fake: register table full");
    mb_fake_reg_t *entry = &mb_fake_regs[mb_fake_regs_len++];
    entry->unit = unit;
    entry->reg = reg;
    return entry;
}

static mb_fake_coil_t *mb_fake_find_coil(uint8_t unit, uint16_t coil, bool add)
{
    for (unsigned i = 0; i < mb_fake_coils_len; i++) {
        if ((mb_fake_coils[i].unit == unit) && (mb_fake_coils[i].coil == coil)) {
            return &mb_fake_coils[i];
        }
    }
    if (!add) {
        return NULL;
    }
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(MB_FAKE_COILS_MAX, mb_fake_coils_len, "mb_master_fake: coil table full");
    mb_fake_coil_t *entry = &mb_fake_coils[mb_fake_coils_len++];
    entry->unit = unit;
    entry->coil = coil;
    return entry;
}

void mb_fake_set_holding(uint8_t unit, uint16_t reg, uint16_t value)
{
    mb_fake_find_reg(unit, reg, true)->value = value;
}

uint16_t mb_fake_holding(uint8_t unit, uint16_t reg)
{
    const mb_fake_reg_t *entry = mb_fake_find_reg(unit, reg, false);
    return (entry != NULL) ? entry->value : 0;
}

void mb_fake_set_read_error(uint8_t unit, uint16_t reg, esp_err_t err)
{
    mb_fake_find_reg(unit, reg, true)->read_err = err;
}

void mb_fake_set_write_error(uint8_t unit, uint16_t reg, esp_err_t err)
{
    mb_fake_find_reg(unit, reg, true)->write_err = err;
}

bool mb_fake_coil(uint8_t unit, uint16_t coil)
{
    const mb_fake_coil_t *entry = mb_fake_find_coil(unit, coil, false);
    return ((entry != NULL) && entry->on);
}

static void mb_fake_count_exchange(uint8_t port)
{
    mb_fake_exchange_count++;
    if (port != MB_FAKE_RELAY_PORT) {
        mb_fake_wrong_port_count++;
    }
}

static esp_err_t mb_fake_coil_result(uint8_t port)
{
    mb_fake_count_exchange(port);
    if (mb_fake_results_next < mb_fake_results_len) {
        return mb_fake_results[mb_fake_results_next++];
    }
    return mb_fake_default_result;
}

// injected is the error set for the register(s) of the exchange, ESP_OK for none.
static esp_err_t mb_fake_holding_result(uint8_t port, esp_err_t injected)
{
    mb_fake_count_exchange(port);
    return (injected != ESP_OK) ? injected : mb_fake_default_result;
}

esp_err_t mb_master_write_coil(uint8_t port, uint8_t slave_id, uint16_t coil, bool on)
{
    trace_add("W%u.%u=%d", (unsigned)slave_id, (unsigned)coil, on ? 1 : 0);
    esp_err_t err = mb_fake_coil_result(port);
    if (err == ESP_OK) {
        mb_fake_find_coil(slave_id, coil, true)->on = on;
    }
    return err;
}

esp_err_t mb_master_read_coil(uint8_t port, uint8_t slave_id, uint16_t coil, bool *on)
{
    trace_add("R%u.%u", (unsigned)slave_id, (unsigned)coil);
    esp_err_t err = mb_fake_coil_result(port);
    if (err == ESP_OK) {
        *on = mb_fake_coil(slave_id, coil);
    }
    return err;
}

esp_err_t mb_master_read_holding(uint8_t port, uint8_t slave_id, uint16_t addr, uint16_t count, uint16_t *out)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(out, "mb_master_read_holding: no buffer");
    TEST_ASSERT_TRUE_MESSAGE((count > 0) && (count <= MB_FAKE_READ_REGS_MAX), "mb_master_read_holding: bad count");
    if (mb_fake_trace_reads) {
        if (count == 1) {
            trace_add("H%u.%u?", (unsigned)slave_id, (unsigned)addr);
        } else {
            trace_add("H%u.%u*%u?", (unsigned)slave_id, (unsigned)addr, (unsigned)count);
        }
    }
    mb_fake_read_count++;

    esp_err_t injected = ESP_OK;
    for (uint16_t i = 0; (i < count) && (injected == ESP_OK); i++) {
        const mb_fake_reg_t *entry = mb_fake_find_reg(slave_id, (uint16_t)(addr + i), false);
        if (entry != NULL) {
            injected = entry->read_err;
        }
    }
    esp_err_t err = mb_fake_holding_result(port, injected);
    if (err == ESP_OK) {
        for (uint16_t i = 0; i < count; i++) {
            out[i] = mb_fake_holding(slave_id, (uint16_t)(addr + i));
        }
    }
    return err;
}

esp_err_t mb_master_write_holding(uint8_t port, uint8_t slave_id, uint16_t addr, uint16_t value)
{
    trace_add("H%u.%u=%u", (unsigned)slave_id, (unsigned)addr, (unsigned)value);
    const mb_fake_reg_t *entry = mb_fake_find_reg(slave_id, addr, false);
    esp_err_t err = mb_fake_holding_result(port, (entry != NULL) ? entry->write_err : ESP_OK);
    if (mb_fake_write_count < MB_FAKE_WRITES_MAX) {
        mb_fake_writes[mb_fake_write_count] = (mb_fake_write_t){slave_id, addr, value, err};
    }
    mb_fake_write_count++;
    if (err == ESP_OK) {
        mb_fake_set_holding(slave_id, addr, value);
    }
    return err;
}
