#include "mbtcp_reasm.h"

#include "esp_log.h"

#include <string.h>

size_t mbtcp_reasm_frame_total_len(const uint8_t *buf)
{
    uint16_t mbap_len = ((uint16_t)buf[4] << 8) | buf[5];        /* big-endian length field */
    return (size_t)mbap_len + offsetof(mb_tcp_header_t, unit_id); /* length counts from unit_id: +6 */
}

/* Find the slot held by sock, or claim a free one for it.
 * Returns NULL when every slot is taken. */
static mbtcp_reasm_slot_t *slot_get(mbtcp_reasm_t *r, int sock)
{
    mbtcp_reasm_slot_t *slot      = NULL;
    mbtcp_reasm_slot_t *free_slot = NULL;

    if (r->mutex) { xSemaphoreTake(r->mutex, portMAX_DELAY); }

    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        if (r->slots[i].sock == sock) { slot = &r->slots[i]; break; }
        if ((free_slot == NULL) && (r->slots[i].sock == -1)) { free_slot = &r->slots[i]; }
    }
    if ((slot == NULL) && (free_slot != NULL)) {
        free_slot->sock = sock;
        free_slot->len  = 0;
        slot = free_slot;
    }

    if (r->mutex) { xSemaphoreGive(r->mutex); }
    return slot;
}

bool mbtcp_reasm_init(mbtcp_reasm_t *r, const char *tag)
{
    if (r == NULL) return false;

    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        r->slots[i].sock = -1;
        r->slots[i].len  = 0;
    }
    r->slot_exhausted = 0;
    r->tag            = (tag != NULL) ? tag : "mbtcp_reasm";
    r->mutex          = xSemaphoreCreateMutex();

    return (r->mutex != NULL);
}

void mbtcp_reasm_deinit(mbtcp_reasm_t *r)
{
    if (r == NULL) return;
    if (r->mutex) {
        vSemaphoreDelete(r->mutex);
        r->mutex = NULL;
    }
}

void mbtcp_reasm_close(mbtcp_reasm_t *r, int sock)
{
    /* sock < 0 is the free-slot sentinel — it must never match a slot. */
    if (r == NULL || sock < 0) return;

    if (r->mutex) { xSemaphoreTake(r->mutex, portMAX_DELAY); }
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        if (r->slots[i].sock == sock) {
            r->slots[i].sock = -1;
            r->slots[i].len  = 0;   /* drop the partial frame: a reused fd must not inherit it */
            break;
        }
    }
    if (r->mutex) { xSemaphoreGive(r->mutex); }
}

size_t mbtcp_reasm_pending(mbtcp_reasm_t *r, int sock)
{
    if (r == NULL || sock < 0) return 0;   /* sock < 0 is the free-slot sentinel */

    size_t pending = 0;
    if (r->mutex) { xSemaphoreTake(r->mutex, portMAX_DELAY); }
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        if (r->slots[i].sock == sock) { pending = r->slots[i].len; break; }
    }
    if (r->mutex) { xSemaphoreGive(r->mutex); }
    return pending;
}

#ifdef __unittest_env__
bool mbtcp_reasm_has_slot(mbtcp_reasm_t *r, int sock)
{
    if (r == NULL || sock < 0) return false;   /* sock < 0 is the free-slot sentinel */

    bool found = false;
    if (r->mutex) { xSemaphoreTake(r->mutex, portMAX_DELAY); }
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        if (r->slots[i].sock == sock) { found = true; break; }
    }
    if (r->mutex) { xSemaphoreGive(r->mutex); }
    return found;
}
#endif /* __unittest_env__ */

uint32_t mbtcp_reasm_slot_exhausted(const mbtcp_reasm_t *r)
{
    return (r != NULL) ? r->slot_exhausted : 0u;
}

int mbtcp_reasm_feed(mbtcp_reasm_t *r, int sock, const uint8_t *data, size_t len,
                     mbtcp_reasm_frame_cb_t on_frame, void *user_ctx)
{
    if (r == NULL || data == NULL || on_frame == NULL) return MBTCP_REASM_NO_SLOT;

    /* -1 is the free-slot sentinel: a negative descriptor must never be allowed
     * to match (or claim) a slot. Not a slot-exhaustion condition. */
    if (sock < 0) {
        ESP_LOGW(r->tag, "invalid socket %d, not reassembling", sock);
        return MBTCP_REASM_NO_SLOT;
    }

    mbtcp_reasm_slot_t *c = slot_get(r, sock);
    if (c == NULL) {
        /* More than MBTCP_REASM_MAX_CONNS simultaneous connections. The caller
         * falls back to an unbuffered parse, which still handles a frame that
         * arrives whole in one recv() but cannot reassemble a split one. Counted
         * so the condition is observable instead of silent. */
        r->slot_exhausted++;
        ESP_LOGW(r->tag, "sock=%d: no reassembly slot (>%d conns), no stream reassembly",
                 sock, MBTCP_REASM_MAX_CONNS);
        return MBTCP_REASM_NO_SLOT;
    }

    int    accepted = 0;
    size_t off      = 0;

    while (off < len) {
        /* Append as much as fits. The buffer-full reset at the bottom of the loop
         * guarantees space > 0 here, so this always makes progress. */
        size_t space = MBTCP_REASM_FRAME_MAX - c->len;
        size_t chunk = len - off;
        if (chunk > space) { chunk = space; }
        memcpy(c->buf + c->len, data + off, chunk);
        c->len += chunk;
        off    += chunk;

        /* Dispatch every complete frame currently buffered.
         *
         * `resyncing` is deliberately local to this call: the scan always restarts
         * at pos = 0, so each feed() re-derives the desync state from the buffer
         * contents. No cross-call state is needed. */
        size_t pos       = 0;
        bool   resyncing = false;

        while ((c->len - pos) >= sizeof(mb_tcp_header_t)) {
            const uint8_t *h     = c->buf + pos;
            uint16_t       proto = ((uint16_t)h[2] << 8) | h[3];
            size_t         flen  = mbtcp_reasm_frame_total_len(h);

            bool header_ok = (proto == MODBUS_TCP_PROTOCOL_ID) &&
                             (flen >= sizeof(mb_tcp_header_t)) &&
                             (flen <= MBTCP_REASM_FRAME_MAX);

            if (!header_ok) {
                /* Bad header: resync a byte at a time. Dropping the whole buffer
                 * would also discard any valid frame coalesced behind this one in
                 * the same recv(). */
                pos      += 1;
                resyncing = true;
                continue;
            }

            if ((c->len - pos) < flen) {
                /* Plausible header, frame not fully buffered yet. */
                if (resyncing) {
                    /* Mid-resync this "header" is unverified — it may just be
                     * garbage that happens to look like one. Keep scanning rather
                     * than stalling the buffer on it. */
                    pos += 1;
                    continue;
                }
                break;  /* legitimate partial frame: carry it over to the next feed */
            }

            if (on_frame(user_ctx, sock, c->buf + pos, flen)) {
                accepted++;
            }
            pos      += flen;
            resyncing = false;   /* a complete frame re-establishes alignment */
        }

        /* Shift the unconsumed tail to the front. */
        if (pos > 0) {
            memmove(c->buf, c->buf + pos, c->len - pos);
            c->len -= pos;
        }

        /* Buffer full with no complete frame in it: the stream is desynced or the
         * ADU is oversized. Drop and resync. */
        if (c->len == MBTCP_REASM_FRAME_MAX) {
            ESP_LOGW(r->tag, "sock=%d: reassembly buffer full, resync (drop)", sock);
            c->len = 0;
        }
    }

    return accepted;
}
