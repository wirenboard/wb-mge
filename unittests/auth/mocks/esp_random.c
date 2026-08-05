#include "esp_random.h"

#define MOCK_RANDOM_QUEUE_LEN   8

static uint32_t queued[MOCK_RANDOM_QUEUE_LEN];
static int queued_count = 0;
static int queued_taken = 0;
static uint32_t counter = 0;

uint32_t esp_random(void)
{
    if (queued_taken < queued_count) {
        return queued[queued_taken++];
    }
    return ++counter;
}

void mock_esp_random_reset(void)
{
    queued_count = 0;
    queued_taken = 0;
    counter = 0;
}

void mock_esp_random_push(uint32_t value)
{
    if (queued_count < MOCK_RANDOM_QUEUE_LEN) {
        queued[queued_count++] = value;
    }
}
