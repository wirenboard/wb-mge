#include "fp_log.h"

#if FP_ACS_ENABLED

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "fp_log";

#define FP_LOG_PARTITION            "fp_nvs"
#define FP_LOG_NAMESPACE            "fp_log"
#define FP_LOG_ENTRY_KEY_FMT        "e%u"
#define FP_LOG_NEXT_KEY             "next"
#define FP_LOG_KEY_SIZE             16          // NVS keys: 15 characters and the terminator
#define FP_LOG_FIRST_SEQ            1

// Bumped whenever the layout of fp_log_blob_t changes: a blob of another version is skipped at
// load, not misread.
#define FP_LOG_BLOB_VERSION         1
// Two 32-byte NVS data entries; with the chunk header and the blob index, four entries a record.
#define FP_LOG_BLOB_SIZE_MAX        64

#define FP_LOG_SNTP_SERVER          "pool.ntp.org"
// Earlier than this, the clock is still counting from 1970 at boot: SNTP has not set it yet.
#define FP_LOG_TIME_VALID_MIN       1704067200  // 2024-01-01T00:00:00Z

// A record as NVS stores it: fixed-width fields, in an order that leaves no padding.
typedef struct {
    uint8_t  version;
    uint8_t  source;
    uint8_t  result;
    uint8_t  user_id;
    uint32_t seq;
    uint32_t time;
    uint16_t score;
    uint8_t  has_score;
    char     user_name[FP_STORE_NAME_MAX + 1];
} fp_log_blob_t;

_Static_assert(sizeof(fp_log_blob_t) <= FP_LOG_BLOB_SIZE_MAX, "an fp_log record must fit two NVS data entries");

// NULL until fp_log_init() has succeeded; guards everything below.
static SemaphoreHandle_t  fp_log_mutex;
static fp_log_entry_t    *fp_log_entries;               // the RAM copy, by slot; seq 0: empty
static uint32_t           fp_log_next = FP_LOG_FIRST_SEQ;
static bool               fp_log_nvs_ok;
static nvs_handle_t       fp_log_nvs;


static bool fp_log_lock(void)
{
    if (fp_log_mutex == NULL) {
        return false;
    }
    xSemaphoreTake(fp_log_mutex, portMAX_DELAY);
    return true;
}

static void fp_log_unlock(void)
{
    xSemaphoreGive(fp_log_mutex);
}

static uint32_t fp_log_slot(uint32_t seq)
{
    return (seq % FP_LOG_CAPACITY);
}

static void fp_log_key(uint32_t slot, char key[FP_LOG_KEY_SIZE])
{
    snprintf(key, FP_LOG_KEY_SIZE, FP_LOG_ENTRY_KEY_FMT, (unsigned)slot);
}

static uint32_t fp_log_now(void)
{
    time_t now = time(NULL);
    return (now < FP_LOG_TIME_VALID_MIN) ? FP_LOG_TIME_UNKNOWN : (uint32_t)now;
}


// ── NVS ──────────────────────────────────────────────────────────────────────────────────

static void fp_log_to_blob(const fp_log_entry_t *entry, fp_log_blob_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->version = FP_LOG_BLOB_VERSION;
    blob->source = (uint8_t)entry->source;
    blob->result = (uint8_t)entry->result;
    blob->user_id = entry->user_id;
    blob->seq = entry->seq;
    blob->time = entry->time;
    blob->score = entry->score;
    blob->has_score = entry->has_score ? 1 : 0;
    memcpy(blob->user_name, entry->user_name, sizeof(blob->user_name));
}

static void fp_log_from_blob(const fp_log_blob_t *blob, fp_log_entry_t *entry)
{
    memset(entry, 0, sizeof(*entry));
    entry->seq = blob->seq;
    entry->time = blob->time;
    entry->source = (fp_acs_source_t)blob->source;
    entry->result = (fp_acs_result_t)blob->result;
    entry->user_id = blob->user_id;
    entry->has_score = (blob->has_score != 0);
    entry->score = blob->score;
    memcpy(entry->user_name, blob->user_name, sizeof(entry->user_name));
}

// Checks what came out of NVS: the blob is ours, but the partition is not beyond corruption.
static bool fp_log_blob_valid(const fp_log_blob_t *blob, uint32_t slot)
{
    return ((blob->version == FP_LOG_BLOB_VERSION) && (blob->seq >= FP_LOG_FIRST_SEQ) &&
            (fp_log_slot(blob->seq) == slot) && (blob->source <= FP_ACS_SOURCE_TELEGRAM) &&
            (blob->result <= FP_ACS_RESULT_UNKNOWN) &&
            (memchr(blob->user_name, '\0', sizeof(blob->user_name)) != NULL));
}

// The record first, then "next": a power loss between the two leaves a record whose seq
// fp_log_load() counts past anyway.
static esp_err_t fp_log_write(const fp_log_entry_t *entry)
{
    char key[FP_LOG_KEY_SIZE];
    fp_log_key(fp_log_slot(entry->seq), key);

    fp_log_blob_t blob;
    fp_log_to_blob(entry, &blob);

    esp_err_t err = nvs_set_blob(fp_log_nvs, key, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_set_u32(fp_log_nvs, FP_LOG_NEXT_KEY, entry->seq + 1);
    }
    if (err == ESP_OK) {
        err = nvs_commit(fp_log_nvs);
    }
    return err;
}

static void fp_log_load(void)
{
    uint32_t max_seq = 0;
    size_t loaded = 0;
    for (uint32_t slot = 0; slot < FP_LOG_CAPACITY; slot++) {
        char key[FP_LOG_KEY_SIZE];
        fp_log_key(slot, key);

        fp_log_blob_t blob;
        size_t len = sizeof(blob);
        esp_err_t err = nvs_get_blob(fp_log_nvs, key, &blob, &len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }
        if ((err != ESP_OK) || (len != sizeof(blob)) || (!fp_log_blob_valid(&blob, slot))) {
            ESP_LOGW(TAG, "Record %s is unreadable (%s, %u bytes), skipped", key, esp_err_to_name(err),
                     (unsigned)len);
            continue;
        }
        fp_log_from_blob(&blob, &fp_log_entries[slot]);
        if (blob.seq > max_seq) {
            max_seq = blob.seq;
        }
        loaded++;
    }

    uint32_t next = FP_LOG_FIRST_SEQ;
    esp_err_t err = nvs_get_u32(fp_log_nvs, FP_LOG_NEXT_KEY, &next);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "\"%s\" is unreadable (%s)", FP_LOG_NEXT_KEY, esp_err_to_name(err));
        }
        next = FP_LOG_FIRST_SEQ;
    }
    fp_log_next = (next > max_seq) ? next : (max_seq + 1);
    if (fp_log_next < FP_LOG_FIRST_SEQ) {
        fp_log_next = FP_LOG_FIRST_SEQ;
    }
    ESP_LOGI(TAG, "%u records loaded, next %" PRIu32, (unsigned)loaded, fp_log_next);
}


// ── API ──────────────────────────────────────────────────────────────────────────────────

void fp_log_init(void)
{
    if (fp_log_mutex != NULL) {
        return;
    }

    fp_log_entries = heap_caps_calloc(FP_LOG_CAPACITY, sizeof(fp_log_entry_t), MALLOC_CAP_SPIRAM);
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if ((fp_log_entries == NULL) || (mutex == NULL)) {
        ESP_LOGE(TAG, "No memory for the access log: attempts are not logged");
        heap_caps_free(fp_log_entries);
        fp_log_entries = NULL;
        if (mutex != NULL) {
            vSemaphoreDelete(mutex);
        }
        return;
    }

    esp_err_t err = nvs_open_from_partition(FP_LOG_PARTITION, FP_LOG_NAMESPACE, NVS_READWRITE, &fp_log_nvs);
    if (err == ESP_OK) {
        fp_log_nvs_ok = true;
        fp_log_load();
    } else {
        ESP_LOGW(TAG, "Opening \"%s\" in \"%s\" failed (%s): the access log is kept in RAM only, until the next "
                      "reboot", FP_LOG_NAMESPACE, FP_LOG_PARTITION, esp_err_to_name(err));
    }
    // Published last: the other functions do nothing until the log is loaded.
    fp_log_mutex = mutex;
}

void fp_log_clock_start(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(FP_LOG_SNTP_SERVER);
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Starting SNTP failed (%s): access log records carry no time", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "SNTP started: %s", FP_LOG_SNTP_SERVER);
}

void fp_log_append(const fp_acs_event_t *event)
{
    if ((event == NULL) || (!fp_log_lock())) {
        return;
    }

    fp_log_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.seq = fp_log_next;
    entry.time = fp_log_now();
    entry.source = event->source;
    entry.result = event->result;
    entry.user_id = event->user_id;
    snprintf(entry.user_name, sizeof(entry.user_name), "%s", event->user_name);
    entry.has_score = event->has_score;
    entry.score = event->score;

    fp_log_entries[fp_log_slot(entry.seq)] = entry;
    fp_log_next++;
    esp_err_t err = fp_log_nvs_ok ? fp_log_write(&entry) : ESP_OK;
    fp_log_unlock();

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Writing record %" PRIu32 " failed (%s): it is kept in RAM only, until the next reboot",
                 entry.seq, esp_err_to_name(err));
    }
}

size_t fp_log_snapshot(fp_log_entry_t *out, size_t max)
{
    if ((out == NULL) || (!fp_log_lock())) {
        return 0;
    }
    // Only the last FP_LOG_CAPACITY sequence numbers count. A slot may hold none of them: it is
    // empty, its record was unreadable, or it still has the one from a lap earlier because the
    // write of the newer one failed before a reboot.
    size_t count = 0;
    uint32_t seq = fp_log_next;
    for (size_t n = 0; (n < FP_LOG_CAPACITY) && (count < max) && (seq > FP_LOG_FIRST_SEQ); n++) {
        seq--;
        const fp_log_entry_t *entry = &fp_log_entries[fp_log_slot(seq)];
        if (entry->seq == seq) {
            out[count++] = *entry;
        }
    }
    fp_log_unlock();
    return count;
}

#endif // FP_ACS_ENABLED
