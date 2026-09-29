#include "fp_store.h"

#include "fp_acs.h"

#if FP_ACS_ENABLED

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "fp_store";

#define FP_STORE_PARTITION          "fp_nvs"
#define FP_STORE_NS_USERS           "fp_users"
#define FP_STORE_NS_TG              "fp_tg"
#define FP_STORE_TG_KEY             "chats"
#define FP_STORE_USER_KEY_FMT       "u%u"
#define FP_STORE_KEY_SIZE           16          // NVS keys: 15 characters and the terminator

// Bumped whenever the layout of a blob below changes: a blob of another version is skipped at
// load, not misread.
#define FP_STORE_BLOB_VERSION       1

#define FP_STORE_TG_AT              '@'
#define FP_STORE_MINUS              '-'

// UTF-8 lead and continuation bytes, for the well-formedness check of names.
#define FP_STORE_UTF8_ASCII_MAX     0x7F
#define FP_STORE_UTF8_LEAD2_MIN     0xC2        // 0xC0 and 0xC1 only start overlong encodings
#define FP_STORE_UTF8_LEAD3_MIN     0xE0
#define FP_STORE_UTF8_LEAD4_MIN     0xF0
#define FP_STORE_UTF8_LEAD4_MAX     0xF4        // past U+10FFFF above this
#define FP_STORE_UTF8_CONT_MASK     0xC0
#define FP_STORE_UTF8_CONT_BITS     0x80

typedef struct {
    uint8_t         version;
    fp_store_user_t user;
} fp_store_user_blob_t;

typedef struct {
    uint8_t            version;
    uint8_t            count;
    fp_store_tg_chat_t chats[FP_STORE_TG_CHATS_MAX];
} fp_store_tg_blob_t;

static SemaphoreHandle_t  fp_store_mutex;
static bool               fp_store_ready;
static nvs_handle_t       fp_store_users_nvs;
static nvs_handle_t       fp_store_tg_nvs;
static uint32_t           fp_store_rev;

// The RAM copy. Users by id - 1; a free entry has id FP_STORE_NO_USER.
static fp_store_user_t    fp_store_users[FP_STORE_USERS_MAX];
static fp_store_tg_blob_t fp_store_tg;
// The chat list as it is about to be written: a change is built here, and copied over
// fp_store_tg only once NVS has taken it. Static rather than on the caller's stack (1.4 KB).
static fp_store_tg_blob_t fp_store_tg_next;


// ── Validation ───────────────────────────────────────────────────────────────────────────

static bool fp_store_utf8_valid(const char *text, size_t len)
{
    size_t i = 0;
    while (i < len) {
        uint8_t lead = (uint8_t)text[i];
        size_t extra;
        if (lead <= FP_STORE_UTF8_ASCII_MAX) {
            extra = 0;
        } else if ((lead >= FP_STORE_UTF8_LEAD2_MIN) && (lead < FP_STORE_UTF8_LEAD3_MIN)) {
            extra = 1;
        } else if ((lead >= FP_STORE_UTF8_LEAD3_MIN) && (lead < FP_STORE_UTF8_LEAD4_MIN)) {
            extra = 2;
        } else if ((lead >= FP_STORE_UTF8_LEAD4_MIN) && (lead <= FP_STORE_UTF8_LEAD4_MAX)) {
            extra = 3;
        } else {
            return false;
        }
        if (extra >= (len - i)) {
            return false;                       // the sequence is cut off by the end of the text
        }
        for (size_t k = 1; k <= extra; k++) {
            if ((((uint8_t)text[i + k]) & FP_STORE_UTF8_CONT_MASK) != FP_STORE_UTF8_CONT_BITS) {
                return false;
            }
        }
        i += extra + 1;
    }
    return true;
}

// UTF-8 of min_len..max_len bytes.
static bool fp_store_text_valid(const char *text, size_t min_len, size_t max_len)
{
    if (text == NULL) {
        return false;
    }
    size_t len = strnlen(text, max_len + 1);
    return ((len >= min_len) && (len <= max_len) && fp_store_utf8_valid(text, len));
}

// Copies a Telegram username without its leading '@'. False when what remains is longer than
// FP_STORE_TG_USERNAME_MAX or has a character Telegram does not allow in one.
static bool fp_store_tg_username_normalize(const char *in, char out[FP_STORE_TG_USERNAME_MAX + 1])
{
    if (in == NULL) {
        return false;
    }
    if (in[0] == FP_STORE_TG_AT) {
        in++;
    }
    size_t len = strnlen(in, FP_STORE_TG_USERNAME_MAX + 1);
    if (len > FP_STORE_TG_USERNAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = in[i];
        bool ok = (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) ||
                   (c == '_'));
        if (!ok) {
            return false;
        }
    }
    memcpy(out, in, len);
    out[len] = '\0';
    return true;
}

// A decimal Telegram chat id: digits, with a '-' in front for groups and channels.
static bool fp_store_tg_chat_id_valid(const char *chat_id)
{
    if (chat_id == NULL) {
        return false;
    }
    size_t len = strnlen(chat_id, FP_STORE_TG_CHAT_ID_MAX + 1);
    if ((len == 0) || (len > FP_STORE_TG_CHAT_ID_MAX)) {
        return false;
    }
    size_t i = (chat_id[0] == FP_STORE_MINUS) ? 1 : 0;
    if (i == len) {
        return false;
    }
    for (; i < len; i++) {
        if ((chat_id[i] < '0') || (chat_id[i] > '9')) {
            return false;
        }
    }
    return true;
}

// Checks what came out of NVS: the blob is ours, but the partition is not beyond corruption.
static bool fp_store_user_valid(const fp_store_user_t *user, uint8_t id)
{
    char username[FP_STORE_TG_USERNAME_MAX + 1];
    return ((user->id == id) &&
            (memchr(user->name, '\0', sizeof(user->name)) != NULL) &&
            fp_store_text_valid(user->name, 1, FP_STORE_NAME_MAX) &&
            (memchr(user->tg_username, '\0', sizeof(user->tg_username)) != NULL) &&
            fp_store_tg_username_normalize(user->tg_username, username) &&
            (user->finger_count <= FP_STORE_FINGERS_MAX));
}


// ── NVS ──────────────────────────────────────────────────────────────────────────────────

static void fp_store_user_key(uint8_t id, char key[FP_STORE_KEY_SIZE])
{
    snprintf(key, FP_STORE_KEY_SIZE, FP_STORE_USER_KEY_FMT, (unsigned)id);
}

static esp_err_t fp_store_write_user(const fp_store_user_t *user)
{
    char key[FP_STORE_KEY_SIZE];
    fp_store_user_key(user->id, key);

    fp_store_user_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.version = FP_STORE_BLOB_VERSION;
    blob.user = *user;

    esp_err_t err = nvs_set_blob(fp_store_users_nvs, key, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(fp_store_users_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Writing user %u failed: %s", (unsigned)user->id, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t fp_store_erase_user(uint8_t id)
{
    char key[FP_STORE_KEY_SIZE];
    fp_store_user_key(id, key);

    esp_err_t err = nvs_erase_key(fp_store_users_nvs, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(fp_store_users_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erasing user %u failed: %s", (unsigned)id, esp_err_to_name(err));
    }
    return err;
}

// Writes fp_store_tg_next and, once NVS has it, makes it the live list.
static esp_err_t fp_store_commit_tg(void)
{
    fp_store_tg_next.version = FP_STORE_BLOB_VERSION;
    esp_err_t err = nvs_set_blob(fp_store_tg_nvs, FP_STORE_TG_KEY, &fp_store_tg_next, sizeof(fp_store_tg_next));
    if (err == ESP_OK) {
        err = nvs_commit(fp_store_tg_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Writing the Telegram chat list failed: %s", esp_err_to_name(err));
        return err;
    }
    fp_store_tg = fp_store_tg_next;
    return ESP_OK;
}

static void fp_store_load_users(void)
{
    size_t loaded = 0;
    for (unsigned id = 1; id <= FP_STORE_USERS_MAX; id++) {
        char key[FP_STORE_KEY_SIZE];
        fp_store_user_key((uint8_t)id, key);

        fp_store_user_blob_t blob;
        size_t len = sizeof(blob);
        esp_err_t err = nvs_get_blob(fp_store_users_nvs, key, &blob, &len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }
        if ((err != ESP_OK) || (len != sizeof(blob)) || (blob.version != FP_STORE_BLOB_VERSION) ||
            (!fp_store_user_valid(&blob.user, (uint8_t)id)))
        {
            ESP_LOGW(TAG, "User record %s is unreadable (%s, %u bytes), skipped", key, esp_err_to_name(err),
                     (unsigned)len);
            continue;
        }
        fp_store_users[id - 1] = blob.user;
        loaded++;
    }
    ESP_LOGI(TAG, "%u users loaded", (unsigned)loaded);
}

static void fp_store_load_tg(void)
{
    size_t len = sizeof(fp_store_tg);
    esp_err_t err = nvs_get_blob(fp_store_tg_nvs, FP_STORE_TG_KEY, &fp_store_tg, &len);
    bool valid = ((err == ESP_OK) && (len == sizeof(fp_store_tg)) && (fp_store_tg.version == FP_STORE_BLOB_VERSION) &&
                  (fp_store_tg.count <= FP_STORE_TG_CHATS_MAX));
    for (size_t i = 0; valid && (i < fp_store_tg.count); i++) {
        const fp_store_tg_chat_t *chat = &fp_store_tg.chats[i];
        valid = ((memchr(chat->id, '\0', sizeof(chat->id)) != NULL) && fp_store_tg_chat_id_valid(chat->id) &&
                 (memchr(chat->name, '\0', sizeof(chat->name)) != NULL));
    }
    if (!valid) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Telegram chat list is unreadable (%s), starting empty", esp_err_to_name(err));
        }
        memset(&fp_store_tg, 0, sizeof(fp_store_tg));
    }
    ESP_LOGI(TAG, "%u Telegram chats loaded", (unsigned)fp_store_tg.count);
}

esp_err_t fp_store_init(void)
{
    if (fp_store_ready) {
        return ESP_OK;
    }

    fp_store_mutex = xSemaphoreCreateMutex();
    if (fp_store_mutex == NULL) {
        ESP_LOGE(TAG, "No memory for the mutex");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = nvs_flash_init_partition(FP_STORE_PARTITION);
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_LOGW(TAG, "Partition \"%s\" is not usable as it is (%s): erasing it", FP_STORE_PARTITION,
                 esp_err_to_name(err));
        err = nvs_flash_erase_partition(FP_STORE_PARTITION);
        if (err == ESP_OK) {
            err = nvs_flash_init_partition(FP_STORE_PARTITION);
        }
    }
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "No \"%s\" partition: the device still has an older partition table. An OTA update "
                      "never rewrites it - flash the firmware over serial once. Users cannot be stored until then.",
                 FP_STORE_PARTITION);
        return err;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mounting \"%s\" failed: %s", FP_STORE_PARTITION, esp_err_to_name(err));
        return err;
    }

    err = nvs_open_from_partition(FP_STORE_PARTITION, FP_STORE_NS_USERS, NVS_READWRITE, &fp_store_users_nvs);
    if (err == ESP_OK) {
        err = nvs_open_from_partition(FP_STORE_PARTITION, FP_STORE_NS_TG, NVS_READWRITE, &fp_store_tg_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Opening the namespaces failed: %s", esp_err_to_name(err));
        return err;
    }

    fp_store_load_users();
    fp_store_load_tg();
    fp_store_ready = true;
    return ESP_OK;
}


// ── Users ────────────────────────────────────────────────────────────────────────────────

// False, without taking the mutex, when there is no store to lock.
static bool fp_store_lock(void)
{
    if (!fp_store_ready) {
        return false;
    }
    xSemaphoreTake(fp_store_mutex, portMAX_DELAY);
    return true;
}

static void fp_store_unlock(void)
{
    xSemaphoreGive(fp_store_mutex);
}

static fp_store_user_t *fp_store_slot_owner(uint16_t slot)
{
    for (size_t i = 0; i < FP_STORE_USERS_MAX; i++) {
        fp_store_user_t *user = &fp_store_users[i];
        if (user->id == FP_STORE_NO_USER) {
            continue;
        }
        for (size_t f = 0; f < user->finger_count; f++) {
            if (user->fingers[f] == slot) {
                return user;
            }
        }
    }
    return NULL;
}

static fp_store_user_t *fp_store_user_by_id(uint8_t id)
{
    if ((id == FP_STORE_NO_USER) || (id > FP_STORE_USERS_MAX)) {
        return NULL;
    }
    fp_store_user_t *user = &fp_store_users[id - 1];
    return (user->id == id) ? user : NULL;
}

// Applies and validates a patch; the user is left untouched when it returns false.
static bool fp_store_apply_patch(fp_store_user_t *user, const fp_store_user_patch_t *patch)
{
    fp_store_user_t next = *user;

    if (patch->name != NULL) {
        if (!fp_store_text_valid(patch->name, 1, FP_STORE_NAME_MAX)) {
            return false;
        }
        snprintf(next.name, sizeof(next.name), "%s", patch->name);
    }
    if ((patch->tg_username != NULL) && (!fp_store_tg_username_normalize(patch->tg_username, next.tg_username))) {
        return false;
    }
    if (patch->set_enabled) {
        next.enabled = patch->enabled;
    }
    if (patch->set_allow_fp) {
        next.allow_fp = patch->allow_fp;
    }
    if (patch->set_allow_tg) {
        next.allow_tg = patch->allow_tg;
    }

    *user = next;
    return true;
}

// Writes the new version of a user and, once NVS has it, puts it in the RAM copy. Called with
// the mutex held.
static esp_err_t fp_store_commit_user(const fp_store_user_t *user, fp_store_user_t *out)
{
    esp_err_t err = fp_store_write_user(user);
    if (err != ESP_OK) {
        return err;
    }
    fp_store_users[user->id - 1] = *user;
    fp_store_rev++;
    if (out != NULL) {
        *out = *user;
    }
    return ESP_OK;
}

uint32_t fp_store_users_rev(void)
{
    if (!fp_store_lock()) {
        return 0;
    }
    uint32_t rev = fp_store_rev;
    fp_store_unlock();
    return rev;
}

size_t fp_store_users_snapshot(fp_store_user_t *out, size_t max, uint32_t *rev)
{
    if (rev != NULL) {
        *rev = 0;
    }
    if ((out == NULL) || (!fp_store_lock())) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; (i < FP_STORE_USERS_MAX) && (count < max); i++) {
        if (fp_store_users[i].id != FP_STORE_NO_USER) {
            out[count++] = fp_store_users[i];
        }
    }
    if (rev != NULL) {
        *rev = fp_store_rev;
    }
    fp_store_unlock();
    return count;
}

bool fp_store_get_user(uint8_t id, fp_store_user_t *out)
{
    if (!fp_store_lock()) {
        return false;
    }
    const fp_store_user_t *user = fp_store_user_by_id(id);
    if ((user != NULL) && (out != NULL)) {
        *out = *user;
    }
    fp_store_unlock();
    return (user != NULL);
}

bool fp_store_find_by_slot(uint16_t slot, fp_store_user_t *out)
{
    if (!fp_store_lock()) {
        return false;
    }
    const fp_store_user_t *user = fp_store_slot_owner(slot);
    if ((user != NULL) && (out != NULL)) {
        *out = *user;
    }
    fp_store_unlock();
    return (user != NULL);
}

bool fp_store_find_by_tg_username(const char *username, fp_store_user_t *out)
{
    char wanted[FP_STORE_TG_USERNAME_MAX + 1];
    if ((!fp_store_tg_username_normalize(username, wanted)) || (wanted[0] == '\0')) {
        return false;
    }
    if (!fp_store_lock()) {
        return false;
    }
    const fp_store_user_t *found = NULL;
    for (size_t i = 0; (i < FP_STORE_USERS_MAX) && (found == NULL); i++) {
        const fp_store_user_t *user = &fp_store_users[i];
        if ((user->id != FP_STORE_NO_USER) && (strcasecmp(user->tg_username, wanted) == 0)) {
            found = user;
        }
    }
    if ((found != NULL) && (out != NULL)) {
        *out = *found;
    }
    fp_store_unlock();
    return (found != NULL);
}

size_t fp_store_used_slots(void)
{
    if (!fp_store_lock()) {
        return 0;
    }
    size_t used = 0;
    for (size_t i = 0; i < FP_STORE_USERS_MAX; i++) {
        if (fp_store_users[i].id != FP_STORE_NO_USER) {
            used += fp_store_users[i].finger_count;
        }
    }
    fp_store_unlock();
    return used;
}

bool fp_store_alloc_slot(uint16_t capacity, uint16_t *slot)
{
    if (!fp_store_lock()) {
        return false;
    }
    bool found = false;
    for (uint32_t candidate = 0; (candidate < capacity) && (!found); candidate++) {
        if (fp_store_slot_owner((uint16_t)candidate) == NULL) {
            *slot = (uint16_t)candidate;
            found = true;
        }
    }
    fp_store_unlock();
    return found;
}

esp_err_t fp_store_create_user(const fp_store_user_patch_t *patch, fp_store_user_t *out)
{
    if ((patch == NULL) || (patch->name == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    fp_store_user_t user;
    memset(&user, 0, sizeof(user));
    user.enabled = true;
    user.allow_fp = true;
    user.allow_tg = false;
    if (!fp_store_apply_patch(&user, patch)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!fp_store_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_NO_MEM;
    for (size_t i = 0; i < FP_STORE_USERS_MAX; i++) {
        if (fp_store_users[i].id == FP_STORE_NO_USER) {
            user.id = (uint8_t)(i + 1);
            err = fp_store_commit_user(&user, out);
            break;
        }
    }
    fp_store_unlock();

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "User %u \"%s\" created", (unsigned)user.id, user.name);
    }
    return err;
}

esp_err_t fp_store_update_user(uint8_t id, const fp_store_user_patch_t *patch, fp_store_user_t *out)
{
    if (patch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!fp_store_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NOT_FOUND;
    const fp_store_user_t *current = fp_store_user_by_id(id);
    if (current != NULL) {
        fp_store_user_t user = *current;
        err = fp_store_apply_patch(&user, patch) ? fp_store_commit_user(&user, out) : ESP_ERR_INVALID_ARG;
    }
    fp_store_unlock();
    return err;
}

esp_err_t fp_store_set_enabled(uint8_t id, bool enabled)
{
    fp_store_user_patch_t patch = {
        .set_enabled = true,
        .enabled = enabled,
    };
    return fp_store_update_user(id, &patch, NULL);
}

esp_err_t fp_store_delete_user(uint8_t id)
{
    if (!fp_store_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NOT_FOUND;
    fp_store_user_t *user = fp_store_user_by_id(id);
    if (user != NULL) {
        err = fp_store_erase_user(id);
        if (err == ESP_OK) {
            memset(user, 0, sizeof(*user));
            fp_store_rev++;
        }
    }
    fp_store_unlock();

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "User %u deleted", (unsigned)id);
    }
    return err;
}

esp_err_t fp_store_add_finger(uint8_t id, uint16_t slot)
{
    if (!fp_store_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;
    const fp_store_user_t *current = fp_store_user_by_id(id);
    if (current == NULL) {
        err = ESP_ERR_NOT_FOUND;
    } else if (current->finger_count >= FP_STORE_FINGERS_MAX) {
        err = ESP_ERR_NO_MEM;
    } else if (fp_store_slot_owner(slot) != NULL) {
        err = ESP_ERR_INVALID_ARG;
    } else {
        fp_store_user_t user = *current;
        user.fingers[user.finger_count++] = slot;
        err = fp_store_commit_user(&user, NULL);
    }
    fp_store_unlock();
    return err;
}

esp_err_t fp_store_remove_finger(uint16_t slot)
{
    if (!fp_store_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NOT_FOUND;
    const fp_store_user_t *owner = fp_store_slot_owner(slot);
    if (owner != NULL) {
        fp_store_user_t user = *owner;
        size_t kept = 0;
        for (size_t f = 0; f < user.finger_count; f++) {
            if (user.fingers[f] != slot) {
                user.fingers[kept++] = user.fingers[f];
            }
        }
        user.finger_count = (uint8_t)kept;
        err = fp_store_commit_user(&user, NULL);
    }
    fp_store_unlock();
    return err;
}


// ── Telegram chats ───────────────────────────────────────────────────────────────────────

// Index of the chat in fp_store_tg, or -1.
static int fp_store_tg_find(const char *chat_id)
{
    for (size_t i = 0; i < fp_store_tg.count; i++) {
        if (strcmp(fp_store_tg.chats[i].id, chat_id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

bool fp_store_tg_chat_is_allowed(const char *chat_id)
{
    if ((!fp_store_tg_chat_id_valid(chat_id)) || (!fp_store_lock())) {
        return false;
    }
    bool allowed = (fp_store_tg_find(chat_id) >= 0);
    fp_store_unlock();
    return allowed;
}

esp_err_t fp_store_tg_chat_add(const char *chat_id, const char *name)
{
    if ((!fp_store_tg_chat_id_valid(chat_id)) || (!fp_store_text_valid(name, 0, FP_STORE_TG_CHAT_NAME_MAX))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!fp_store_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;
    fp_store_tg_next = fp_store_tg;
    int index = fp_store_tg_find(chat_id);
    if (index < 0) {
        if (fp_store_tg_next.count >= FP_STORE_TG_CHATS_MAX) {
            err = ESP_ERR_NO_MEM;
        } else {
            index = fp_store_tg_next.count++;
            snprintf(fp_store_tg_next.chats[index].id, sizeof(fp_store_tg_next.chats[index].id), "%s", chat_id);
        }
    }
    if (err == ESP_OK) {
        snprintf(fp_store_tg_next.chats[index].name, sizeof(fp_store_tg_next.chats[index].name), "%s", name);
        err = fp_store_commit_tg();
    }
    fp_store_unlock();
    return err;
}

esp_err_t fp_store_tg_chat_remove(const char *chat_id)
{
    if (!fp_store_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NOT_FOUND;
    int index = (chat_id != NULL) ? fp_store_tg_find(chat_id) : -1;
    if (index >= 0) {
        fp_store_tg_next = fp_store_tg;
        for (size_t i = (size_t)index + 1; i < fp_store_tg_next.count; i++) {
            fp_store_tg_next.chats[i - 1] = fp_store_tg_next.chats[i];
        }
        fp_store_tg_next.count--;
        memset(&fp_store_tg_next.chats[fp_store_tg_next.count], 0, sizeof(fp_store_tg_next.chats[0]));
        err = fp_store_commit_tg();
    }
    fp_store_unlock();
    return err;
}

esp_err_t fp_store_tg_chat_clear(void)
{
    if (!fp_store_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(&fp_store_tg_next, 0, sizeof(fp_store_tg_next));
    esp_err_t err = fp_store_commit_tg();
    fp_store_unlock();
    return err;
}

size_t fp_store_tg_chats_list(fp_store_tg_chat_t *out, size_t max)
{
    if ((out == NULL) || (!fp_store_lock())) {
        return 0;
    }
    size_t count = (fp_store_tg.count < max) ? fp_store_tg.count : max;
    memcpy(out, fp_store_tg.chats, count * sizeof(out[0]));
    fp_store_unlock();
    return count;
}

#endif // FP_ACS_ENABLED
