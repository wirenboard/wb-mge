#pragma once

/*
 * The R503 driver (main/fp_acs/r503.h) as fp_acs sees it: every command adds its name to the
 * trace ("GenImg", "Img2Tz1", "Search", "Led2.4", "UpImage", "RegModel", "Store5", "Delete5",
 * "VfyPwd", "ReadSysPara") and answers with the next result queued for it, or with its default
 * once the queue is used up. A result is the transport error and, when that is ESP_OK, the
 * confirmation code the command writes into *code during the call.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    esp_err_t err;
    uint8_t   code;
} r503_fake_result_t;

// Defaults: GenImg no finger, Img2Tz / RegModel / Store / DeletChar / AuraLed ok, Search not found.
extern r503_fake_result_t r503_fake_gen_img_default;
extern r503_fake_result_t r503_fake_search_default;

// VfyPwd and ReadSysPara.
extern esp_err_t r503_fake_connect_err;
extern uint16_t  r503_fake_capacity;

// UpImage: its result, how many bytes arrive (R503_IMAGE_RAW_SIZE unless set) and the value of the
// first; raw byte k is r503_fake_up_image_raw(k).
extern r503_fake_result_t r503_fake_up_image_result;
extern size_t             r503_fake_up_image_len;
extern uint8_t            r503_fake_up_image_byte;
uint8_t                   r503_fake_up_image_raw(size_t index);

// What r503_last_error() returns.
extern char r503_fake_last_error[64];

extern int  r503_fake_gen_img_calls;
// Called inside every GenImg, before it answers; call counts from 1.
extern void (*r503_fake_gen_img_hook)(int call);

void r503_fake_push_gen_img(esp_err_t err, uint8_t code, unsigned times);
void r503_fake_push_img2tz(esp_err_t err, uint8_t code);
void r503_fake_push_search(esp_err_t err, uint8_t code, uint16_t page, uint16_t score);
void r503_fake_push_reg_model(esp_err_t err, uint8_t code);
void r503_fake_push_store(esp_err_t err, uint8_t code);
void r503_fake_reset(void);
