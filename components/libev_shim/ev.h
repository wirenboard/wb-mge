/*
 * Minimal libev-compatible API for ESP32.
 * Implements ev_io, ev_timer, ev_async using select() + manual timers.
 * Only the subset used by knxd is implemented.
 */
#ifndef EV_H_
#define EV_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef double ev_tstamp;

/* Event flags */
#define EV_UNDEF    ((int)0xFFFFFFFF)
#define EV_NONE     0x00
#define EV_READ     0x01
#define EV_WRITE    0x02
#define EV_TIMER    0x00000100
#define EV_ASYNC    0x00000200
#define EV_ERROR    0x80000000

/* Loop flags */
#define EVFLAG_AUTO      0x00000000U
#define EVFLAG_NOSIGMASK 0x00000000U
#define EVFLAG_SELECT    0x00000000U
#define EVRUN_NOWAIT     1
#define EVRUN_ONCE       2
#define EVBREAK_ONE      1
#define EVBREAK_ALL      2

/* Forward declare */
struct ev_loop;

/* libev macros for passing loop parameter */
#define EV_A_ ev_default_loop(0),
#define EV_A  ev_default_loop(0)
#define EV_P_ struct ev_loop *loop,
#define EV_P  struct ev_loop *loop
#define EV_DEFAULT ev_default_loop(0)
#define EV_DEFAULT_
#define EVBACKEND_SELECT 1

/* Watcher base fields (must be first in every watcher struct) */
#define EV_WATCHER_BASE \
    int active;         \
    int pending;        \
    int priority;       \
    void *data;         \
    struct ev_loop *loop_

/* ---- ev_io ---- */
typedef struct ev_io {
    EV_WATCHER_BASE;
    void (*cb)(struct ev_loop *, struct ev_io *, int);
    int fd;
    int events;
    struct ev_io *next_;
} ev_io;

/* ---- ev_timer ---- */
typedef struct ev_timer {
    EV_WATCHER_BASE;
    void (*cb)(struct ev_loop *, struct ev_timer *, int);
    ev_tstamp at;
    ev_tstamp repeat;
    struct ev_timer *next_;
} ev_timer;

/* ---- ev_async ---- */
typedef struct ev_async {
    EV_WATCHER_BASE;
    void (*cb)(struct ev_loop *, struct ev_async *, int);
    volatile int sent;
    struct ev_async *next_;
} ev_async;

/* ---- ev_sig (stub — not needed on ESP32) ---- */
typedef struct ev_sig {
    EV_WATCHER_BASE;
    void (*cb)(struct ev_loop *, struct ev_sig *, int);
    int signum;
} ev_sig;

/* ---- Loop API ---- */
struct ev_loop *ev_default_loop(unsigned int flags);
int             ev_run(struct ev_loop *loop, int flags);
void            ev_break(struct ev_loop *loop, int how);
ev_tstamp       ev_now(struct ev_loop *loop);
ev_tstamp       ev_time(void);

/* ---- io ---- */
void ev_io_init(ev_io *w, void (*cb)(struct ev_loop *, ev_io *, int), int fd, int events);
void ev_io_start(struct ev_loop *loop, ev_io *w);
void ev_io_stop(struct ev_loop *loop, ev_io *w);
void ev_io_set(ev_io *w, int fd, int events);

/* ---- timer ---- */
void ev_timer_init(ev_timer *w, void (*cb)(struct ev_loop *, ev_timer *, int), ev_tstamp after, ev_tstamp repeat);
void ev_timer_start(struct ev_loop *loop, ev_timer *w);
void ev_timer_stop(struct ev_loop *loop, ev_timer *w);
void ev_timer_set(ev_timer *w, ev_tstamp after, ev_tstamp repeat);
void ev_timer_again(struct ev_loop *loop, ev_timer *w);

/* ---- async ---- */
void ev_async_init(ev_async *w, void (*cb)(struct ev_loop *, ev_async *, int));
void ev_async_start(struct ev_loop *loop, ev_async *w);
void ev_async_stop(struct ev_loop *loop, ev_async *w);
void ev_async_send(struct ev_loop *loop, ev_async *w);

/* ---- sig (stubs) ---- */
static inline void ev_signal_init(ev_sig *w, void (*cb)(struct ev_loop *, ev_sig *, int), int sig) {
    (void)w; (void)cb; (void)sig;
}
static inline void ev_signal_start(struct ev_loop *loop, ev_sig *w) { (void)loop; (void)w; }
static inline void ev_signal_stop(struct ev_loop *loop, ev_sig *w) { (void)loop; (void)w; }

#ifdef __cplusplus
}
#endif

#endif /* EV_H_ */
