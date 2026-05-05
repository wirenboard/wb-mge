#include "cache_multimaster.h"
#include "sniffer.h"
#include "bridge.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "cache_mm";

/* ---- Data model --------------------------------------------------------- */

/* Flat pool: 1024 entries × 16 bytes ≈ 16 KB — fits comfortably in ESP32 DRAM. */
#define CACHE_MAX_ENTRIES  1024

/** Data type tag for a cache entry */
typedef enum {
    CACHE_TYPE_HOLDING  = 0,  /* FC03 - holding registers */
    CACHE_TYPE_INPUT    = 1,  /* FC04 - input registers   */
    CACHE_TYPE_COIL     = 2,  /* FC01 - output coils      */
    CACHE_TYPE_DISCRETE = 3,  /* FC02 - discrete inputs   */
} cache_type_t;

/** One flat pool entry representing a single register or coil value */
typedef struct {
    bool         used;          /* Entry is populated                        */
    uint8_t      port;          /* 0-based RS-485 port index                 */
    uint8_t      slave_id;      /* Modbus slave address                      */
    cache_type_t type;          /* Data type (holding/input/coil/discrete)   */
    uint16_t     address;       /* Register or coil address (0-based)        */
    uint16_t     value;         /* Last known value (0 or 1 for coils)       */
    uint64_t     timestamp_us;  /* esp_timer_get_time() at capture           */
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

static cache_entry_t     s_pool[CACHE_MAX_ENTRIES];
static volatile bool     s_cache_enabled = false;
static SemaphoreHandle_t s_cache_mutex   = NULL;
static pending_req_t     s_pending[BRIDGES_COUNT];

/* ---- Internal helpers ---------------------------------------------------- */

/**
 * @brief Find an existing pool entry matching (port, slave_id, type, address),
 *        or allocate the first unused slot for a new entry.
 *
 * Performs a single linear scan: if an exact match is found it is returned
 * immediately; otherwise the first unused slot is initialised and returned.
 * Returns NULL when the pool is full.
 *
 * Caller must hold s_cache_mutex.
 */
static cache_entry_t *find_or_alloc_entry(uint8_t port, uint8_t slave_id,
                                           cache_type_t type, uint16_t address)
{
    cache_entry_t *free_slot = NULL;

    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        if (s_pool[i].used &&
            s_pool[i].port     == port     &&
            s_pool[i].slave_id == slave_id &&
            s_pool[i].type     == type     &&
            s_pool[i].address  == address) {
            return &s_pool[i];  /* existing entry found */
        }
        if (!s_pool[i].used && free_slot == NULL) {
            free_slot = &s_pool[i];
        }
    }

    if (free_slot != NULL) {
        free_slot->used         = true;
        free_slot->port         = port;
        free_slot->slave_id     = slave_id;
        free_slot->type         = type;
        free_slot->address      = address;
        free_slot->value        = 0;
        free_slot->timestamp_us = 0;
    }

    return free_slot;  /* NULL when pool is full */
}

/**
 * @brief Count the total number of used entries in the flat pool.
 *
 * Caller must hold s_cache_mutex.
 */
static int count_entries_locked(void)
{
    int n = 0;
    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        if (s_pool[i].used) n++;
    }
    return n;
}

/* ---- Public API ---------------------------------------------------------- */

esp_err_t cache_multimaster_init(void)
{
    s_cache_mutex = xSemaphoreCreateMutex();
    if (s_cache_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create cache mutex");
        return ESP_ERR_NO_MEM;
    }
    memset(s_pool,    0, sizeof(s_pool));
    memset(s_pending, 0, sizeof(s_pending));
    s_cache_enabled = false;
    ESP_LOGI(TAG, "Cache multimaster initialized");
    return ESP_OK;
}

void cache_multimaster_enable(void)
{
    s_cache_enabled = true;
    sniffer_set_cache_active(true);
    ESP_LOGI(TAG, "Cache multimaster enabled");
}

void cache_multimaster_disable(void)
{
    s_cache_enabled = false;
    sniffer_set_cache_active(false);
    cache_multimaster_clear();
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
    memset(s_pool, 0, sizeof(s_pool));
    xSemaphoreGive(s_cache_mutex);
    /* s_pending is only written from sniffer_ws_task; clear outside the mutex */
    memset(s_pending, 0, sizeof(s_pending));
    ESP_LOGI(TAG, "Cache cleared");
}

void cache_multimaster_on_request(uint8_t port, uint8_t slave_id, uint8_t function,
                                  uint16_t start_reg, uint16_t count)
{
    if (port >= BRIDGES_COUNT) return;

    /* No mutex needed: s_pending is only written here and read in on_response,
     * both called from the same sniffer_ws_task context.                       */
    s_pending[port].valid     = true;
    s_pending[port].slave_id  = slave_id;
    s_pending[port].function  = function;
    s_pending[port].start_reg = start_reg;
    s_pending[port].count     = count;
}

void cache_multimaster_on_response(uint8_t port, uint8_t slave_id, uint8_t function,
                                   const uint8_t *data, uint16_t data_len,
                                   uint64_t timestamp_us)
{
    if (port >= BRIDGES_COUNT) return;
    if (data == NULL || data_len < 4) return;

    /* Verify there is a matching pending request */
    if (!s_pending[port].valid ||
        s_pending[port].slave_id != slave_id ||
        s_pending[port].function != function) {
        s_pending[port].valid = false;
        return;
    }

    uint16_t start_addr = s_pending[port].start_reg;
    uint16_t count      = s_pending[port].count;
    s_pending[port].valid = false; /* consume the pending request */

    /* data layout: [0]=slave_id [1]=FC [2]=byte_count [3..N]=data */
    uint8_t byte_count = data[2];

    if (s_cache_mutex == NULL) return;

    if (function == 0x03 || function == 0x04) {
        /* Holding / input registers: 2 bytes per value, big-endian */
        cache_type_t type     = (function == 0x03) ? CACHE_TYPE_HOLDING : CACHE_TYPE_INPUT;
        uint16_t     max_regs = byte_count / 2;

        if (max_regs < count) count = max_regs;

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
            cache_entry_t *e = find_or_alloc_entry(port, slave_id, type, addr);
            if (e == NULL) {
                ESP_LOGW(TAG, "Pool full, dropping entry");
                break;
            }
            e->value        = value;
            e->timestamp_us = timestamp_us;
        }
        xSemaphoreGive(s_cache_mutex);

    } else if (function == 0x01 || function == 0x02) {
        /* Coils / discrete inputs: bit-packed, LSB first (bit 0 of data[3] = coil[start_addr]) */
        cache_type_t type = (function == 0x01) ? CACHE_TYPE_COIL : CACHE_TYPE_DISCRETE;

        /* Clamp count to Modbus spec maximum (2000 coils per request) to prevent
         * uint overflow in byte arithmetic when a malformed response is received. */
        if (count > 2000) count = 2000;

        /* Use uint32_t for byte arithmetic to avoid uint8_t overflow. */
        uint32_t bytes_needed = (count + 7u) / 8u;

        /* Clamp count if the response carries fewer bytes than requested */
        if ((uint32_t)byte_count < bytes_needed) count = (uint16_t)((uint32_t)byte_count * 8u);

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
            cache_entry_t *e = find_or_alloc_entry(port, slave_id, type, addr);
            if (e == NULL) {
                ESP_LOGW(TAG, "Pool full, dropping coil entry");
                break;
            }
            e->value        = value;
            e->timestamp_us = timestamp_us;
        }
        xSemaphoreGive(s_cache_mutex);
    }
}

/* ---- Lookup API ---------------------------------------------------------- */

bool cache_multimaster_lookup(uint8_t slave_id, uint8_t function_code,
                               uint16_t address, uint16_t *value_out)
{
    if (s_cache_mutex == NULL || value_out == NULL) return false;

    /* Map Modbus function code to cache type */
    cache_type_t type;
    switch (function_code) {
        case 0x01: type = CACHE_TYPE_COIL;      break;
        case 0x02: type = CACHE_TYPE_DISCRETE;  break;
        case 0x03: type = CACHE_TYPE_HOLDING;   break;
        case 0x04: type = CACHE_TYPE_INPUT;     break;
        default:   return false;
    }

    bool found = false;

    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        const cache_entry_t *e = &s_pool[i];
        if (e->used &&
            e->slave_id == slave_id &&
            e->type     == type     &&
            e->address  == address) {
            *value_out = e->value;
            found = true;
            break;
        }
    }
    xSemaphoreGive(s_cache_mutex);

    return found;
}

/* ---- HTTP handlers ------------------------------------------------------- */

/**
 * POST /cache/enable — enable caching
 */
static esp_err_t cache_enable_handler(httpd_req_t *req)
{
    cache_multimaster_enable();
    const char *resp = "{\"enabled\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, (ssize_t)strlen(resp));
    return ESP_OK;
}

/**
 * POST /cache/disable — disable and clear caching
 */
static esp_err_t cache_disable_handler(httpd_req_t *req)
{
    cache_multimaster_disable();
    const char *resp = "{\"enabled\":false}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, (ssize_t)strlen(resp));
    return ESP_OK;
}

/**
 * GET /cache/status — return enabled flag and total entry count
 */
static esp_err_t cache_status_handler(httpd_req_t *req)
{
    int entries = 0;
    if (s_cache_mutex != NULL) {
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
        entries = count_entries_locked();
        xSemaphoreGive(s_cache_mutex);
    }

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"enabled\":%s,\"entries\":%d}",
             s_cache_enabled ? "true" : "false", entries);
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
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"modbus_cache.csv\"");

    /* Send CSV header line — includes a type column for holding/input/coil/discrete */
    const char *header = "port,slave_id,type,address,value,timestamp_us\r\n";
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

    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        const cache_entry_t *e = &s_pool[i];
        if (!e->used) continue;

        const char *type_str;
        switch (e->type) {
            case CACHE_TYPE_HOLDING:  type_str = "holding";  break;
            case CACHE_TYPE_INPUT:    type_str = "input";    break;
            case CACHE_TYPE_COIL:     type_str = "coil";     break;
            case CACHE_TYPE_DISCRETE: type_str = "discrete"; break;
            default:                  type_str = "unknown";  break;
        }

        /* Format one CSV row into a stack-local 128-byte buffer */
        char line[128];
        int len = snprintf(line, sizeof(line),
                           "%u,%u,%s,%u,%u,%" PRIu64 "\r\n",
                           (unsigned)(e->port + 1),   /* 1-based port */
                           (unsigned)e->slave_id,
                           type_str,
                           (unsigned)e->address,
                           (unsigned)e->value,
                           e->timestamp_us);

        if (len < 0 || len >= (int)sizeof(line)) continue;

        /* Release the mutex while sending to avoid holding it during I/O */
        xSemaphoreGive(s_cache_mutex);
        ret = httpd_resp_send_chunk(req, line, (ssize_t)len);
        xSemaphoreTake(s_cache_mutex, portMAX_DELAY);

        if (ret != ESP_OK) {
            xSemaphoreGive(s_cache_mutex);
            return ret;
        }
    }

    xSemaphoreGive(s_cache_mutex);

    /* Terminate chunked transfer */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/**
 * GET /cache/json — stream all cached register values as a JSON array.
 *
 * Designed for frequent UI polling: no heap allocation, chunked transfer,
 * mutex released between chunks to avoid stalling writers.
 *
 * Output format (compact, one object per entry):
 *   [{"p":1,"s":3,"t":"h","a":100,"v":1234,"ts":1234567890},...]
 *
 * Field abbreviations (kept short for low-overhead JS parsing):
 *   p  – port (1-based)
 *   s  – slave_id
 *   t  – type: "h"=holding, "i"=input, "c"=coil, "d"=discrete
 *   a  – address (0-based)
 *   v  – value
 *   ts – timestamp_us
 */
static esp_err_t cache_json_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    if (s_cache_mutex == NULL) {
        const char *empty = "[]";
        httpd_resp_send(req, empty, (ssize_t)strlen(empty));
        return ESP_OK;
    }

    /* Opening bracket */
    esp_err_t ret = httpd_resp_send_chunk(req, "[", 1);
    if (ret != ESP_OK) return ret;

    bool first = true;

    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);

    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        const cache_entry_t *e = &s_pool[i];
        if (!e->used) continue;

        /* Single-char type tag — shorter JSON, faster JS access */
        char type_ch;
        switch (e->type) {
            case CACHE_TYPE_HOLDING:  type_ch = 'h'; break;
            case CACHE_TYPE_INPUT:    type_ch = 'i'; break;
            case CACHE_TYPE_COIL:     type_ch = 'c'; break;
            case CACHE_TYPE_DISCRETE: type_ch = 'd'; break;
            default:                  type_ch = '?'; break;
        }

        /* Stack-local buffer: max entry ~80 chars, prefix comma = 81 */
        char buf[96];
        int len = snprintf(buf, sizeof(buf),
                           "%s{\"p\":%u,\"s\":%u,\"t\":\"%c\",\"a\":%u,\"v\":%u,\"ts\":%" PRIu64 "}",
                           first ? "" : ",",
                           (unsigned)(e->port + 1),
                           (unsigned)e->slave_id,
                           type_ch,
                           (unsigned)e->address,
                           (unsigned)e->value,
                           e->timestamp_us);

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
            xSemaphoreGive(s_cache_mutex);
            return ret;
        }
    }

    xSemaphoreGive(s_cache_mutex);

    /* Closing bracket + terminate chunked transfer */
    ret = httpd_resp_send_chunk(req, "]", 1);
    if (ret != ESP_OK) return ret;
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* ---- URI descriptor tables ---------------------------------------------- */

static const httpd_uri_t cache_enable_uri = {
    .uri     = "/cache/enable",
    .method  = HTTP_POST,
    .handler = cache_enable_handler,
};

static const httpd_uri_t cache_disable_uri = {
    .uri     = "/cache/disable",
    .method  = HTTP_POST,
    .handler = cache_disable_handler,
};

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

    ret = httpd_register_uri_handler(server, &cache_enable_uri);
    if (ret != ESP_OK) return ret;

    ret = httpd_register_uri_handler(server, &cache_disable_uri);
    if (ret != ESP_OK) return ret;

    ret = httpd_register_uri_handler(server, &cache_status_uri);
    if (ret != ESP_OK) return ret;

    ret = httpd_register_uri_handler(server, &cache_csv_uri);
    if (ret != ESP_OK) return ret;

    return httpd_register_uri_handler(server, &cache_json_uri);
}
