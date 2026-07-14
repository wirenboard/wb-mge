#include "cache_multimaster.h"
#include "bridge.h"
#include "auth.h"
#include "modbus_helpers.h"  /* MODBUS_FC_READ_* — the cache entry type IS the FC */

#ifdef __unittest_env__
#include "malloc.h"  /* test_malloc / test_free for allocation tracking in unit tests */
#endif

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "cache_mm";

/* ---- Data model --------------------------------------------------------- */

/* Flat pool: 4096 entries × 8 bytes = 32 KB — allocated from heap on enable. */
#define CACHE_MAX_ENTRIES  4096

/* Type field bit layout:
 *   bit 7  (0x80) = used flag: 1 = slot occupied, 0 = free
 *   bits 2:0      = the Modbus read function code the value came from:
 *                   1 = FC01 coils, 2 = FC02 discrete inputs,
 *                   3 = FC03 holding regs, 4 = FC04 input regs
 *   bits 6:3      = reserved, must be 0
 * A zero byte means "free slot" — and since no valid FC is 0, a used entry can
 * never look like a free one.
 *
 * The entry type IS the Modbus function code: storing anything else would mean
 * translating FC -> type on every store and every lookup, and back again for the
 * CSV/JSON output. Only the outward-facing name (holding/input/coil/discrete) is
 * derived, in the two output handlers. The field stays one byte, so
 * cache_entry_t is unchanged at 8 bytes and the HTTP API is unaffected.
 */
#define CACHE_TYPE_MASK      0x07u  /* bits 2:0 of cache_entry_t.type = Modbus FC */
#define CACHE_USED_BIT       0x80u

/* Saturating cap for age_s counter: ~18.2 hours in seconds */
#define CACHE_AGE_MAX_S  65535u

/* One flat pool entry — 8 bytes, no padding, naturally aligned. */
typedef struct {
    uint8_t  slave_id;      /* Modbus slave address (0x00..0xFE)              */
    uint8_t  type;          /* bit7=used, bits2:0=Modbus FC (see above)       */
    uint16_t address;       /* register or coil address (0-based)             */
    uint16_t value;         /* last known value (0 or 1 for coils)            */
    uint16_t age_s;         /* seconds since last update; saturates at CACHE_AGE_MAX_S */
} cache_entry_t;

/* The 32 KB pool budget (CACHE_MAX_ENTRIES x 8) and the memory_bytes figure
 * reported by /cache/status are both computed from this size. */
_Static_assert(sizeof(cache_entry_t) == 8, "cache_entry_t must stay 8 bytes");

/** Saved context from a master request, per port */
typedef struct {
    bool     valid;
    uint8_t  slave_id;
    uint8_t  function;
    uint16_t start_reg;
    uint16_t count;
} pending_req_t;

/* ---- Module-level state ------------------------------------------------- */

/* Pool is heap-allocated on cache_multimaster_enable() and freed on disable().
 *
 * POOL INVARIANT — DENSE PREFIX:
 *   Used entries occupy a contiguous prefix s_pool[0 .. n-1]; every slot from
 *   the first free one onwards is free. It holds because entries are never
 *   released individually: find_or_alloc_entry() always takes the FIRST free
 *   slot, and the only ways a slot is freed are enable(), clear() and disable(),
 *   which all wipe the WHOLE pool at once. There is no eviction.
 *
 *   find_or_alloc_entry() and cache_multimaster_lookup() rely on this: they stop
 *   scanning at the first free slot instead of walking all CACHE_MAX_ENTRIES
 *   (4096) slots under the mutex, which for a 125-register FC03 response is the
 *   difference between ~512k and O(entries) iterations per packet.
 *
 *   IF YOU ADD PER-ENTRY EVICTION (LRU, TTL-based reclaim, a "delete one entry"
 *   API, ...) THE INVARIANT BREAKS AND THOSE EARLY EXITS BECOME SILENT BUGS:
 *   a hole left behind would hide every entry after it. Either compact the pool
 *   on removal (keeping the prefix dense) or revert both scans to a full pass.
 */
static cache_entry_t    *s_pool              = NULL;
static volatile bool     s_cache_enabled = false;
static SemaphoreHandle_t s_cache_mutex   = NULL;
static pending_req_t     s_pending[BRIDGES_COUNT];
static TaskHandle_t      s_age_task      = NULL; /* handle for cache_age_task, NULL when stopped */

/* Statistics counters — reset on cache_multimaster_clear() */
static volatile uint32_t s_packets_processed = 0; /* total response packets stored since last clear */
static volatile uint64_t s_last_packet_us    = 0; /* esp_timer_get_time() of last stored response  */
static volatile uint64_t s_reset_us          = 0; /* esp_timer_get_time() at last enable/clear      */
static volatile uint32_t s_entries_dropped   = 0; /* values dropped because the pool was full        */
/* Bumped under s_cache_mutex on every wholesale pool change (enable alloc/clear,
 * clear() wipe, disable() free). The chunked CSV/JSON handlers release the mutex
 * between chunks; they capture this generation and abort the stream if it
 * changes mid-iteration, so a concurrent disable+enable (pool reallocated) or
 * clear() can never make them emit a torn/mixed snapshot (cache-concurrency-1). */
static volatile uint32_t s_pool_generation   = 0;

/* ---- Internal helpers ---------------------------------------------------- */

/* The four read function codes the cache can store. They are contiguous
 * (FC01 coils .. FC04 input registers), which is what lets the entry type be the
 * raw function code. Anything else must never reach the pool. */
static inline bool cache_fc_is_cacheable(uint8_t fc)
{
    return (fc >= MODBUS_FC_READ_COILS) && (fc <= MODBUS_FC_READ_INPUT_REGS);
}

/* Maximum quantity the given read FC may ask for in one request. */
static inline uint16_t cache_fc_max_count(uint8_t fc)
{
    return (fc == MODBUS_FC_READ_HOLDING_REGS || fc == MODBUS_FC_READ_INPUT_REGS)
           ? MODBUS_MAX_READ_REGISTERS
           : MODBUS_MAX_READ_COILS;
}

/**
 * @brief Can a response to this request ever be stored in the cache?
 *
 * True only for a spec-legal read request: one of FC01..FC04, a quantity within
 * that FC's limit, and an address range that fits the 16-bit space. Anything
 * else (a write, a zero/oversized count, a range wrapping past 0xFFFF) can only
 * produce a response we would have to throw away, so it is never recorded as a
 * pending request in the first place.
 */
static bool request_is_cacheable(uint8_t function, uint16_t start_reg, uint16_t count)
{
    if (!cache_fc_is_cacheable(function)) return false;
    if (count == 0 || count > cache_fc_max_count(function)) return false;
    if ((uint32_t)start_reg + (uint32_t)count > 0x10000u) return false;
    return true;
}

/**
 * @brief Find an existing pool entry matching (slave_id, type, address),
 *        or allocate the first unused slot for a new entry.
 *
 * Scans the used prefix of the pool (see the POOL INVARIANT at s_pool): if an
 * exact match is found it is returned immediately; the scan stops at the first
 * free slot, which is then initialised and returned as the new entry.
 * Returns NULL when the pool is full.
 *
 * Note: the port is intentionally NOT part of the lookup key.  The cache is
 * designed as a merged view of all RS-485 ports: when the same slave address
 * is seen on multiple ports, the most recently received value wins.  This
 * matches the behaviour of cache_multimaster_lookup(), which also ignores the
 * originating port.
 *
 * @p type_value is the Modbus read function code (FC01..FC04), stored verbatim
 * in bits 2:0 of the type byte — caller must have validated it.
 *
 * Caller must hold s_cache_mutex.
 */
static cache_entry_t *find_or_alloc_entry(uint8_t slave_id,
                                           uint8_t type_value, uint16_t address)
{
    /* Pool not allocated yet — cache is disabled or enable() failed */
    if (s_pool == NULL) return NULL;

    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        cache_entry_t *e = &s_pool[i];

        /* First free slot: by the dense-prefix invariant no used entry can
         * follow it, so the search is over — claim this slot for the new entry. */
        if (!(e->type & CACHE_USED_BIT)) {
            e->type     = (uint8_t)(CACHE_USED_BIT | type_value);
            e->slave_id = slave_id;
            e->address  = address;
            e->value    = 0;
            e->age_s    = 0;
            return e;
        }

        if (e->slave_id                 == slave_id &&
            (e->type & CACHE_TYPE_MASK) == type_value &&
            e->address                  == address) {
            return e;  /* existing entry found */
        }
    }

    return NULL;  /* pool is full */
}

/**
 * @brief Count used pool entries and the unique slave IDs (devices) among them.
 *
 * Single pass over the pool. Either output pointer may be NULL when the caller
 * does not need that figure; both counts are 0 when the pool is not allocated.
 *
 * Caller must hold s_cache_mutex.
 */
static void cache_count_entries_and_devices(int *entries_out, int *devices_out)
{
    int entries = 0;
    int devices = 0;

    if (s_pool != NULL) {
        /* Bitmap of slave IDs already counted: 256 bits = 32 bytes. */
        uint8_t seen[32] = {0};
        for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
            if (!(s_pool[i].type & CACHE_USED_BIT)) continue;
            entries++;
            uint8_t sid = s_pool[i].slave_id;
            if (!(seen[sid >> 3] & (1u << (sid & 7)))) {
                seen[sid >> 3] |= (1u << (sid & 7));
                devices++;
            }
        }
    }

    if (entries_out != NULL) *entries_out = entries;
    if (devices_out != NULL) *devices_out = devices;
}


/* ---- Background aging task ----------------------------------------------- */

/* Increment age_s for every used pool entry by one second.
 * age_s saturates at CACHE_AGE_MAX_S and is never incremented beyond that.
 * Caller must hold s_cache_mutex. */
static void cache_age_tick_pool(void)
{
    if (s_pool == NULL) {
        return;
    }
    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        if ((s_pool[i].type & CACHE_USED_BIT) &&
            s_pool[i].age_s < CACHE_AGE_MAX_S) {
            s_pool[i].age_s++;
        }
    }
}

/* Background task: increment age_s for every used cache entry once per second.
 * age_s saturates at CACHE_AGE_MAX_S and is never incremented beyond that. */
static void cache_age_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (s_cache_mutex == NULL) continue;
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
        cache_age_tick_pool();
        xSemaphoreGive(s_cache_mutex);
    }
}

/* ---- Public API ---------------------------------------------------------- */

esp_err_t cache_multimaster_init(void)
{
    s_cache_mutex = xSemaphoreCreateMutex();
    if (s_cache_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create cache mutex");
        return ESP_ERR_NO_MEM;
    }
    /* s_pool is NULL here — memory is allocated lazily in cache_multimaster_enable() */
    memset(s_pending, 0, sizeof(s_pending));
    s_cache_enabled = false;
    ESP_LOGI(TAG, "Cache multimaster initialized");
    return ESP_OK;
}

void cache_multimaster_enable(void)
{
    if (s_cache_mutex == NULL) return;

    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);

    if (s_pool == NULL) {
        /* First enable (or after a disable): allocate pool from heap.
         * MALLOC_CAP_8BIT selects ordinary DRAM (or PSRAM if available). */
        s_pool = heap_caps_malloc(CACHE_MAX_ENTRIES * sizeof(cache_entry_t),
                                  MALLOC_CAP_8BIT);
        if (s_pool == NULL) {
            ESP_LOGE(TAG, "Failed to allocate cache pool (%u bytes)",
                     (unsigned)(CACHE_MAX_ENTRIES * sizeof(cache_entry_t)));
            xSemaphoreGive(s_cache_mutex);
            return;
        }
    }

    /* Clear pool (whether freshly allocated or reused after a repeated enable) */
    memset(s_pool, 0, CACHE_MAX_ENTRIES * sizeof(cache_entry_t));
    s_pool_generation++;  /* pool (re)initialised — invalidate any in-flight stream */

    /* Reset stats atomically with pool clear under the mutex.
     * s_reset_us / s_last_packet_us are uint64_t and NOT atomically
     * writable on Xtensa without a lock; cache_status_handler() reads
     * them under the same mutex. */
    s_reset_us          = esp_timer_get_time();
    s_packets_processed = 0;
    s_last_packet_us    = 0;
    s_entries_dropped   = 0;

    /* Start the aging task under the mutex to prevent concurrent enables from
     * creating duplicate tasks (TOCTOU on s_age_task == NULL check). */
    if (s_age_task == NULL) {
        BaseType_t rc = xTaskCreate(cache_age_task, "cache_age", 2048,
                                    NULL, tskIDLE_PRIORITY + 1, &s_age_task);
        if (rc != pdPASS) {
            /* Without the aging task age_s never increments, so the lookup
             * staleness check (age_s >= timeout) can never fire and stale values
             * would be served as fresh forever (mem-exhaust-2). Refuse to enable
             * a cache that cannot expire its data: roll back the pool allocation
             * and leave the cache disabled so callers fall back to live polling. */
            ESP_LOGE(TAG, "Failed to create cache_age_task (err %d) — aborting cache enable", rc);
            s_age_task = NULL; /* xTaskCreate does not clear the handle on failure */
#ifdef __unittest_env__
            test_free(s_pool);
#else
            free(s_pool);
#endif
            s_pool = NULL;
            s_pool_generation++;  /* pool freed on rollback — keep the invariant */
            xSemaphoreGive(s_cache_mutex);
            s_cache_enabled = false;
            return;
        }
    }

    /* Reset pending request state for all ports before re-activating the
     * sniffer. Done under the mutex because on_request/on_response access
     * s_pending from the sniffer task (corr-5). */
    memset(s_pending, 0, sizeof(s_pending));

    xSemaphoreGive(s_cache_mutex);

    s_cache_enabled = true;

    ESP_LOGI(TAG, "Cache multimaster enabled");
}

void cache_multimaster_disable(void)
{
    /* Disable cache first so no new data enters the pool while we are tearing
     * down — s_cache_enabled is volatile, visible immediately. */
    s_cache_enabled = false;

    /* Take the mutex before deleting the aging task to guarantee it is not
     * inside its critical section when deleted — prevents a mutex leak that
     * would cause cache_multimaster_clear() to deadlock. */
    if (s_cache_mutex != NULL) {
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    }
    if (s_age_task != NULL) {
        vTaskDelete(s_age_task);
        s_age_task = NULL;
    }
    if (s_cache_mutex != NULL) {
        xSemaphoreGive(s_cache_mutex);
    }
    /* clear() zeros the pool and resets stats under the mutex */
    cache_multimaster_clear();
    /* Free heap pool under the mutex to prevent a race with HTTP handlers */
    if (s_cache_mutex != NULL) {
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
#ifdef __unittest_env__
        test_free(s_pool);  /* use tracked free so the allocation tracker stays consistent */
#else
        free(s_pool);
#endif
        s_pool = NULL;
        s_pool_generation++;  /* pool freed — invalidate any in-flight stream */
        xSemaphoreGive(s_cache_mutex);
    }
    ESP_LOGI(TAG, "Cache multimaster disabled");
}

bool cache_multimaster_is_enabled(void)
{
    return s_cache_enabled;
}

void cache_multimaster_clear(void)
{
    if (s_cache_mutex == NULL) return;
    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    /* Only zero if the pool is allocated; do NOT free — that is done by disable(). */
    if (s_pool != NULL) {
        memset(s_pool, 0, CACHE_MAX_ENTRIES * sizeof(cache_entry_t));
        s_pool_generation++;  /* contents wiped — invalidate any in-flight stream */
    }
    /* Reset all stats atomically with pool clear while the mutex is held,
     * so cache_status_handler() never observes a zeroed pool with a stale
     * s_reset_us timestamp (TOCTOU window eliminated). */
    s_packets_processed = 0;
    s_last_packet_us    = 0;
    s_reset_us          = esp_timer_get_time();
    s_entries_dropped   = 0;
    /* Reset pending requests under the mutex: on_request/on_response read and
     * write s_pending from the sniffer task, so this cross-task reset must be
     * serialised with them (corr-5). */
    memset(s_pending, 0, sizeof(s_pending));
    xSemaphoreGive(s_cache_mutex);
    ESP_LOGI(TAG, "Cache cleared");
}

void cache_multimaster_on_request(uint8_t port, uint8_t slave_id, uint8_t function,
                                  uint16_t start_reg, uint16_t count)
{
    if (port >= BRIDGES_COUNT) return;

    /* Only a spec-legal read request can ever yield a response worth storing.
     * A write, a zero/oversized count or a wrapping address range is recorded
     * with valid = false: the slot is still overwritten (so this request retires
     * whatever was pending before it — a stale pending must never survive a new
     * request and go on to match some later response) but nothing can be stored
     * against it. This is the first half of the cache-poisoning defence; the
     * second is the response-grammar check in on_response(). */
    const bool cacheable = request_is_cacheable(function, start_reg, count);

    /* s_pending is written here and in on_response from the sniffer task, but
     * ALSO reset (memset) by enable()/clear() running on the httpd/settings
     * task — so the single-writer assumption does not hold and these accesses
     * must be serialised under s_cache_mutex to avoid a torn read in
     * on_response on a dual-core SMP device (corr-5). */
    if (s_cache_mutex != NULL) xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    s_pending[port].valid     = cacheable;
    s_pending[port].slave_id  = slave_id;
    s_pending[port].function  = function;
    s_pending[port].start_reg = start_reg;
    s_pending[port].count     = count;
    if (s_cache_mutex != NULL) xSemaphoreGive(s_cache_mutex);
}

void cache_multimaster_clear_pending(uint8_t port)
{
    if (port >= BRIDGES_COUNT) return;
    /* Serialise with the other s_pending writers (enable()/clear() reset it from
     * a different task) — see on_request (corr-5). */
    if (s_cache_mutex != NULL) xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    s_pending[port].valid = false;
    if (s_cache_mutex != NULL) xSemaphoreGive(s_cache_mutex);
}

void cache_multimaster_on_response(uint8_t port, uint8_t slave_id, uint8_t function,
                                   const uint8_t *data, uint16_t data_len,
                                   uint64_t timestamp_us)
{
    if (port >= BRIDGES_COUNT) return;
    if (data == NULL || data_len < 4) return;

    /* Read and consume the pending request under s_cache_mutex: s_pending is
     * also reset by enable()/clear() on another task, so reading these fields
     * unlocked could observe a torn state (corr-5). Copy the needed fields to
     * locals so the heavier pool work below runs without holding the lock here. */
    bool     match      = false;
    uint16_t start_addr = 0;
    uint16_t count      = 0;
    if (s_cache_mutex != NULL) xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    if (s_pending[port].valid &&
        s_pending[port].slave_id == slave_id &&
        s_pending[port].function == function) {
        match      = true;
        start_addr = s_pending[port].start_reg;
        count      = s_pending[port].count;
    }
    s_pending[port].valid = false; /* consume / reject the pending request */
    if (s_cache_mutex != NULL) xSemaphoreGive(s_cache_mutex);

    if (!match) return;

    /* data layout: [0]=slave_id [1]=FC [2]=byte_count [3..N]=data */
    uint8_t byte_count = data[2];

    if (s_cache_mutex == NULL) return;

    /* The entry type is the function code itself (see the type bit layout). */
    const uint8_t type_value = function;

    /* ---- Response grammar check — the second half of the cache-poisoning
     * defence (the first is request_is_cacheable() gating s_pending.valid).
     *
     * A pending request is matched to a response on (port, slave_id, function)
     * only: an RTU read response carries no start address, so there is nothing
     * else to bind them with. That binding is therefore weak — a late reply, a
     * duplicated slave address on the bus, or a corrupt frame that still passes
     * CRC can all be matched to a request they do not answer. Whatever such a
     * response carries would then be filed under OUR start_addr with age_s = 0,
     * i.e. another register's values presented as our own, fresh.
     *
     * The binding cannot be strengthened, but a reply whose SHAPE does not match
     * the request can be refused: a genuine answer to "read N registers" always
     * declares exactly 2*N data bytes (or ceil(N/8) for coils) and carries them.
     * Anything else is not our answer — DROP THE WHOLE RESPONSE.
     *
     * Storing the overlapping prefix instead (what the old code did: silently
     * shrinking count to the bytes actually present) is exactly the mechanism by
     * which a mismatched reply poisons the cache, so a short/long/garbled
     * response must never be trimmed into a "partial success".
     *
     * count and start_addr themselves need no re-validation here: they come from
     * a pending entry that request_is_cacheable() already vetted (FC01..FC04,
     * 1 <= count <= per-FC limit, start_addr + count <= 0x10000).
     */
    if (function == MODBUS_FC_READ_HOLDING_REGS || function == MODBUS_FC_READ_INPUT_REGS) {
        /* Holding / input registers: exactly 2 bytes per requested register.
         * count <= 125, so count*2 <= 250 and the comparison cannot overflow. */
        if (byte_count != (uint8_t)(count * 2u)) {
            ESP_LOGW(TAG, "Port %u slave %u FC%02X: byte_count %u != %u for %u regs — dropped",
                     port, slave_id, function,
                     (unsigned)byte_count, (unsigned)(count * 2u), (unsigned)count);
            return;
        }

        /* ...and the frame must actually carry the bytes it declares. */
        if ((uint32_t)3u + (uint32_t)byte_count > (uint32_t)data_len) {
            ESP_LOGW(TAG, "Port %u slave %u FC%02X: response too short — dropped",
                     port, slave_id, function);
            return;
        }

        /* Number of values this response could not store because the pool was
         * full. Recorded under the mutex, logged after releasing it: ESP_LOGW
         * goes to the UART and can block for milliseconds, and the sniffer task
         * must not hold the cache mutex for that long. */
        uint16_t dropped = 0;

        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
        for (uint16_t i = 0; i < count; i++) {
            uint16_t addr  = start_addr + i;
            uint16_t value = ((uint16_t)data[3 + i * 2] << 8) | data[3 + i * 2 + 1];
            cache_entry_t *e = find_or_alloc_entry(slave_id, type_value, addr);
            if (e == NULL) {
                /* find_or_alloc_entry returns NULL both when the pool is full
                 * and when s_pool was freed by a concurrent disable(). Only the
                 * former is a genuine pool-full drop worth counting; guard on
                 * s_pool so a disable() race does not inflate the counter. The
                 * remaining registers of this response cannot be cached — count
                 * every dropped value so the condition is observable via
                 * /cache/status instead of being silently lost (mem-exhaust-1). */
                if (s_pool != NULL) {
                    dropped = (uint16_t)(count - i);
                    s_entries_dropped += (uint32_t)dropped;
                }
                break;
            }
            e->value  = value;
            e->age_s  = 0;  /* reset age on every new value received */
        }
        /* Update stats inside the mutex: s_last_packet_us is uint64_t and is
         * NOT atomically writable on Xtensa without a lock; s_packets_processed
         * (uint32_t) would be safe alone, but keeping both updates here
         * ensures they are always consistent with each other. */
        s_packets_processed++;
        s_last_packet_us = timestamp_us;
        xSemaphoreGive(s_cache_mutex);

        /* Logged outside the critical section: the log backend can block on the
         * console and would stall every other cache user holding s_cache_mutex. */
        if (dropped > 0) {
            ESP_LOGW(TAG, "Pool full, dropping %u entries", (unsigned)dropped);
        }

    } else if (function == MODBUS_FC_READ_COILS || function == MODBUS_FC_READ_DISCRETE_INPUTS) {
        /* Coils / discrete inputs: bit-packed, LSB first (bit 0 of data[3] = coil[start_addr]).
         * Same grammar check as the register branch above: exactly ceil(count/8)
         * data bytes, no more, no less. count <= 2000, so the expected byte count
         * is at most 250 and fits a uint8_t. */
        const uint8_t expected_bytes = (uint8_t)((count + 7u) / 8u);

        if (byte_count != expected_bytes) {
            ESP_LOGW(TAG, "Port %u slave %u FC%02X: byte_count %u != %u for %u coils — dropped",
                     port, slave_id, function,
                     (unsigned)byte_count, (unsigned)expected_bytes, (unsigned)count);
            return;
        }

        if ((uint32_t)3u + (uint32_t)byte_count > (uint32_t)data_len) {
            ESP_LOGW(TAG, "Port %u slave %u FC%02X: coil response too short — dropped",
                     port, slave_id, function);
            return;
        }

        /* count is NOT widened to byte_count*8: the last data byte is padded with
         * zero bits up to the byte boundary, and those padding bits are not coils.
         * Storing them would invent up to 7 phantom coils per response, reported
         * as real values with age_s = 0. Only the count the master asked for is
         * stored. */

        /* Logged after the mutex is released — see the register branch. */
        uint16_t dropped = 0;

        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
        for (uint16_t i = 0; i < count; i++) {
            uint16_t addr  = start_addr + i;
            uint16_t value = (data[3 + i / 8] >> (i % 8)) & 1u;
            cache_entry_t *e = find_or_alloc_entry(slave_id, type_value, addr);
            if (e == NULL) {
                /* Pool full — see register branch (mem-exhaust-1). */
                if (s_pool != NULL) {
                    dropped = (uint16_t)(count - i);
                    s_entries_dropped += (uint32_t)dropped;
                }
                break;
            }
            e->value  = value;
            e->age_s  = 0;  /* reset age on every new value received */
        }
        /* Same rationale as the register branch above. */
        s_packets_processed++;
        s_last_packet_us = timestamp_us;
        xSemaphoreGive(s_cache_mutex);

        /* Logged outside the critical section — see register branch. */
        if (dropped > 0) {
            ESP_LOGW(TAG, "Pool full, dropping %u coil entries", (unsigned)dropped);
        }
    }
}

/* ---- Lookup API ---------------------------------------------------------- */

cache_lookup_result_t cache_multimaster_lookup(uint8_t slave_id, uint8_t function_code,
                                               uint16_t address, uint16_t *value_out,
                                               uint16_t value_timeout_s)
{
    if (s_cache_mutex == NULL || value_out == NULL) return CACHE_LOOKUP_NOT_FOUND;

    /* The entry type IS the function code — no mapping needed, just validation. */
    if (!cache_fc_is_cacheable(function_code)) return CACHE_LOOKUP_NOT_FOUND;
    const uint8_t type_value = function_code;

    cache_lookup_result_t result = CACHE_LOOKUP_NOT_FOUND;

    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    if (s_pool != NULL) {
        for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
            const cache_entry_t *e = &s_pool[i];

            /* Dense-prefix invariant (see s_pool): the first free slot ends the
             * used region — nothing beyond it can match. */
            if (!(e->type & CACHE_USED_BIT)) break;

            if (e->slave_id                 == slave_id &&
                (e->type & CACHE_TYPE_MASK) == type_value &&
                e->address                  == address) {
                *value_out = e->value;
                result = CACHE_LOOKUP_FOUND;

                /* Age check: age_s is maintained by cache_age_task (saturating counter).
                 * value_timeout_s == 0 disables the check — always return FOUND.
                 * The comparison is >= so that an entry reaching the timeout is
                 * stale: age_s saturates at CACHE_AGE_MAX_S (65535), so a strict >
                 * could never fire for value_timeout_s == 65535 and such entries
                 * would be served as fresh forever.                                       */
                if (value_timeout_s > 0) {
                    if (e->age_s >= value_timeout_s) {
                        result = CACHE_LOOKUP_STALE;
                    }
                }
                break;
            }
        }
    }
    xSemaphoreGive(s_cache_mutex);

    return result;
}

/* ---- Aggregate stats accessor -------------------------------------------- */

void cache_multimaster_get_stats(cache_multimaster_stats_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));

    if (s_cache_mutex == NULL || s_pool == NULL) {
        return;
    }

    int      slaves      = 0;
    uint32_t packets     = 0;
    uint64_t last_pkt_us = 0;
    uint64_t reset_us    = 0;

    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);

    /* Reading the stats counters under the same lock as the pool scan prevents
     * torn 64-bit reads of s_last_packet_us / s_reset_us on Xtensa (64-bit
     * volatile is NOT atomically readable without a mutex). */
    cache_count_entries_and_devices(NULL, &slaves);
    packets     = s_packets_processed;
    last_pkt_us = s_last_packet_us;
    reset_us    = s_reset_us;

    xSemaphoreGive(s_cache_mutex);

    uint64_t now_us          = esp_timer_get_time();
    uint64_t last_pkt_age_us = (last_pkt_us > 0 && now_us >= last_pkt_us)
                               ? (now_us - last_pkt_us) : 0;
    uint64_t map_age_us      = (reset_us > 0 && now_us >= reset_us)
                               ? (now_us - reset_us) : 0;

    out->packets_processed = packets;
    out->last_packet_age_s = (uint32_t)(last_pkt_age_us / 1000000u);
    out->map_age_s         = (uint32_t)(map_age_us / 1000000u);
    out->devices_on_bus    = (uint16_t)slaves;
}

/* ---- HTTP handlers ------------------------------------------------------- */

/* TODO: /cache/status still reflects cache_multimaster internal state.
 * Consider replacing it with a port_manager status endpoint that covers
 * all modes uniformly. */

/**
 * GET /cache/status — return enabled flag and total entry count
 */
static esp_err_t cache_status_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    int      entries      = 0;
    int      slaves       = 0;
    uint32_t packets      = 0;
    uint32_t dropped      = 0;
    uint64_t last_pkt_us  = 0;
    uint64_t reset_us     = 0;

    if (s_cache_mutex != NULL) {
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);

        /* Reading the stats counters under the same lock as the pool scan
         * prevents torn 64-bit reads of s_last_packet_us / s_reset_us on
         * Xtensa (64-bit volatile is NOT atomically readable without a mutex). */
        cache_count_entries_and_devices(&entries, &slaves);
        packets     = s_packets_processed;
        dropped     = s_entries_dropped;
        last_pkt_us = s_last_packet_us;
        reset_us    = s_reset_us;

        xSemaphoreGive(s_cache_mutex);
    }

    uint64_t now_us          = esp_timer_get_time();
    uint64_t last_pkt_age_us = (last_pkt_us > 0 && now_us >= last_pkt_us)
                               ? (now_us - last_pkt_us) : 0;
    uint64_t map_age_us      = (reset_us > 0 && now_us >= reset_us)
                               ? (now_us - reset_us) : 0;
    uint32_t memory_bytes    = (uint32_t)entries * (uint32_t)sizeof(cache_entry_t);

    /* Build response — 256 bytes: worst case ~248 bytes (two uint64_t fields at
     * 20 digits each plus the uint32_t counters); snprintf is bounded regardless. */
    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"enabled\":%s"
             ",\"entries\":%d"
             ",\"max_entries\":%d"
             ",\"slaves\":%d"
             ",\"packets_processed\":%" PRIu32
             ",\"entries_dropped\":%" PRIu32
             ",\"last_packet_age_us\":%" PRIu64
             ",\"map_age_us\":%" PRIu64
             ",\"memory_bytes\":%" PRIu32
             "}",
             s_cache_enabled ? "true" : "false",
             entries,
             CACHE_MAX_ENTRIES,
             slaves,
             packets,
             dropped,
             last_pkt_age_us,
             map_age_us,
             memory_bytes);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, (ssize_t)strlen(resp));
    return ESP_OK;
}

/**
 * GET /cache/csv — download all cached register values as a CSV file.
 *
 * Returns 409 Conflict when the cache is disabled: there is no data to export
 * and handing the user a file containing nothing but a header row is worse than
 * an explicit error.
 *
 * Uses chunked transfer: one stack-local 128-byte buffer per line so that
 * the full dataset (potentially ~240 KB) never has to be allocated at once.
 */
static esp_err_t cache_csv_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    /* This check MUST stay ahead of httpd_resp_set_type() and of the first
     * httpd_resp_send_chunk(): once a chunked response has begun, the 200 status
     * line is already on the wire and cannot be turned into a 409.
     *
     * Only the CSV endpoint does this. /cache/json is polled by the UI, where an
     * empty result set is a normal state and {"d":[]} is its correct
     * representation — an error status there would turn "cache is off" into a
     * failed request on every poll. */
    if (!cache_multimaster_is_enabled()) {
        /* ESP-IDF's httpd_err_code_t has no 409 entry, so set the status line
         * directly — the same idiom the rest of the project uses. */
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "text/plain");
        const char *msg = "cache disabled";
        httpd_resp_send(req, msg, (ssize_t)strlen(msg));
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"modbus_cache.csv\"");

    /* Send CSV header line — includes a type column for holding/input/coil/discrete */
    const char *header = "slave_id,type,address,value,age_s\r\n";
    esp_err_t ret = httpd_resp_send_chunk(req, header, (ssize_t)strlen(header));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CSV: failed to send header chunk: %d", ret);
        return ret;
    }

    if (s_cache_mutex == NULL) {
        /* Module not initialized — send empty body */
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);

    /* Snapshot the pool generation: if it changes while we are streaming (a
     * concurrent clear()/disable()+enable()), abort to avoid a torn snapshot
     * (cache-concurrency-1). */
    uint32_t gen = s_pool_generation;

    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        /* One guard at the top of every iteration covers both cases in which
         * s_pool must not be dereferenced: the pool was never allocated (cache
         * disabled), or disable()/clear()/enable() replaced or wiped it while
         * the mutex was released for the previous chunk. Stopping here falls
         * through to the normal terminator below, so a partial stream is ended
         * cleanly rather than torn. */
        if (s_pool == NULL || s_pool_generation != gen) break;

        const cache_entry_t *e = &s_pool[i];
        if (!(e->type & CACHE_USED_BIT)) continue;

        const char *type_str;
        switch (e->type & CACHE_TYPE_MASK) {
            case MODBUS_FC_READ_HOLDING_REGS:    type_str = "holding";  break;
            case MODBUS_FC_READ_INPUT_REGS:      type_str = "input";    break;
            case MODBUS_FC_READ_COILS:           type_str = "coil";     break;
            case MODBUS_FC_READ_DISCRETE_INPUTS: type_str = "discrete"; break;
            default:                             type_str = "unknown";  break;
        }

        /* Format one CSV row into a stack-local 128-byte buffer */
        char line[128];
        int len = snprintf(line, sizeof(line),
                           "%u,%s,%u,%u,%u\r\n",
                           (unsigned)e->slave_id,
                           type_str,
                           (unsigned)e->address,
                           (unsigned)e->value,
                           (unsigned)e->age_s);

        if (len < 0 || len >= (int)sizeof(line)) continue;

        /* Release the mutex while sending to avoid holding it during I/O */
        xSemaphoreGive(s_cache_mutex);
        ret = httpd_resp_send_chunk(req, line, (ssize_t)len);
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);

        if (ret != ESP_OK) {
            /* Send error — connection broken; release mutex and return error */
            xSemaphoreGive(s_cache_mutex);
            return ret;
        }
        /* A pool change during the send above is caught by the guard at the top
         * of the next iteration. */
    }

    xSemaphoreGive(s_cache_mutex);

    /* Terminate chunked transfer */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/**
 * GET /cache/json — stream all cached register values as a JSON object.
 *
 * Designed for frequent UI polling: no heap allocation, chunked transfer,
 * mutex released between chunks to avoid stalling writers.
 *
 * Output format (compact, one object per entry):
 *   {"d":[{"s":3,"t":"h","a":100,"v":1234,"age":42},...]}
 *
 * Field abbreviations (kept short for low-overhead JS parsing):
 *   s   – slave_id
 *   t   – type: "h"=holding, "i"=input, "c"=coil, "d"=discrete
 *   a   – address (0-based)
 *   v   – value
 *   age – seconds since last update (saturating at 65535 = ~18 h)
 */
static esp_err_t cache_json_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");

    if (s_cache_mutex == NULL) {
        const char *empty = "{\"d\":[]}";
        httpd_resp_send(req, empty, (ssize_t)strlen(empty));
        return ESP_OK;
    }

    bool first = true;

    /* Send the JSON object header outside the mutex */
    const char *hdr = "{\"d\":[";
    esp_err_t ret = httpd_resp_send_chunk(req, hdr, (ssize_t)strlen(hdr));
    if (ret != ESP_OK) return ret;

    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);

    /* Snapshot the pool generation: abort cleanly if it changes mid-stream
     * (concurrent clear()/disable()+enable()) to avoid a torn snapshot
     * (cache-concurrency-1). */
    uint32_t gen = s_pool_generation;

    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        /* Guards every s_pool dereference below. Covers both a pool that was
         * never allocated (cache disabled on entry) and one freed by disable()
         * or wholesale-changed by clear()/disable+enable while the mutex was
         * released to send the previous chunk. Close the array/object and stop
         * rather than emit a torn snapshot (cache-concurrency-1). */
        if (s_pool == NULL || s_pool_generation != gen) {
            xSemaphoreGive(s_cache_mutex);
            /* Close out exactly like the normal tail below, error handling included:
             * a failed send means the connection is broken, so report it instead of
             * telling the caller the response went out fine. */
            ret = httpd_resp_send_chunk(req, "]}", 2);
            if (ret != ESP_OK) return ret;
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_OK;
        }

        const cache_entry_t *e = &s_pool[i];
        if (!(e->type & CACHE_USED_BIT)) continue;

        /* Single-char type tag — shorter JSON, faster JS access */
        char type_ch;
        switch (e->type & CACHE_TYPE_MASK) {
            case MODBUS_FC_READ_HOLDING_REGS:    type_ch = 'h'; break;
            case MODBUS_FC_READ_INPUT_REGS:      type_ch = 'i'; break;
            case MODBUS_FC_READ_COILS:           type_ch = 'c'; break;
            case MODBUS_FC_READ_DISCRETE_INPUTS: type_ch = 'd'; break;
            default:                             type_ch = '?'; break;
        }

        /* Stack-local buffer: max entry ~72 chars, prefix comma = 73 */
        char buf[96];
        int len = snprintf(buf, sizeof(buf),
                           "%s{\"s\":%u,\"t\":\"%c\",\"a\":%u,\"v\":%u,\"age\":%u}",
                           first ? "" : ",",
                           (unsigned)e->slave_id,
                           type_ch,
                           (unsigned)e->address,
                           (unsigned)e->value,
                           (unsigned)e->age_s);

        if (len < 0 || len >= (int)sizeof(buf)) {
            /* Should never happen with the chosen sizes; skip silently */
            continue;
        }

        first = false;

        /* Release mutex while sending to avoid blocking sniffer callbacks */
        xSemaphoreGive(s_cache_mutex);
        ret = httpd_resp_send_chunk(req, buf, (ssize_t)len);
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);

        if (ret != ESP_OK) {
            /* Send error — connection broken; release mutex and return error */
            xSemaphoreGive(s_cache_mutex);
            return ret;
        }
        /* No pool re-check here: the top of the next iteration re-checks before
         * the next s_pool dereference, and nothing dereferences it in between. */
    }

    xSemaphoreGive(s_cache_mutex);

    /* Closing data array + closing object + terminate chunked transfer */
    ret = httpd_resp_send_chunk(req, "]}", 2);
    if (ret != ESP_OK) return ret;
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* ---- URI descriptor tables ---------------------------------------------- */

static const httpd_uri_t cache_status_uri = {
    .uri     = "/cache/status",
    .method  = HTTP_GET,
    .handler = cache_status_handler,
};

static const httpd_uri_t cache_csv_uri = {
    .uri     = "/cache/csv",
    .method  = HTTP_GET,
    .handler = cache_csv_handler,
};

static const httpd_uri_t cache_json_uri = {
    .uri     = "/cache/json",
    .method  = HTTP_GET,
    .handler = cache_json_handler,
};

esp_err_t cache_multimaster_register_handlers(httpd_handle_t server)
{
    esp_err_t ret;

    ret = httpd_register_uri_handler(server, &cache_status_uri);
    if (ret != ESP_OK) return ret;

    ret = httpd_register_uri_handler(server, &cache_csv_uri);
    if (ret != ESP_OK) return ret;

    return httpd_register_uri_handler(server, &cache_json_uri);
}

#ifdef __unittest_env__

/* Compile-time guard: if bridge.h changes BRIDGES_COUNT, this assertion fires,
 * reminding developers to update the local BRIDGES_COUNT define in
 * cache_multimaster_test.c to keep the OOB-port tests accurate. */
_Static_assert(BRIDGES_COUNT == 2,
               "Update BRIDGES_COUNT in cache_multimaster_test.c to match bridge.h");

/* Reset all module-level state for unit tests.
 * Sets s_pool to NULL without freeing: reset_malloc_tracking() in setUp()
 * handles cleanup of any leftover allocation via stdlib free, avoiding
 * double-free UB when the allocator reuses the same address within a test. */
void cache_multimaster_test_reset(void)
{
    s_pool              = NULL;
    s_cache_enabled     = false;
    s_cache_mutex       = NULL;
    /* s_pending reset here is intentionally unlocked: the host test harness is
     * single-threaded (not the corr-5 cross-task path). */
    memset(s_pending, 0, sizeof(s_pending));
    s_age_task          = NULL;
    s_packets_processed = 0;
    s_last_packet_us    = 0;
    s_reset_us          = 0;
    s_entries_dropped   = 0;
    s_pool_generation   = 0;
}

/* Returns the count of values dropped because the pool was full since the last
 * enable()/clear(). Used in unit tests only. */
uint32_t cache_multimaster_test_get_entries_dropped(void)
{
    return s_entries_dropped;
}

/* Bump the pool generation WITHOUT touching the pool — lets a test simulate a
 * concurrent clear()/disable()+enable() landing exactly while a chunked handler
 * has released the mutex, isolating the generation-abort path (cache-concurrency-1)
 * from the side effect of the pool being zeroed. Used in unit tests only. */
void cache_multimaster_test_bump_generation(void)
{
    s_pool_generation++;
}

/* Returns the value of s_pending[port].valid for assertion in unit tests.
 * Only available under __unittest_env__. */
bool cache_multimaster_test_get_pending_valid(uint8_t port)
{
    if (port >= BRIDGES_COUNT) {
        return false;
    }
    return s_pending[port].valid;
}

/* Sets age_s for the first pool entry matching (slave_id, function_code, address).
 * Returns true if the entry was found and updated. Used in unit tests only. */
bool cache_multimaster_test_set_entry_age(uint8_t slave_id, uint8_t function_code,
                                           uint16_t address, uint16_t age_s_val)
{
    /* The entry type IS the function code — see the type bit layout. */
    if (!cache_fc_is_cacheable(function_code)) return false;
    const uint8_t type_value = function_code;

    if (s_pool == NULL) return false;
    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        if ((s_pool[i].type & CACHE_USED_BIT) &&
            s_pool[i].slave_id == slave_id &&
            (s_pool[i].type & CACHE_TYPE_MASK) == type_value &&
            s_pool[i].address == address) {
            s_pool[i].age_s = age_s_val;
            return true;
        }
    }
    return false;
}

/* Performs one aging tick by calling the same cache_age_tick_pool() that
 * cache_age_task() uses — tests the real production code, not a copy. */
void cache_multimaster_test_tick_age(void)
{
    if (s_cache_mutex == NULL) return;
    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    cache_age_tick_pool();
    xSemaphoreGive(s_cache_mutex);
}

/* Expose static HTTP handlers for unit tests. */
esp_err_t cache_multimaster_test_status_handler(httpd_req_t *req)
{
    return cache_status_handler(req);
}

esp_err_t cache_multimaster_test_csv_handler(httpd_req_t *req)
{
    return cache_csv_handler(req);
}

esp_err_t cache_multimaster_test_json_handler(httpd_req_t *req)
{
    return cache_json_handler(req);
}

#endif
