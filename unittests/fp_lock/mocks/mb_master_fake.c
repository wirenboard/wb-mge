#include "mb_master_fake.h"

#include <string.h>

#include "mb_master.h"
#include "trace.h"

#define MB_FAKE_RESULTS_MAX         64
#define MB_FAKE_RELAY_PORT          0

esp_err_t mb_fake_default_result = ESP_OK;
int       mb_fake_wrong_port_count = 0;
int       mb_fake_exchange_count = 0;

static esp_err_t mb_fake_results[MB_FAKE_RESULTS_MAX];
static unsigned  mb_fake_results_len;
static unsigned  mb_fake_results_next;

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
    memset(mb_fake_results, 0, sizeof(mb_fake_results));
    mb_fake_results_len = 0;
    mb_fake_results_next = 0;
}

static esp_err_t mb_fake_next_result(uint8_t port)
{
    mb_fake_exchange_count++;
    if (port != MB_FAKE_RELAY_PORT) {
        mb_fake_wrong_port_count++;
    }
    if (mb_fake_results_next < mb_fake_results_len) {
        return mb_fake_results[mb_fake_results_next++];
    }
    return mb_fake_default_result;
}

esp_err_t mb_master_write_coil(uint8_t port, uint8_t slave_id, uint16_t coil, bool on)
{
    trace_add("W%u.%u=%d", (unsigned)slave_id, (unsigned)coil, on ? 1 : 0);
    return mb_fake_next_result(port);
}

esp_err_t mb_master_read_coil(uint8_t port, uint8_t slave_id, uint16_t coil, bool *on)
{
    trace_add("R%u.%u", (unsigned)slave_id, (unsigned)coil);
    esp_err_t err = mb_fake_next_result(port);
    if (err == ESP_OK) {
        *on = false;
    }
    return err;
}
