/*
 * libev-compatible event loop for ESP32.
 * Uses select() for fd watching + manual timer tracking.
 */
#include "ev.h"

#include <string.h>
#include <sys/time.h>
#include <sys/select.h>
#include <errno.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "ev_loop";
static int s_loop_iter = 0;
#endif

struct ev_loop {
    ev_io    *io_list;
    ev_timer *timer_list;
    ev_async *async_list;
    ev_tstamp now_;
    int       break_;
};

static struct ev_loop s_default_loop;
static int s_loop_init;

ev_tstamp ev_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (ev_tstamp)tv.tv_sec + (ev_tstamp)tv.tv_usec * 1e-6;
}

struct ev_loop *ev_default_loop(unsigned int flags)
{
    (void)flags;
    if (!s_loop_init) {
        memset(&s_default_loop, 0, sizeof(s_default_loop));
        s_default_loop.now_ = ev_time();
        s_loop_init = 1;
    }
    return &s_default_loop;
}

ev_tstamp ev_now(struct ev_loop *loop) { return loop->now_; }

void ev_break(struct ev_loop *loop, int how)
{
    (void)how;
    loop->break_ = 1;
}

/* ---- IO ---- */
void ev_io_init(ev_io *w, void (*cb)(struct ev_loop *, ev_io *, int), int fd, int events)
{
    memset(w, 0, sizeof(*w));
    w->cb = cb; w->fd = fd; w->events = events;
}

void ev_io_set(ev_io *w, int fd, int events) { w->fd = fd; w->events = events; }

void ev_io_start(struct ev_loop *loop, ev_io *w)
{
    if (w->active) return;
    w->active = 1; w->loop_ = loop;
    w->next_ = loop->io_list; loop->io_list = w;
#ifdef ESP_PLATFORM
    if (w->fd > 10)
        ESP_LOGI(TAG, "io+ fd=%d ev=%d", w->fd, w->events);
#endif
}

void ev_io_stop(struct ev_loop *loop, ev_io *w)
{
    if (!w->active) return;
    w->active = 0;
    ev_io **pp = &loop->io_list;
    while (*pp) { if (*pp == w) { *pp = w->next_; break; } pp = &(*pp)->next_; }
    w->next_ = NULL;
}

/* ---- Timer ---- */
void ev_timer_init(ev_timer *w, void (*cb)(struct ev_loop *, ev_timer *, int), ev_tstamp after, ev_tstamp repeat)
{
    memset(w, 0, sizeof(*w));
    w->cb = cb; w->at = after; w->repeat = repeat;
}

void ev_timer_set(ev_timer *w, ev_tstamp after, ev_tstamp repeat) { w->at = after; w->repeat = repeat; }

void ev_timer_start(struct ev_loop *loop, ev_timer *w)
{
    if (w->active) ev_timer_stop(loop, w);
    w->active = 1; w->loop_ = loop;
    w->at += ev_time();
    w->next_ = loop->timer_list; loop->timer_list = w;
}

void ev_timer_stop(struct ev_loop *loop, ev_timer *w)
{
    if (!w->active) return;
    w->active = 0;
    ev_timer **pp = &loop->timer_list;
    while (*pp) { if (*pp == w) { *pp = w->next_; break; } pp = &(*pp)->next_; }
    w->next_ = NULL;
}

void ev_timer_again(struct ev_loop *loop, ev_timer *w)
{
    if (w->active) ev_timer_stop(loop, w);
    if (w->repeat > 0) { w->at = w->repeat; ev_timer_start(loop, w); }
}

/* ---- Async ---- */
void ev_async_init(ev_async *w, void (*cb)(struct ev_loop *, ev_async *, int))
{
    memset(w, 0, sizeof(*w)); w->cb = cb;
}

void ev_async_start(struct ev_loop *loop, ev_async *w)
{
    if (w->active) return;
    w->active = 1; w->loop_ = loop; w->sent = 0;
    w->next_ = loop->async_list; loop->async_list = w;
}

void ev_async_stop(struct ev_loop *loop, ev_async *w)
{
    if (!w->active) return;
    w->active = 0;
    ev_async **pp = &loop->async_list;
    while (*pp) { if (*pp == w) { *pp = w->next_; break; } pp = &(*pp)->next_; }
    w->next_ = NULL;
}

void ev_async_send(struct ev_loop *loop, ev_async *w)
{
    (void)loop;
    w->sent = 1;
}

/* ---- Main loop ---- */
int ev_run(struct ev_loop *loop, int flags)
{
    loop->break_ = 0;
#ifdef ESP_PLATFORM
    s_loop_iter = 0;
    ESP_LOGI(TAG, "ev_run starting (flags=%d)", flags);
#endif

    do {
        loop->now_ = ev_time();

        fd_set rfds, wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        int maxfd = -1;
        int nio = 0;

        for (ev_io *w = loop->io_list; w; w = w->next_) {
            if (w->events & EV_READ)  FD_SET(w->fd, &rfds);
            if (w->events & EV_WRITE) FD_SET(w->fd, &wfds);
            if (w->fd > maxfd) maxfd = w->fd;
            nio++;
        }

        struct timeval tv;
        struct timeval *tvp = NULL;

        ev_tstamp min_at = 1e18;
        int ntimers = 0;
        for (ev_timer *w = loop->timer_list; w; w = w->next_) {
            if (w->at < min_at) min_at = w->at;
            ntimers++;
        }

        if (min_at < 1e18) {
            ev_tstamp dt = min_at - loop->now_;
            if (dt < 0) dt = 0;
            if (dt > 3600) dt = 3600;
            tv.tv_sec = (long)dt;
            tv.tv_usec = (long)((dt - (ev_tstamp)tv.tv_sec) * 1e6);
            tvp = &tv;
        }

        int nasync = 0;
        for (ev_async *w = loop->async_list; w; w = w->next_) {
            nasync++;
            if (w->sent) { tv.tv_sec = 0; tv.tv_usec = 0; tvp = &tv; break; }
        }

        if (flags & EVRUN_NOWAIT) { tv.tv_sec = 0; tv.tv_usec = 0; tvp = &tv; }

        if (maxfd < 0 && !loop->timer_list && !loop->async_list) {
            tv.tv_sec = 0; tv.tv_usec = 50000; tvp = &tv;
        }

        /* Cap select timeout to 100ms so new connections are handled promptly.
         * Without this, select blocks up to the nearest timer (e.g. 10s keepalive)
         * and incoming TCP connections aren't accepted in time. */
        if (!tvp || tv.tv_sec > 0 || tv.tv_usec > 100000) {
            tv.tv_sec = 0; tv.tv_usec = 100000; tvp = &tv;
        }

#ifdef ESP_PLATFORM
        if (s_loop_iter == 0) {
            ESP_LOGI(TAG, "first select: maxfd=%d nio=%d", maxfd, nio);
        }
#endif

        int nready = select(maxfd + 1, &rfds, &wfds, NULL, tvp);

#ifdef ESP_PLATFORM
        static int last_nio = 0;
        if (nio != last_nio) {
            ESP_LOGI(TAG, "nio %d->%d maxfd=%d", last_nio, nio, maxfd);
            last_nio = nio;
        }
        s_loop_iter++;
#endif

        loop->now_ = ev_time();

        if (nready > 0) {
            ev_io *snap[32];
            int nsnap = 0;
            for (ev_io *w = loop->io_list; w && nsnap < 32; w = w->next_)
                snap[nsnap++] = w;

            for (int i = 0; i < nsnap; i++) {
                ev_io *w = snap[i];
                if (!w->active) continue;
                int revents = 0;
                if ((w->events & EV_READ)  && FD_ISSET(w->fd, &rfds))  revents |= EV_READ;
                if ((w->events & EV_WRITE) && FD_ISSET(w->fd, &wfds))  revents |= EV_WRITE;
                if (revents) {
#ifdef ESP_PLATFORM
                    if (w->fd > 10) /* skip UART fd=6 noise */
                        ESP_LOGI(TAG, "io_cb fd=%d ev=%d act=%d", w->fd, revents, w->active);
#endif
                    if (w->cb) w->cb(loop, w, revents);
                }
            }
        }

        /* Timers */
        {
            ev_timer *snap[32];
            int nsnap = 0;
            for (ev_timer *w = loop->timer_list; w && nsnap < 32; w = w->next_)
                snap[nsnap++] = w;
            for (int i = 0; i < nsnap; i++) {
                ev_timer *w = snap[i];
                if (!w->active) continue;
                if (w->at <= loop->now_) {
                    if (w->repeat > 0.) w->at = loop->now_ + w->repeat;
                    else ev_timer_stop(loop, w);
                    if (w->cb) w->cb(loop, w, EV_TIMER);
                }
            }
        }

        /* Async */
        {
            ev_async *snap[16];
            int nsnap = 0;
            for (ev_async *w = loop->async_list; w && nsnap < 16; w = w->next_)
                snap[nsnap++] = w;
            for (int i = 0; i < nsnap; i++) {
                ev_async *w = snap[i];
                if (!w->active) continue;
                if (w->sent) { w->sent = 0; if (w->cb) w->cb(loop, w, EV_ASYNC); }
            }
        }

    } while (!(flags & (EVRUN_NOWAIT | EVRUN_ONCE)) && !loop->break_);

#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "ev_run exited after %d iterations", s_loop_iter);
#endif
    return 0;
}
