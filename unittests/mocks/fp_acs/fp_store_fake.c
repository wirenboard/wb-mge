#include "fp_store_fake.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static fp_store_user_t   fp_store_fake_users[FP_STORE_USERS_MAX];     // by id - 1
static bool               fp_store_fake_present[FP_STORE_USERS_MAX];
static uint32_t           fp_store_fake_rev;
static fp_store_tg_chat_t fp_store_fake_chats[FP_STORE_TG_CHATS_MAX];
static size_t             fp_store_fake_chat_count;

esp_err_t fp_store_fake_init_result = ESP_OK;
esp_err_t fp_store_fake_add_finger_result = ESP_OK;
esp_err_t fp_store_fake_chat_result = ESP_OK;

int  fp_store_fake_get_user_calls = 0;
int  fp_store_fake_delete_user_calls = 0;
int  fp_store_fake_remove_finger_calls = 0;
int  fp_store_fake_set_enabled_calls = 0;
int  fp_store_fake_chat_add_calls = 0;
int  fp_store_fake_chat_remove_calls = 0;
int  fp_store_fake_chat_clear_calls = 0;
char fp_store_fake_last_chat_id[FP_STORE_TG_CHAT_ID_MAX + 8];
char fp_store_fake_last_chat_name[FP_STORE_TG_CHAT_NAME_MAX + 8];

static fp_store_user_t *fp_store_fake_user(uint8_t id)
{
    if ((id == FP_STORE_NO_USER) || (id > FP_STORE_USERS_MAX) || (!fp_store_fake_present[id - 1])) {
        return NULL;
    }
    return &fp_store_fake_users[id - 1];
}

static fp_store_user_t *fp_store_fake_owner(uint16_t slot)
{
    for (uint8_t id = 1; id <= FP_STORE_USERS_MAX; id++) {
        fp_store_user_t *user = fp_store_fake_user(id);
        if (user == NULL) {
            continue;
        }
        for (uint8_t i = 0; i < user->finger_count; i++) {
            if (user->fingers[i] == slot) {
                return user;
            }
        }
    }
    return NULL;
}

static int fp_store_fake_chat_index(const char *id)
{
    for (size_t i = 0; i < fp_store_fake_chat_count; i++) {
        if (strcmp(fp_store_fake_chats[i].id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

void fp_store_fake_reset(void)
{
    memset(fp_store_fake_users, 0, sizeof(fp_store_fake_users));
    memset(fp_store_fake_present, 0, sizeof(fp_store_fake_present));
    fp_store_fake_rev = 1;
    memset(fp_store_fake_chats, 0, sizeof(fp_store_fake_chats));
    fp_store_fake_chat_count = 0;
    fp_store_fake_init_result = ESP_OK;
    fp_store_fake_add_finger_result = ESP_OK;
    fp_store_fake_chat_result = ESP_OK;
    fp_store_fake_get_user_calls = 0;
    fp_store_fake_delete_user_calls = 0;
    fp_store_fake_remove_finger_calls = 0;
    fp_store_fake_set_enabled_calls = 0;
    fp_store_fake_chat_add_calls = 0;
    fp_store_fake_chat_remove_calls = 0;
    fp_store_fake_chat_clear_calls = 0;
    fp_store_fake_last_chat_id[0] = '\0';
    fp_store_fake_last_chat_name[0] = '\0';
}

fp_store_user_t *fp_store_fake_add_user(uint8_t id, const char *name, bool enabled, bool allow_fp, bool allow_tg,
                                        const char *tg_username)
{
    fp_store_user_t *user = &fp_store_fake_users[id - 1];
    memset(user, 0, sizeof(*user));
    user->id = id;
    snprintf(user->name, sizeof(user->name), "%s", name);
    user->enabled = enabled;
    user->allow_fp = allow_fp;
    user->allow_tg = allow_tg;
    snprintf(user->tg_username, sizeof(user->tg_username), "%s", (tg_username != NULL) ? tg_username : "");
    fp_store_fake_present[id - 1] = true;
    fp_store_fake_rev++;
    return user;
}

void fp_store_fake_add_slot(uint8_t id, uint16_t slot)
{
    fp_store_user_t *user = fp_store_fake_user(id);
    if ((user != NULL) && (user->finger_count < FP_STORE_FINGERS_MAX)) {
        user->fingers[user->finger_count++] = slot;
        fp_store_fake_rev++;
    }
}

void fp_store_fake_add_chat(const char *id, const char *name)
{
    if (fp_store_fake_chat_count < FP_STORE_TG_CHATS_MAX) {
        fp_store_tg_chat_t *chat = &fp_store_fake_chats[fp_store_fake_chat_count++];
        snprintf(chat->id, sizeof(chat->id), "%s", id);
        snprintf(chat->name, sizeof(chat->name), "%s", name);
    }
}


// ── fp_store.h ───────────────────────────────────────────────────────────────────────────

esp_err_t fp_store_init(void)
{
    return fp_store_fake_init_result;
}

uint32_t fp_store_users_rev(void)
{
    return fp_store_fake_rev;
}

size_t fp_store_users_snapshot(fp_store_user_t *out, size_t max, uint32_t *rev)
{
    size_t count = 0;
    for (uint8_t id = 1; (id <= FP_STORE_USERS_MAX) && (count < max); id++) {
        const fp_store_user_t *user = fp_store_fake_user(id);
        if (user != NULL) {
            out[count++] = *user;
        }
    }
    if (rev != NULL) {
        *rev = fp_store_fake_rev;
    }
    return count;
}

bool fp_store_get_user(uint8_t id, fp_store_user_t *out)
{
    fp_store_fake_get_user_calls++;
    const fp_store_user_t *user = fp_store_fake_user(id);
    if (user == NULL) {
        return false;
    }
    if (out != NULL) {
        *out = *user;
    }
    return true;
}

bool fp_store_find_by_slot(uint16_t slot, fp_store_user_t *out)
{
    const fp_store_user_t *user = fp_store_fake_owner(slot);
    if (user == NULL) {
        return false;
    }
    if (out != NULL) {
        *out = *user;
    }
    return true;
}

bool fp_store_find_by_tg_username(const char *username, fp_store_user_t *out)
{
    if (username[0] == '@') {
        username++;
    }
    if (username[0] == '\0') {
        return false;
    }
    for (uint8_t id = 1; id <= FP_STORE_USERS_MAX; id++) {
        const fp_store_user_t *user = fp_store_fake_user(id);
        if ((user == NULL) || (user->tg_username[0] == '\0')) {
            continue;
        }
        if (strcasecmp(user->tg_username, username) == 0) {
            if (out != NULL) {
                *out = *user;
            }
            return true;
        }
    }
    return false;
}

size_t fp_store_used_slots(void)
{
    size_t used = 0;
    for (uint8_t id = 1; id <= FP_STORE_USERS_MAX; id++) {
        const fp_store_user_t *user = fp_store_fake_user(id);
        if (user != NULL) {
            used += user->finger_count;
        }
    }
    return used;
}

bool fp_store_alloc_slot(uint16_t capacity, uint16_t *slot)
{
    for (uint16_t candidate = 0; candidate < capacity; candidate++) {
        if (fp_store_fake_owner(candidate) == NULL) {
            *slot = candidate;
            return true;
        }
    }
    return false;
}

esp_err_t fp_store_create_user(const fp_store_user_patch_t *patch, fp_store_user_t *out)
{
    (void)patch;
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t fp_store_update_user(uint8_t id, const fp_store_user_patch_t *patch, fp_store_user_t *out)
{
    (void)id;
    (void)patch;
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t fp_store_set_enabled(uint8_t id, bool enabled)
{
    fp_store_fake_set_enabled_calls++;
    fp_store_user_t *user = fp_store_fake_user(id);
    if (user == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    user->enabled = enabled;
    fp_store_fake_rev++;
    return ESP_OK;
}

esp_err_t fp_store_delete_user(uint8_t id)
{
    fp_store_fake_delete_user_calls++;
    if (fp_store_fake_user(id) == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    fp_store_fake_present[id - 1] = false;
    fp_store_fake_rev++;
    return ESP_OK;
}

esp_err_t fp_store_add_finger(uint8_t id, uint16_t slot)
{
    if (fp_store_fake_add_finger_result != ESP_OK) {
        return fp_store_fake_add_finger_result;
    }
    fp_store_user_t *user = fp_store_fake_user(id);
    if (user == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (user->finger_count >= FP_STORE_FINGERS_MAX) {
        return ESP_ERR_NO_MEM;
    }
    user->fingers[user->finger_count++] = slot;
    fp_store_fake_rev++;
    return ESP_OK;
}

esp_err_t fp_store_remove_finger(uint16_t slot)
{
    fp_store_fake_remove_finger_calls++;
    fp_store_user_t *user = fp_store_fake_owner(slot);
    if (user == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    for (uint8_t i = 0; i < user->finger_count; i++) {
        if (user->fingers[i] == slot) {
            user->fingers[i] = user->fingers[--user->finger_count];
            break;
        }
    }
    fp_store_fake_rev++;
    return ESP_OK;
}

bool fp_store_tg_chat_is_allowed(const char *chat_id)
{
    return (fp_store_fake_chat_index(chat_id) >= 0);
}

esp_err_t fp_store_tg_chat_add(const char *chat_id, const char *name)
{
    fp_store_fake_chat_add_calls++;
    snprintf(fp_store_fake_last_chat_id, sizeof(fp_store_fake_last_chat_id), "%s", chat_id);
    snprintf(fp_store_fake_last_chat_name, sizeof(fp_store_fake_last_chat_name), "%s", name);
    if (fp_store_fake_chat_result != ESP_OK) {
        return fp_store_fake_chat_result;
    }
    // The real store's bounds: a decimal id, '-' allowed in front, and a name of at most 48 bytes.
    size_t id_len = strlen(chat_id);
    if ((id_len == 0) || (id_len > FP_STORE_TG_CHAT_ID_MAX) || (strlen(name) > FP_STORE_TG_CHAT_NAME_MAX)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < id_len; i++) {
        if ((!isdigit((unsigned char)chat_id[i])) && (!((i == 0) && (chat_id[i] == '-')))) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    int index = fp_store_fake_chat_index(chat_id);
    if (index >= 0) {
        snprintf(fp_store_fake_chats[index].name, sizeof(fp_store_fake_chats[index].name), "%s", name);
        return ESP_OK;
    }
    if (fp_store_fake_chat_count >= FP_STORE_TG_CHATS_MAX) {
        return ESP_ERR_NO_MEM;
    }
    fp_store_fake_add_chat(chat_id, name);
    return ESP_OK;
}

esp_err_t fp_store_tg_chat_remove(const char *chat_id)
{
    fp_store_fake_chat_remove_calls++;
    snprintf(fp_store_fake_last_chat_id, sizeof(fp_store_fake_last_chat_id), "%s", chat_id);
    if (fp_store_fake_chat_result != ESP_OK) {
        return fp_store_fake_chat_result;
    }
    int index = fp_store_fake_chat_index(chat_id);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    memmove(&fp_store_fake_chats[index], &fp_store_fake_chats[index + 1],
            (fp_store_fake_chat_count - (size_t)index - 1) * sizeof(fp_store_fake_chats[0]));
    fp_store_fake_chat_count--;
    return ESP_OK;
}

esp_err_t fp_store_tg_chat_clear(void)
{
    fp_store_fake_chat_clear_calls++;
    if (fp_store_fake_chat_result != ESP_OK) {
        return fp_store_fake_chat_result;
    }
    fp_store_fake_chat_count = 0;
    return ESP_OK;
}

size_t fp_store_tg_chats_list(fp_store_tg_chat_t *out, size_t max)
{
    size_t count = (fp_store_fake_chat_count < max) ? fp_store_fake_chat_count : max;
    memcpy(out, fp_store_fake_chats, count * sizeof(*out));
    return count;
}
