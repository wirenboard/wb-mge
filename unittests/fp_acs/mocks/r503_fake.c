#include "r503_fake.h"

#include <string.h>

#include "r503.h"
#include "trace.h"

#define R503_FAKE_QUEUE_MAX         1024
#define R503_FAKE_IMAGE_STRIDE      37      // raw byte k is base + 37 k: no two neighbours alike

typedef struct {
    r503_fake_result_t result;
    uint16_t           page;
    uint16_t           score;
} r503_fake_entry_t;

typedef struct {
    r503_fake_entry_t entries[R503_FAKE_QUEUE_MAX];
    unsigned          len;
    unsigned          next;
} r503_fake_queue_t;

static r503_fake_queue_t r503_fake_gen_img_queue;
static r503_fake_queue_t r503_fake_img2tz_queue;
static r503_fake_queue_t r503_fake_search_queue;
static r503_fake_queue_t r503_fake_reg_model_queue;
static r503_fake_queue_t r503_fake_store_queue;

r503_fake_result_t r503_fake_gen_img_default = {ESP_OK, R503_CODE_NO_FINGER};
r503_fake_result_t r503_fake_search_default = {ESP_OK, R503_CODE_NOT_FOUND};
esp_err_t          r503_fake_connect_err = ESP_OK;
uint16_t           r503_fake_capacity = 200;
r503_fake_result_t r503_fake_up_image_result = {ESP_OK, R503_CODE_OK};
size_t             r503_fake_up_image_len = R503_IMAGE_RAW_SIZE;
uint8_t            r503_fake_up_image_byte = 0x1F;
char               r503_fake_last_error[64];
int                r503_fake_gen_img_calls = 0;
void (*r503_fake_gen_img_hook)(int call) = NULL;

static void r503_fake_push(r503_fake_queue_t *queue, esp_err_t err, uint8_t code, uint16_t page, uint16_t score)
{
    if (queue->len < R503_FAKE_QUEUE_MAX) {
        r503_fake_entry_t *entry = &queue->entries[queue->len++];
        entry->result.err = err;
        entry->result.code = code;
        entry->page = page;
        entry->score = score;
    }
}

// The next queued entry, or one made of the default.
static r503_fake_entry_t r503_fake_pop(r503_fake_queue_t *queue, r503_fake_result_t fallback)
{
    if (queue->next < queue->len) {
        return queue->entries[queue->next++];
    }
    r503_fake_entry_t entry = {.result = fallback};
    return entry;
}

// What a command returns: its transport result, and its code written only when that is ESP_OK.
static esp_err_t r503_fake_answer(r503_fake_result_t result, uint8_t *code)
{
    if (result.err == ESP_OK) {
        *code = result.code;
    }
    return result.err;
}

void r503_fake_push_gen_img(esp_err_t err, uint8_t code, unsigned times)
{
    for (unsigned i = 0; i < times; i++) {
        r503_fake_push(&r503_fake_gen_img_queue, err, code, 0, 0);
    }
}

void r503_fake_push_img2tz(esp_err_t err, uint8_t code)
{
    r503_fake_push(&r503_fake_img2tz_queue, err, code, 0, 0);
}

void r503_fake_push_search(esp_err_t err, uint8_t code, uint16_t page, uint16_t score)
{
    r503_fake_push(&r503_fake_search_queue, err, code, page, score);
}

void r503_fake_push_reg_model(esp_err_t err, uint8_t code)
{
    r503_fake_push(&r503_fake_reg_model_queue, err, code, 0, 0);
}

void r503_fake_push_store(esp_err_t err, uint8_t code)
{
    r503_fake_push(&r503_fake_store_queue, err, code, 0, 0);
}

void r503_fake_reset(void)
{
    memset(&r503_fake_gen_img_queue, 0, sizeof(r503_fake_gen_img_queue));
    memset(&r503_fake_img2tz_queue, 0, sizeof(r503_fake_img2tz_queue));
    memset(&r503_fake_search_queue, 0, sizeof(r503_fake_search_queue));
    memset(&r503_fake_reg_model_queue, 0, sizeof(r503_fake_reg_model_queue));
    memset(&r503_fake_store_queue, 0, sizeof(r503_fake_store_queue));
    r503_fake_gen_img_default = (r503_fake_result_t){ESP_OK, R503_CODE_NO_FINGER};
    r503_fake_search_default = (r503_fake_result_t){ESP_OK, R503_CODE_NOT_FOUND};
    r503_fake_connect_err = ESP_OK;
    r503_fake_capacity = 200;
    r503_fake_up_image_result = (r503_fake_result_t){ESP_OK, R503_CODE_OK};
    r503_fake_up_image_len = R503_IMAGE_RAW_SIZE;
    r503_fake_up_image_byte = 0x1F;
    r503_fake_last_error[0] = '\0';
    r503_fake_gen_img_calls = 0;
    r503_fake_gen_img_hook = NULL;
}


// ── r503.h ───────────────────────────────────────────────────────────────────────────────

esp_err_t r503_init(void)
{
    return ESP_OK;
}

esp_err_t r503_verify_password(uint32_t password, uint8_t *code)
{
    (void)password;
    trace_add("VfyPwd");
    return r503_fake_answer((r503_fake_result_t){r503_fake_connect_err, R503_CODE_OK}, code);
}

esp_err_t r503_read_capacity(uint16_t *capacity, uint8_t *code)
{
    trace_add("ReadSysPara");
    *capacity = r503_fake_capacity;
    return r503_fake_answer((r503_fake_result_t){ESP_OK, R503_CODE_OK}, code);
}

esp_err_t r503_gen_img(uint8_t *code)
{
    trace_add("GenImg");
    r503_fake_gen_img_calls++;
    if (r503_fake_gen_img_hook != NULL) {
        r503_fake_gen_img_hook(r503_fake_gen_img_calls);
    }
    return r503_fake_answer(r503_fake_pop(&r503_fake_gen_img_queue, r503_fake_gen_img_default).result, code);
}

esp_err_t r503_img2tz(uint8_t buffer_id, uint8_t *code)
{
    trace_add("Img2Tz%u", (unsigned)buffer_id);
    r503_fake_result_t ok = {ESP_OK, R503_CODE_OK};
    return r503_fake_answer(r503_fake_pop(&r503_fake_img2tz_queue, ok).result, code);
}

esp_err_t r503_search(uint8_t buffer_id, uint16_t start, uint16_t count, uint16_t *page, uint16_t *score,
                      uint8_t *code)
{
    (void)buffer_id;
    (void)start;
    (void)count;
    trace_add("Search");
    r503_fake_entry_t entry = r503_fake_pop(&r503_fake_search_queue, r503_fake_search_default);
    if ((entry.result.err == ESP_OK) && (entry.result.code == R503_CODE_OK)) {
        *page = entry.page;
        *score = entry.score;
    }
    return r503_fake_answer(entry.result, code);
}

esp_err_t r503_reg_model(uint8_t *code)
{
    trace_add("RegModel");
    r503_fake_result_t ok = {ESP_OK, R503_CODE_OK};
    return r503_fake_answer(r503_fake_pop(&r503_fake_reg_model_queue, ok).result, code);
}

esp_err_t r503_store(uint8_t buffer_id, uint16_t page, uint8_t *code)
{
    (void)buffer_id;
    trace_add("Store%u", (unsigned)page);
    r503_fake_result_t ok = {ESP_OK, R503_CODE_OK};
    return r503_fake_answer(r503_fake_pop(&r503_fake_store_queue, ok).result, code);
}

esp_err_t r503_delete_char(uint16_t start, uint16_t count, uint8_t *code)
{
    (void)count;
    trace_add("Delete%u", (unsigned)start);
    return r503_fake_answer((r503_fake_result_t){ESP_OK, R503_CODE_OK}, code);
}

esp_err_t r503_aura_led(uint8_t control, uint8_t speed, uint8_t colour, uint8_t count, uint8_t *code)
{
    (void)speed;
    (void)count;
    trace_add("Led%u.%u", (unsigned)control, (unsigned)colour);
    return r503_fake_answer((r503_fake_result_t){ESP_OK, R503_CODE_OK}, code);
}

uint8_t r503_fake_up_image_raw(size_t index)
{
    return (uint8_t)(r503_fake_up_image_byte + (index * R503_FAKE_IMAGE_STRIDE));
}

esp_err_t r503_up_image(uint8_t *raw, size_t raw_size, size_t *raw_len, uint8_t *code)
{
    trace_add("UpImage");
    *raw_len = 0;
    if ((r503_fake_up_image_result.err != ESP_OK) || (r503_fake_up_image_result.code != R503_CODE_OK)) {
        return r503_fake_answer(r503_fake_up_image_result, code);
    }
    size_t len = (r503_fake_up_image_len < raw_size) ? r503_fake_up_image_len : raw_size;
    for (size_t i = 0; i < len; i++) {
        raw[i] = r503_fake_up_image_raw(i);
    }
    *raw_len = len;
    return r503_fake_answer(r503_fake_up_image_result, code);
}

const char *r503_code_str(uint8_t code)
{
    switch (code) {
    case R503_CODE_OK:          return "ok";
    case R503_CODE_NO_FINGER:   return "no finger";
    case R503_CODE_NOT_FOUND:   return "not found";
    case 0x07:                  return "too few features";
    case 0x0A:                  return "the takes do not match";
    default:                    return "sensor error";
    }
}

const char *r503_last_error(void)
{
    return r503_fake_last_error;
}
