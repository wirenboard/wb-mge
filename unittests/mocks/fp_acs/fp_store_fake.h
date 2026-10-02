#pragma once

/*
 * An in-memory fp_store (main/fp_acs/fp_store.h) for the suites of its callers: users with their
 * fingers and Telegram usernames, and the allowed Telegram chats. It implements the documented
 * contract of every call those callers use (lookups, slot allocation, users_rev moving on every
 * change) without NVS; the mutating calls can be made to fail.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "fp_store.h"

void fp_store_fake_reset(void);

// Adds a user as fp_store would hold it; returns it so that a test can adjust it further.
fp_store_user_t *fp_store_fake_add_user(uint8_t id, const char *name, bool enabled, bool allow_fp, bool allow_tg,
                                        const char *tg_username);
void             fp_store_fake_add_slot(uint8_t id, uint16_t slot);
void             fp_store_fake_add_chat(const char *id, const char *name);

// What fp_store_init() returns.
extern esp_err_t fp_store_fake_init_result;
// What the mutating calls return instead of doing their work; ESP_OK lets them work.
extern esp_err_t fp_store_fake_add_finger_result;
extern esp_err_t fp_store_fake_chat_result;

extern int  fp_store_fake_get_user_calls;
extern int  fp_store_fake_delete_user_calls;
extern int  fp_store_fake_remove_finger_calls;
extern int  fp_store_fake_set_enabled_calls;
extern int  fp_store_fake_chat_add_calls;
extern int  fp_store_fake_chat_remove_calls;
extern int  fp_store_fake_chat_clear_calls;
extern char fp_store_fake_last_chat_id[FP_STORE_TG_CHAT_ID_MAX + 8];
extern char fp_store_fake_last_chat_name[FP_STORE_TG_CHAT_NAME_MAX + 8];
