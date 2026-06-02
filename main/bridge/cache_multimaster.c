#include "cache_multimaster.h"
#include "bridge.h"
#include "auth.h"

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
 *   bits 1:0      = register type: 0=holding(FC03), 1=input(FC04),
 *                                  2=coil(FC01),    3=discrete(FC02)
 *   bits 6:2      = reserved, must be 0
 * A zero byte means "free slot".
 */
#define CACHE_TYPE_HOLDING   0u
#define CACHE_TYPE_INPUT     1u
#define CACHE_TYPE_COIL      2u
#define CACHE_TYPE_DISCRETE  3u
#define CACHE_USED_BIT       0x80u

/* Saturating cap for age_s counter: ~18.2 hours in seconds */
#define CACHE_AGE_MAX_S  65535u

/* One flat pool entry — 8 bytes, no padding, naturally aligned. */
typedef struct {
    uint8_t  slave_id;      /* Modbus slave address (0x00..0xFE)              */
    uint8_t  type;          /* bit7=used, bits1:0=register type (see above)   */
    uint16_t address;       /* register or coil address (0-based)             */
    uint16_t value;         /* last known value (0 or 1 for coils)            */
    uint16_t age_s;         /* seconds since last update; saturates at CACHE_AGE_MAX_S */
} cache_entry_t;

/** Saved context from a master request, per port */
typedef struct {
    bool     valid;
    uint8_t  slave_id;
    uint8_t  function;
    uint16_t start_reg;
    uint16_t count;
} pending_req_t;

/* ---- Module-level state ------------------------------------------------- */

/* Pool is heap-allocated on cache_multimaster_enable() and freed on disable(). */
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

/* ---- Internal helpers ---------------------------------------------------- */

/**
 * @brief Find an existing pool entry matching (slave_id, type, address),
 *        or allocate the first unused slot for a new entry.
 *
 * Performs a single linear scan: if an exact match is found it is returned
 * immediately; otherwise the first unused slot is initialised and returned.
 * Returns NULL when the pool is full.
 *
 * Note: the port is intentionally NOT part of the lookup key.  The cache is
 * designed as a merged view of all RS-485 ports: when the same slave address
 * is seen on multiple ports, the most recently received value wins.  This
 * matches the behaviour of cache_multimaster_lookup(), which also ignores the
 * originating port.
 *
 * @p type_value must be one of CACHE_TYPE_HOLDING / INPUT / COIL / DISCRETE
 * (bits 1:0 only, without CACHE_USED_BIT).
 *
 * Caller must hold s_cache_mutex.
 */
static cache_entry_t *find_or_alloc_entry(uint8_t slave_id,
                                           uint8_t type_value, uint16_t address)
{
    /* Pool not allocated yet — cache is disabled or enable() failed */
    if (s_pool == NULL) return NULL;

    cache_entry_t *free_slot = NULL;

    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        if ((s_pool[i].type & CACHE_USED_BIT) &&
            s_pool[i].slave_id              == slave_id &&
            (s_pool[i].type & 0x03u)        == type_value &&
            s_pool[i].address               == address) {
            return &s_pool[i];  /* existing entry found */
        }
        if (!(s_pool[i].type & CACHE_USED_BIT) && free_slot == NULL) {
            free_slot = &s_pool[i];
        }
    }

    if (free_slot != NULL) {
        free_slot->type     = (uint8_t)(CACHE_USED_BIT | type_value);
        free_slot->slave_id = slave_id;
        free_slot->address  = address;
        free_slot->value    = 0;
        free_slot->age_s    = 0;
    }

    return free_slot;  /* NULL when pool is full */
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
             * staleness check (age_s > timeout) can never fire and stale values
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

    /* s_pending is written here and in on_response from the sniffer task, but
     * ALSO reset (memset) by enable()/clear() running on the httpd/settings
     * task — so the single-writer assumption does not hold and these accesses
     * must be serialised under s_cache_mutex to avoid a torn read in
     * on_response on a dual-core SMP device (corr-5). */
    if (s_cache_mutex != NULL) xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    s_pending[port].valid     = true;
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

    if (function == 0x03 || function == 0x04) {
        /* Holding / input registers: 2 bytes per value, big-endian */
        uint8_t  type_value = (function == 0x03) ? CACHE_TYPE_HOLDING : CACHE_TYPE_INPUT;
        uint16_t max_regs   = byte_count / 2;

        if (max_regs < count) count = max_regs;

        /* Clamp so (start_addr + count) does not wrap past the 16-bit address
         * space. Without this a response read near 0xFFFF (e.g. start 0xFFFE,
         * count 4) would compute addr = 0xFFFE, 0xFFFF, 0x0000, 0x0001 and the
         * wrapped low addresses would poison unrelated registers of the same
         * slave (cache-lookup-1). Only the in-range portion is stored. */
        if ((uint32_t)start_addr + count > 0x10000u) {
            count = (uint16_t)(0x10000u - start_addr);
        }

        /* Bounds check: need at least 3 + 2*count bytes */
        if (((uint32_t)3 + (uint32_t)count * 2u) > (uint32_t)data_len) {
            ESP_LOGW(TAG, "Port %u slave %u FC%02X: response too short",
                     port, slave_id, function);
            return;
        }

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
                    s_entries_dropped += (uint32_t)(count - i);
                    ESP_LOGW(TAG, "Pool full, dropping %u entries", (unsigned)(count - i));
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

    } else if (function == 0x01 || function == 0x02) {
        /* Coils / discrete inputs: bit-packed, LSB first (bit 0 of data[3] = coil[start_addr]) */
        uint8_t type_value = (function == 0x01) ? CACHE_TYPE_COIL : CACHE_TYPE_DISCRETE;

        /* Clamp count to Modbus spec maximum (2000 coils per request) to prevent
         * uint overflow in byte arithmetic when a malformed response is received. */
        if (count > 2000) count = 2000;

        /* Use uint32_t for byte arithmetic to avoid uint8_t overflow. */
        uint32_t bytes_needed = (count + 7u) / 8u;

        /* Clamp count if the response carries fewer bytes than requested */
        if ((uint32_t)byte_count < bytes_needed) count = (uint16_t)((uint32_t)byte_count * 8u);

        /* Clamp so (start_addr + count) does not wrap past the 16-bit address
         * space — same rationale as the register branch (cache-lookup-1): a
         * coil read near 0xFFFF must not poison low coils via wrap-around. */
        if ((uint32_t)start_addr + count > 0x10000u) {
            count = (uint16_t)(0x10000u - start_addr);
        }

        /* Recalculate after clamping, then bounds check */
        bytes_needed = (count + 7u) / 8u;
        if ((3u + bytes_needed) > (uint32_t)data_len) {
            ESP_LOGW(TAG, "Port %u slave %u FC%02X: coil response too short",
                     port, slave_id, function);
            return;
        }

        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
        for (uint16_t i = 0; i < count; i++) {
            uint16_t addr  = start_addr + i;
            uint16_t value = (data[3 + i / 8] >> (i % 8)) & 1u;
            cache_entry_t *e = find_or_alloc_entry(slave_id, type_value, addr);
            if (e == NULL) {
                /* Pool full — see register branch (mem-exhaust-1). */
                if (s_pool != NULL) {
                    s_entries_dropped += (uint32_t)(count - i);
                    ESP_LOGW(TAG, "Pool full, dropping %u coil entries", (unsigned)(count - i));
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
    }
}

/* ---- Lookup API ---------------------------------------------------------- */

cache_lookup_result_t cache_multimaster_lookup(uint8_t slave_id, uint8_t function_code,
                                               uint16_t address, uint16_t *value_out,
                                               uint16_t value_timeout_s)
{
    if (s_cache_mutex == NULL || value_out == NULL) return CACHE_LOOKUP_NOT_FOUND;

    /* Map Modbus function code to cache type value (bits 1:0) */
    uint8_t type_value;
    switch (function_code) {
        case 0x01: type_value = CACHE_TYPE_COIL;      break;
        case 0x02: type_value = CACHE_TYPE_DISCRETE;  break;
        case 0x03: type_value = CACHE_TYPE_HOLDING;   break;
        case 0x04: type_value = CACHE_TYPE_INPUT;     break;
        default:   return CACHE_LOOKUP_NOT_FOUND;
    }

    cache_lookup_result_t result = CACHE_LOOKUP_NOT_FOUND;

    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    if (s_pool != NULL) {
        for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
            const cache_entry_t *e = &s_pool[i];
            if ((e->type & CACHE_USED_BIT) &&
                e->slave_id              == slave_id &&
                (e->type & 0x03u)        == type_value &&
                e->address               == address) {
                *value_out = e->value;
                result = CACHE_LOOKUP_FOUND;

                /* Age check: age_s is maintained by cache_age_task (saturating counter).
                 * value_timeout_s == 0 disables the check — always return FOUND.          */
                if (value_timeout_s > 0) {
                    if (e->age_s > value_timeout_s) {
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

    /* Single pass: count unique slave IDs. Reading the stats counters under the
     * same lock prevents torn 64-bit reads of s_last_packet_us / s_reset_us on
     * Xtensa (64-bit volatile is NOT atomically readable without a mutex). */
    uint8_t seen[32] = {0};
    if (s_pool != NULL) {
        for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
            if (!(s_pool[i].type & CACHE_USED_BIT)) continue;
            uint8_t sid = s_pool[i].slave_id;
            if (!(seen[sid >> 3] & (1u << (sid & 7)))) {
                seen[sid >> 3] |= (1u << (sid & 7));
                slaves++;
            }
        }
    }
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

        /* Single pass: count entries and unique slave IDs simultaneously.
         * Reading the stats counters under the same lock prevents torn
         * 64-bit reads of s_last_packet_us / s_reset_us on Xtensa
         * (64-bit volatile is NOT atomically readable without a mutex). */
        uint8_t seen[32] = {0};
        if (s_pool != NULL) {
            for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
                if (!(s_pool[i].type & CACHE_USED_BIT)) continue;
                entries++;
                uint8_t sid = s_pool[i].slave_id;
                if (!(seen[sid >> 3] & (1u << (sid & 7)))) {
                    seen[sid >> 3] |= (1u << (sid & 7));
                    slaves++;
                }
            }
        }
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
 * Uses chunked transfer: one stack-local 128-byte buffer per line so that
 * the full dataset (potentially ~240 KB) never has to be allocated at once.
 */
static esp_err_t cache_csv_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
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

    if (s_pool == NULL) {
        xSemaphoreGive(s_cache_mutex);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        const cache_entry_t *e = &s_pool[i];
        if (!(e->type & CACHE_USED_BIT)) continue;

        const char *type_str;
        switch (e->type & 0x03u) {
            case CACHE_TYPE_HOLDING:  type_str = "holding";  break;
            case CACHE_TYPE_INPUT:    type_str = "input";    break;
            case CACHE_TYPE_COIL:     type_str = "coil";     break;
            case CACHE_TYPE_DISCRETE: type_str = "discrete"; break;
            default:                  type_str = "unknown";  break;
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

        if (s_pool == NULL) {
            /* Pool was freed by disable() while we were sending — stop iteration */
            xSemaphoreGive(s_cache_mutex);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_OK;
        }
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

    if (s_pool == NULL) {
        xSemaphoreGive(s_cache_mutex);
        ret = httpd_resp_send_chunk(req, "]}", 2);
        if (ret != ESP_OK) return ret;
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        const cache_entry_t *e = &s_pool[i];
        if (!(e->type & CACHE_USED_BIT)) continue;

        /* Single-char type tag — shorter JSON, faster JS access */
        char type_ch;
        switch (e->type & 0x03u) {
            case CACHE_TYPE_HOLDING:  type_ch = 'h'; break;
            case CACHE_TYPE_INPUT:    type_ch = 'i'; break;
            case CACHE_TYPE_COIL:     type_ch = 'c'; break;
            case CACHE_TYPE_DISCRETE: type_ch = 'd'; break;
            default:                  type_ch = '?'; break;
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

        if (s_pool == NULL) {
            /* Pool was freed by disable() while we were sending — close data array and object */
            xSemaphoreGive(s_cache_mutex);
            httpd_resp_send_chunk(req, "]}", 2);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_OK;
        }
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
}

/* Returns the count of values dropped because the pool was full since the last
 * enable()/clear(). Used in unit tests only. */
uint32_t cache_multimaster_test_get_entries_dropped(void)
{
    return s_entries_dropped;
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
    uint8_t type_value;
    switch (function_code) {
        case 0x01: type_value = CACHE_TYPE_COIL;     break;
        case 0x02: type_value = CACHE_TYPE_DISCRETE; break;
        case 0x03: type_value = CACHE_TYPE_HOLDING;  break;
        case 0x04: type_value = CACHE_TYPE_INPUT;    break;
        default:   return false;
    }
    if (s_pool == NULL) return false;
    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        if ((s_pool[i].type & CACHE_USED_BIT) &&
            s_pool[i].slave_id == slave_id &&
            (s_pool[i].type & 0x03u) == type_value &&
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
