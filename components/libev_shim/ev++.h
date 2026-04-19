/*
 * Minimal ev++ (libev C++ wrapper) compatible with knxd.
 * Drop-in replacement for <ev++.h> on ESP32.
 *
 * Implements ev::io, ev::timer, ev::async with the same template
 * callback API that knxd uses: w.set<Class, &Class::method>(this).
 */
#ifndef EVPP_H_
#define EVPP_H_

#include "ev.h"

namespace ev {

enum {
    UNDEF    = EV_UNDEF,
    NONE     = EV_NONE,
    READ     = EV_READ,
    WRITE    = EV_WRITE,
    TIMER    = EV_TIMER,
    ASYNC    = EV_ASYNC,
    ERROR_   = EV_ERROR,
};

typedef ev_tstamp tstamp;

inline ev_tstamp now() { return ev_time(); }

/* ============================================================
 * Base template for all watcher types.
 * Provides the set<K, &K::method>(obj) callback mechanism.
 * ============================================================ */
template<class ev_watcher, class watcher>
struct base {
    ev_watcher ew;

    /* Default ctor: zero-init */
    base() {
        ew.active = 0;
        ew.pending = 0;
        ew.priority = 0;
        ew.data = nullptr;
        ew.cb = nullptr;
        ew.loop_ = nullptr;
    }

    /* Type-safe callback binding:
     *   w.set<MyClass, &MyClass::handler>(this);
     * Stores `object` in data and a static thunk in cb. */
    template<class K, void (K::*method)(watcher &, int)>
    void set(K *object) throw () {
        ew.data = object;
        ew.cb = method_thunk<K, method>;
    }

    bool is_active() const { return ew.active; }

private:
    /* Static thunk that converts C callback to C++ method call */
    template<class K, void (K::*method)(watcher &, int)>
    static void method_thunk(struct ev_loop *loop, ev_watcher *w, int revents) {
        (void)loop;
        (static_cast<K *>(w->data)->*method)
            (*static_cast<watcher *>(reinterpret_cast<base *>(w)), revents);
    }

    base(const base &) = delete;
    base &operator=(const base &) = delete;
};

/* ============================================================
 * ev::io — file descriptor watcher
 * ============================================================ */
struct io : base<ev_io, io> {
    void set(int fd, int events) throw () {
        ev_io_set(&ew, fd, events);
    }

    void start() throw () {
        ev_io_start(ev_default_loop(0), &ew);
    }

    void start(int fd, int events) throw () {
        set(fd, events);
        start();
    }

    void stop() throw () {
        ev_io_stop(ev_default_loop(0), &ew);
    }

    /* Allow using set for both callback binding and fd/events.
     * The template version in base<> handles callback binding. */
    using base<ev_io, io>::set;
};

/* ============================================================
 * ev::timer
 * ============================================================ */
struct timer : base<ev_timer, timer> {
    void set(ev_tstamp after, ev_tstamp repeat = 0.) throw () {
        ev_timer_set(&ew, after, repeat);
    }

    void start() throw () {
        ev_timer_start(ev_default_loop(0), &ew);
    }

    void start(ev_tstamp after, ev_tstamp repeat = 0.) throw () {
        set(after, repeat);
        start();
    }

    void stop() throw () {
        ev_timer_stop(ev_default_loop(0), &ew);
    }

    void again() throw () {
        ev_timer_again(ev_default_loop(0), &ew);
    }

    using base<ev_timer, timer>::set;
};

/* ============================================================
 * ev::async
 * ============================================================ */
struct async : base<ev_async, async> {
    void start() throw () {
        ev_async_start(ev_default_loop(0), &ew);
    }

    void stop() throw () {
        ev_async_stop(ev_default_loop(0), &ew);
    }

    void send() throw () {
        ev_async_send(ev_default_loop(0), &ew);
    }

    using base<ev_async, async>::set;
};

/* ============================================================
 * ev::sig — stub (no signals on ESP32)
 * ============================================================ */
struct sig : base<ev_sig, sig> {
    void set(int signum) throw () { (void)signum; }
    void start() throw () {}
    void start(int signum) throw () { (void)signum; }
    void stop() throw () {}
    using base<ev_sig, sig>::set;
};

} /* namespace ev */

#endif /* EVPP_H_ */
