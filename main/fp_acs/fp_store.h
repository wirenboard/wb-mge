#pragma once

/*
 * What the fingerprint access control remembers: its users, the sensor slots each one's
 * fingers are stored in, and the Telegram chats the bot answers in. Everything lives in its
 * own NVS partition ("fp_nvs"), namespaces "fp_users" (one blob per user) and "fp_tg" (the
 * chat list), with a RAM copy that every read is served from.
 *
 * Every function is safe to call from any task: one mutex guards the RAM copy and the NVS
 * writes. A change is written to NVS first and reaches the RAM copy only once the write has
 * succeeded, so a failed write leaves both as they were. users_rev goes up by one with every
 * change to the user list, fingers included: a reader that saw the same value twice saw the
 * same users.
 *
 * Errors of the mutating calls:
 *   ESP_ERR_INVALID_ARG      a field is out of its bounds (see the limits below)
 *   ESP_ERR_NOT_FOUND        no such user, slot or chat
 *   ESP_ERR_NO_MEM           no room: FP_STORE_USERS_MAX users, FP_STORE_FINGERS_MAX fingers of
 *                            one user, or FP_STORE_TG_CHATS_MAX chats
 *   ESP_ERR_INVALID_STATE    the store is not available (fp_store_init() failed or was not run)
 *   anything else            the NVS write itself failed
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define FP_STORE_USERS_MAX          100     // user ids are 1..FP_STORE_USERS_MAX
#define FP_STORE_NO_USER            0
#define FP_STORE_NAME_MAX           48      // bytes of UTF-8, at least one
#define FP_STORE_TG_USERNAME_MAX    32      // [A-Za-z0-9_], stored without '@', may be empty
#define FP_STORE_FINGERS_MAX        10      // sensor slots per user
#define FP_STORE_TG_CHATS_MAX       20
#define FP_STORE_TG_CHAT_ID_MAX     20      // decimal chat id, '-' allowed in front
#define FP_STORE_TG_CHAT_NAME_MAX   48      // bytes of UTF-8, may be empty

typedef struct {
    uint8_t  id;
    char     name[FP_STORE_NAME_MAX + 1];
    bool     enabled;                                   // false locks the user out everywhere
    bool     allow_fp;                                  // may open with a fingerprint
    bool     allow_tg;                                  // may open through the Telegram bot
    char     tg_username[FP_STORE_TG_USERNAME_MAX + 1];
    uint8_t  finger_count;
    uint16_t fingers[FP_STORE_FINGERS_MAX];             // sensor slots, finger_count of them
} fp_store_user_t;

// The fields a create or an update sets. A NULL string or a false set_* leaves that field
// alone on an update and at its default on a create (enabled, allow_fp: true; allow_tg: false;
// tg_username: empty). A create needs a name. A leading '@' of tg_username is dropped.
typedef struct {
    const char *name;
    const char *tg_username;
    bool        set_enabled;
    bool        enabled;
    bool        set_allow_fp;
    bool        allow_fp;
    bool        set_allow_tg;
    bool        allow_tg;
} fp_store_user_patch_t;

typedef struct {
    char id[FP_STORE_TG_CHAT_ID_MAX + 1];
    char name[FP_STORE_TG_CHAT_NAME_MAX + 1];
} fp_store_tg_chat_t;

// Mounts the "fp_nvs" partition and loads it into RAM. Logs and returns an error when the
// partition is missing (a device that has only ever been updated over the air) or unreadable;
// the store then stays unavailable and every call below reports it as empty or failing.
esp_err_t fp_store_init(void);

// ── Users ────────────────────────────────────────────────────────────────────────────────

uint32_t fp_store_users_rev(void);
// Copies up to max users, ascending by id, and returns how many; *rev (may be NULL) is the
// users_rev this copy corresponds to.
size_t   fp_store_users_snapshot(fp_store_user_t *out, size_t max, uint32_t *rev);
// out may be NULL to only test for existence.
bool     fp_store_get_user(uint8_t id, fp_store_user_t *out);
bool     fp_store_find_by_slot(uint16_t slot, fp_store_user_t *out);
// Case-insensitive; a leading '@' is ignored. An empty name matches nobody.
bool     fp_store_find_by_tg_username(const char *username, fp_store_user_t *out);
// Sensor slots referenced by some user.
size_t   fp_store_used_slots(void);
// The lowest slot in 0..capacity-1 no user references. The sensor may still hold a template
// there (a deletion that could not reach it); Store overwrites it.
bool     fp_store_alloc_slot(uint16_t capacity, uint16_t *slot);

// Creates a user with the lowest free id. out (may be NULL) receives the stored user.
esp_err_t fp_store_create_user(const fp_store_user_patch_t *patch, fp_store_user_t *out);
esp_err_t fp_store_update_user(uint8_t id, const fp_store_user_patch_t *patch, fp_store_user_t *out);
esp_err_t fp_store_set_enabled(uint8_t id, bool enabled);
// Store only: the sensor templates of the user's fingers are the caller's to delete first
// (fp_acs_delete_user() does both).
esp_err_t fp_store_delete_user(uint8_t id);
esp_err_t fp_store_add_finger(uint8_t id, uint16_t slot);
// Removes the slot from whichever user owns it. Store only, as above.
esp_err_t fp_store_remove_finger(uint16_t slot);

// ── Telegram chats the bot answers in ────────────────────────────────────────────────────

bool      fp_store_tg_chat_is_allowed(const char *chat_id);
// Adds the chat, or renames it when it is already there.
esp_err_t fp_store_tg_chat_add(const char *chat_id, const char *name);
esp_err_t fp_store_tg_chat_remove(const char *chat_id);
esp_err_t fp_store_tg_chat_clear(void);
// Copies up to max chats in the order they were added and returns how many.
size_t    fp_store_tg_chats_list(fp_store_tg_chat_t *out, size_t max);
