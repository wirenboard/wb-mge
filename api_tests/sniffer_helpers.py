"""
Shared helper functions for sniffer-related tests.

Used by both test_sniffer_ws.py and test_ports.py.
"""

import json
import operator
import threading
import time

import websocket
from urllib.parse import urlparse

from api_client import SNIFFER_STATUS_TIMEOUT_S


# Drop these helper frames from a failing test's traceback, but ONLY for the assertions the
# helpers raise on the caller's behalf: the finding is in the test, and three frames of
# plumbing between it and the message are noise. A genuine helper BUG is the opposite case —
# a JSONDecodeError on a truncated 200, an AttributeError if a body ever comes back as a
# list — and hiding those frames would point the traceback at the test and say nothing about
# where the helper broke. pytest accepts a callable here and calls it with the ExceptionInfo,
# so narrowing the hiding to AssertionError costs one expression.
_HIDE_OUR_ASSERTIONS = operator.methodcaller("errisinstance", AssertionError)

# The defect class the settle window in _poll_sniffer_status() exists to catch, in seconds:
# the reference defect it was validated against is a firmware patched to corrupt the sniffer
# state 300 ms AFTER publishing the bit the test polled for. Every bound below is derived
# from this number, so changing the window's sensitivity means changing THIS line — the old
# form derived its floor from _DelayedSession.DELAY_S, a client-side artificial delay that
# appears nowhere in the window's rationale, and would have lost the 300 ms defect the day
# that delay was dropped or the suite was pointed at a loopback device.
_REFERENCE_DEFECT_S = 0.3

# Length of the settle window, counted in measured status-read round trips rather than in
# seconds so it tracks the machine the suite happens to run on. Four of them: the poll's own
# budget takes ~2 round trips as the cost of surfacing a state change that is already in
# flight, and a LATE violation is a second change of exactly that kind — same single httpd
# thread, same endpoint, same read path — so it surfaces on the same scale. The window
# doubles that scale because it may: overshooting it costs only time.
_SETTLE_ROUND_TRIPS = 4

# Floor and ceiling on that round-trip-derived length, and a cap on the reads inside it.
# _SETTLE_ROUND_TRIPS x t_rtt alone is unbounded in both directions and is what the clamp
# fixes:
#
#   * FLOOR = 2 x _REFERENCE_DEFECT_S. The window must contain at least one read that STARTS
#     after the reference defect has fired, and reads are discrete, so the window has to be
#     longer than the defect's own delay. The loop below tests its exit condition on each
#     read's START time, so the last read it takes starts at >= window_s — or, when the read
#     cap ends the window first, at >= window_s x (1 - 1/_SETTLE_MAX_READS). The floor makes
#     both clear the defect outright, 0.600 s and 0.563 s against 0.300 s, with no case
#     analysis on t_rtt and no assumption about what a read costs.
#     Testing the exit on the time the read RETURNS is what the first version of this window
#     did, and it guarantees the wrong thing. Simulated against this exact loop: t_rtt
#     0.12 s, settle reads of 0.26 s and 0.49 s — the window closes at 0.75 s having started
#     its last read at 0.26 s, before the defect. t_rtt cannot rule that out, because it is
#     the MINIMUM over the poll's reads (see below): a later read may cost arbitrarily more,
#     which is exactly the guest-stall case this whole mechanism exists for.
#     The guarantee is in the CLIENT's clock, which is the only clock this test has. On a
#     guest that is not executing, the device's own 300 ms does not advance either, so such a
#     window observes less device time than wall time and the defect can fall past its
#     horizon — the same bounded-observation limit the docstring names below, not a second
#     one. Without the floor a fast link (~10 ms round trips over loopback, or the day
#     _DelayedSession's 100 ms goes away) shrinks the window to 40 ms and the reference
#     defect stops being caught even on a device that is running normally.
#   * CEILING. t_rtt is measured on reads taken while waiting for the very state change this
#     mechanism exists for, so a read that absorbed a multi-second guest pause inflates it;
#     unclamped, an 8 s sample would ask for a 32 s window, three times over in one test.
#     2.0 s is ~6.7x the reference defect — far past anything the window is meant to see.
#     What the ceiling bounds is the window's LENGTH, not the wall-clock cost of observing
#     it: the loop always takes at least two reads, so that same 8 s sample still costs ~16 s
#     of window per site, ~48 s across the three-site test. That cost, not the ceiling, is
#     what the per-test timeout markers in 13_test_ports.py have to budget for — the
#     derivation is there.
#   * MAX READS. The cap bounds the connect/close churn one observation puts on the device.
#     The client sends `Connection: close` (api_client.py), so every status read opens a
#     fresh TCP connection: over a ~10 ms loopback round trip an uncapped 2 s window would
#     open ~200 of them, ~600 across the three-site test, against 16 and 48 with the cap. It
#     is NOT about httpd's MAX_OPEN_SOCKETS — the reads are strictly sequential and each
#     connection is closed before the next one is opened, so at most one status connection is
#     open at a time whatever the cap says. It costs no sensitivity either, because the loop
#     PACES its reads at window_s / _SETTLE_MAX_READS instead of leaning on whatever delay
#     the client happens to have: the cap binds on the read scheduled at
#     window_s x (1 - 1/_SETTLE_MAX_READS), which is still past the reference defect.
#     No minimum-read constant belongs next to it. The window's start and each read's start
#     are two separate time.monotonic() calls, so the first read normally begins ~0 into the
#     window, 0 >= window_s is false for every window_s >= the floor, and the start-time exit
#     takes at least two reads. NORMALLY, not always: nothing pins the interpreter between
#     those two calls, and on a node loaded enough to pause this thread for longer than
#     window_s — the premise of this whole mechanism — the first read starts past the window
#     and is the only one. That costs no guarantee, which is why no constant is needed to
#     prevent it: what the window owes is one read that STARTS after the reference defect has
#     had time to fire, and a read displaced by such a pause starts even later than the
#     window's own length demands. A minimum-read constant would force extra reads in exactly
#     the case where the single read has already delivered the guarantee.
_SETTLE_FLOOR_S = 2 * _REFERENCE_DEFECT_S
_SETTLE_CEILING_S = 2.0
_SETTLE_MAX_READS = 16


def _ws_connect(api, port):
    """
    Connect a WebSocket to the sniffer endpoint, send the start command, and
    return the connection together with a stop-event for the ping thread.

    Returns:
        tuple: (ws, stop_event, ping_thread)
    """
    parsed = urlparse(api.base_url)
    host = parsed.hostname
    http_port = parsed.port or 80

    ws_url = f"ws://{host}:{http_port}/sniffer/ws"
    cookies = "; ".join([f"{k}={v}" for k, v in api.session.cookies.items()])

    # The HTTP->WS upgrade handshake can be slow under QEMU emulation on a loaded
    # host (the single emulated core runs below real-time, so a few seconds of
    # work stretches to tens of wall-clock seconds). Use a generous handshake
    # timeout and retry once on failure rather than letting a transient slow
    # handshake fail the test. (Kept well under the tests' per-test timeout.)
    ws = None
    last_exc = None
    for _attempt in range(2):
        ws = websocket.WebSocket()
        ws.settimeout(60)
        try:
            ws.connect(ws_url, cookie=cookies)
            break
        except Exception as e:  # noqa: BLE001 - retry any handshake failure
            last_exc = e
            try:
                ws.close()
            except Exception:
                pass
            time.sleep(2)
    else:
        raise TimeoutError(
            f"WebSocket handshake to {ws_url} failed after 2 attempts: {last_exc}"
        )

    # Close the socket ourselves if this send fails. The handshake has already succeeded by
    # now, so httpd is holding one of its MAX_OPEN_SOCKETS sessions for this connection.
    # Raising with the socket still open hands that session to a caller that cannot reach it —
    # `ws` is never returned — so the close is left to garbage collection at an unpredictable
    # moment, and pytest holding the failing frame alive for its traceback is exactly the case
    # where that moment is late.
    #
    # What does NOT leak is the sniffer client slot itself: the firmware policy is "one
    # client, the newest wins" (sniffer_ws_handler, main/bridge/sniffer.c), so the next
    # upgrade overwrites ws_client_fd and closes the fd it replaced. The cost of skipping
    # this close is one httpd session out of twelve, not a slot held against the next client.
    try:
        ws.send(json.dumps({"cmd": "start", "port": port}))
    except Exception:
        try:
            ws.close()
        except Exception:
            pass
        raise

    stop_event = threading.Event()

    def _ping():
        while not stop_event.is_set():
            try:
                ws.ping()
            except Exception:
                break
            time.sleep(0.5)

    ping_thread = threading.Thread(target=_ping, daemon=True)
    ping_thread.start()

    return ws, stop_event, ping_thread


def _read_sniffer_status(api, what, which_read):
    """One GET /sniffer/status, status-checked before the body is parsed.

    The status check is not decoration. auth_middleware_check() answers 401 with an EMPTY
    body (httpd_resp_send(req, NULL, 0), main/auth.c), so calling .json() on a non-200 raises
    requests.exceptions.JSONDecodeError from inside this helper — a parse error where the real
    finding is "the session was rejected". Any other non-200 carrying a body would be worse
    still: the caller's poll would spin its whole budget and then blame the wrong thing.

    `which_read` names this read inside the caller's sequence ("poll read 1", "settle re-read
    4"). It is what separates two genuinely different findings that otherwise print the same
    text: a 401 on the very FIRST read means the session was never valid, while a 401 partway
    through a settle window means a session that was working expired underneath the test.
    """
    __tracebackhide__ = _HIDE_OUR_ASSERTIONS
    resp = api.get_sniffer_status()
    assert resp.status_code == 200, (
        f"{what}: GET /sniffer/status ({which_read}) expected 200, "
        f"got {resp.status_code}: {resp.text!r}"
    )
    return resp.json()


def _assert_state(body, expected_state, what, when):
    """Assert every {key: value} pair of `expected_state` against `body`."""
    __tracebackhide__ = _HIDE_OUR_ASSERTIONS
    for state_key, state_value in expected_state.items():
        assert body.get(state_key) is state_value, (
            f"{what}: {state_key} must have been {state_value} {when}, got {body}"
        )


def _assert_sniffer_precondition(api, expected_state, what):
    """Assert, in ONE read, sniffer state a test relies on but does not itself establish.

    The invariants a test hands to _poll_sniffer_status() are only as good as their
    provenance, and for "a port that was never started here" the provenance is the session:
    the firmware leaves SNIFF_REASON_DISPLAY set when a WebSocket closes, so a port left up
    by an earlier test stays up. Reading it once BEFORE the step under test is what separates
    "an earlier test left the overlay on" from "starting this port raised the other one".

    Returns the body, for a caller that wants the rest of it.
    """
    __tracebackhide__ = _HIDE_OUR_ASSERTIONS
    body = _read_sniffer_status(api, what, "precondition read")
    _assert_state(body, expected_state, what, "before this test drove anything")
    return body


def _poll_sniffer_status(api, key, expected, what, invariants=None):
    """Wait for GET /sniffer/status to report body[key] is `expected`, then watch it settle.

    Returns the LAST body read, which is the final body of the settle window below — and in
    which body[key] is `expected` holds, because the window asserts exactly that on every
    body it reads. (No caller uses the return value today; the contract is stated so that the
    first one that does is not surprised by it.)

    `invariants` is an optional {key: value} mapping of state that the polled transition must
    not disturb ("starting port 2 must not clear port 1"). Unlike `key`, an invariant's value
    is established BEFORE the step being polled, so it is checked on EVERY body this function
    reads, poll phase included — see "Why invariants are checked during the poll too" below.

    Why a poll and not a sleep. A {"cmd":"start"|"stop","port":N} WebSocket frame is applied
    ASYNCHRONOUSLY with respect to ws.send(): the bytes only reach the firmware's SINGLE
    httpd thread, which flips the reason bit (sniffer_ws_handler -> sniffer_enable() /
    sniffer_disable(), main/bridge/sniffer.c) when its select() loop next reaches that
    session. The frame is not acknowledged — the handler sends nothing back — so
    /sniffer/status, which reports that very bitmask (sniff_ctx[N].reasons &
    SNIFF_REASON_DISPLAY, sniffer_status_handler), is the only observable there is, and
    re-reading it is the only honest way to wait.

    A fixed sleep does not fail because the httpd thread is slow to notice the frame: that
    thread sits in select(), so on a device that is executing at all the frame is handled
    within microseconds of becoming readable. A sleep fails when the guest does not execute
    AT ALL for the whole of it — a single-core emulator on a contended CI host, reproduced
    exactly by SIGSTOP on the QEMU process. The sleep then expires in host time without one
    guest instruction retiring, and the status read that follows it is answered from the
    pre-frame state, which is exactly build #19's `{'port_1': True, 'port_2': False}`.
    Re-reading covers that case because a read cannot be answered without the guest running.

    Budget = SNIFFER_STATUS_TIMEOUT_S + 2 x t_rtt_budget, each term derived rather than
    picked:

      * The cost of ONE status read is measured by this poll's own reads — on this machine,
        in this run, a moment before it is used. A constant cannot follow that: the shared CI
        node is materially slower than free hardware, and the only figure this suite has
        actually measured for it is suite WALL-CLOCK, ~124 min against ~35 min across builds
        #17/#18/#19 (see 36_test_tcp_server_deinit_hang.py). That ~3.5x is a suite-level
        ratio; no per-call ratio has been measured, and none is assumed here — which is the
        point of measuring the read cost instead of asserting a multiplier, and how the fixed
        0.5 s sleep this replaces came to fail.
        TWO statistics are kept over those same samples, because the budget and the settle
        window want opposite things from them:
          - t_rtt_budget, the MAXIMUM, feeds the budget here. What the budget is compared
            against, `elapsed`, only ever grows, so a budget that tracked the minimum could
            be cut below an `elapsed` that an earlier expensive read had already run up, and
            would then give up in precisely the stalled-guest regime this whole mechanism
            exists for. Replayed offline against this loop on a virtual clock: reads of
            11.0 s then 0.15 s with the target visible on the 3rd read fail at 2 reads on a
            minimum-driven budget, and 8.0 s then 0.15 s with the target on the 21st fail at
            17. Following the maximum makes the budget monotonically non-decreasing, so
            neither case fails — and no case is lost that a minimum-driven or first-read
            budget would have survived, because the maximum is >= both of them at every step.
          - t_rtt, the MINIMUM, sizes the settle window below and feeds nothing else. Every
            sample is taken while waiting for the very state change this mechanism exists
            for, so the scenario it was built for — the guest not executing, the read blocked
            until it resumes — inflates samples and can never deflate them: a read cannot be
            answered faster than the client, the network and the device can actually do the
            work. The minimum is therefore the least distorted estimate of what a read
            NORMALLY costs, which is the question the window asks; the budget asks the
            opposite one, how bad a read has been seen to get. In the common case the poll
            ends on its first read, both statistics ARE that read, and the window it feeds is
            clamped rather than trusted (see _SETTLE_FLOOR_S / _SETTLE_CEILING_S).
        The failure message prints both, labelled with which feeds what, and the budget it
        prints is the one the poll actually applied when it gave up. Because that budget
        never shrinks and is tested BEFORE each read, the `elapsed` it prints is always under
        the printed budget plus one round trip — an equation the reader can check instead of
        two numbers that do not reconcile.
      * 2 x t_rtt_budget guarantees the budget has room for the no-defect case on an
        arbitrarily slow machine: one status read that overtook the still-unread frame, plus
        the read after it that sees the new state — two reads priced at the worst one seen so
        far, which is the point of taking the maximum. Without the margin, a node where a
        single read costs more than the ceiling would give up before it had asked twice. That
        one read is the EMPIRICAL common case, not a bound: httpd_process_session() runs
        httpd_sess_process() at most once per session per select round (httpd_main.c), so N
        frames already queued ahead of the start frame need N rounds — and this module's own
        ping thread puts frames there, a PING every 0.5 s on the SAME socket (1-2 of them
        ahead of the start frame under the SIGSTOP repro). The ceiling, not this term, is
        what actually bounds the wait.
      * SNIFFER_STATUS_TIMEOUT_S is the ceiling, taken from the client timeout of the very
        call being polled: past it the device is not slow but unresponsive, and "the bit
        never flipped across N reads" is then the diagnosis to report. Note the budget sits
        ABOVE that client timeout, not under it. THIS IS THE CANONICAL STATEMENT OF WHY THAT
        IS SAFE (api_client.SNIFFER_STATUS_TIMEOUT_S points here rather than repeating it):
        the property that a genuine hang surfaces as a named requests.ReadTimeout does not
        rest on any relation between the two numbers — it holds CONSTRUCTIVELY, because the
        budget is checked BEFORE each read and never during one, so a read already started
        always plays out to its own ReadTimeout and raises it straight out of this helper.

    Why invariants are checked during the poll too. The poll reads whole bodies, and those
    bodies carry the invariant keys; discarding them would mean this helper fails not on the
    first violating body but on the first violating body AFTER the target bit appears. That
    is a real blind spot, not a theoretical one: a start handler that clears the neighbour's
    bit, restores it, and only then publishes its own puts the violation exclusively in
    poll-phase bodies, and every one of them would be read and thrown away.
    Checking them there does NOT break "slowness must never mean failure". The two kinds of
    key are not symmetric: the TARGET key is "wrong" by definition until the frame is
    applied, which is the whole reason for polling, whereas an invariant's value is
    established BEFORE the step being polled. Where it is established differs by call site,
    and only one of the two sources is this helper's own doing:

      * a port whose own poll and settle window already completed earlier in the same test —
        established here, one step ago;
      * a port that was never started in this test — established by nothing here at all. The
        firmware does not clear SNIFF_REASON_DISPLAY when a WebSocket closes: the eviction
        path closes the replaced session and touches no reason bits (sniffer_ws_handler,
        main/bridge/sniffer.c), and the bit goes down only on an explicit {"cmd":"stop"} or
        on a mode change through sniffer_detach(). "Port 2 is down" is therefore a property
        of the session, held by whatever ran before — so the call sites that rely on it read
        it once and assert it as a precondition of their own (_assert_sniffer_precondition()
        above, both users in 13_test_ports.py). Without that, a future test that raised the
        overlay on port 2 and closed its socket without stopping would leave the bit set for
        the rest of the session and this helper would report the residue as a firmware defect
        in starting port 1.

    There is therefore no legitimate reason to observe an invariant violated while waiting,
    and every body that shows one is a finding.

    The settle window. The poll and the settled-state check want opposite things from time,
    and conflating them is what the two-phase shape below avoids:

      * The poll waits for something to APPEAR. There, slowness must NEVER mean failure, so
        it is bounded by a deadline and ends on the earliest body in which the target bit is
        set.
      * The window is an OBSERVATION of an already-published state. There, slowness costs
        only time, so a fixed window is legitimate and a generous one is free.

    Ending at the poll-ending body would check everything at the earliest instant the target
    flipped — blind to a defect that publishes a bit first and corrupts the state a few
    hundred milliseconds later, which is precisely the per-port independence these tests
    exist to prove. So the window re-reads afterwards and asserts, on every body it reads,
    the invariants AND `key == expected` itself. The target key belongs in the window even
    though it would be vacuous on the poll-ending body: a start that publishes its own bit
    and then drops it 300 ms later is a defect nothing else here would see, and it is exactly
    what leaves a test green while the sniffer it just started has fallen off. That is also
    why the window runs UNCONDITIONALLY — the call sites that pass no invariants
    (13_test_ports.py's test_sniffer_status and test_sniffer_status_reflects_stop_command)
    still have a target key to watch, and used to get no window at all.

    A violation seen inside the window is a firmware defect by construction and cannot be a
    race against our own traffic: the target bit is already published, nothing else that
    mutates this state is in flight (the ping thread shares the socket but carries no
    command), and the port that the frame did not name has no reason to change at all.

    What the window does NOT cover, and nothing else here does either: a change that lands
    after it closes still escapes. The window is a bounded observation, not a proof, and no
    length would make it one. Past that horizon THERE IS NO TEST AT ALL, and that is worth
    saying outright rather than gesturing at "the unit tests" — a comment promising coverage
    that does not exist is the exact defect this docstring was rewritten to stop committing.
    Inside the horizon per-port isolation IS checked, by exactly one test: 13_test_ports.py's
    test_sniffer_status_both_ports_independent, the caller that passes these invariants.
    Offline it is not checked at all — and the reason is that the test was never written, not
    that it cannot be. sniffer.c does export a single test accessor, and it is
    sniffer_test_get_ws_client_fd(); but the reasons bitmask is observable without any
    accessor, through the RX gate it controls — sniffer_process() returns early while
    ctx->reasons == 0, so a port whose bits are clear enqueues nothing.
    unittests/sniffer/sniffer_process_test.c already drives exactly that machinery: per-port
    injection through s_desc0/s_desc1.sniff_handler (its SEND0/SEND1 macros), a setUp() that
    enables SNIFF_REASON_DISPLAY on both ports, and assert_queue_empty() as an established
    assertion (83 uses). "Enable port 0 only, feed a frame on port 1, assert the queue stays
    empty" is a per-port isolation test that needs no new accessor and no firmware change.
    Until someone writes it, the claim beyond this window's horizon rests on reading the code
    and on nothing else — both sniffer_enable() and sniffer_disable() take a port index and
    touch nothing but sniff_ctx[port_index] (main/bridge/sniffer.c). That is a real hole in
    coverage, named here so it can be filed rather than assumed away.

    The window has been observed firing, not just reasoned about. Against a firmware patched
    to clear port 1's DISPLAY bit 300 ms AFTER publishing port 2's,
    test_sniffer_status_both_ports_independent failed inside the window, while the same test
    asserting the invariant only on the poll-ending body passed and reported nothing. The two
    blind spots closed above were validated the same way, each against its own patched
    firmware: a start that clears its OWN bit 300 ms later (caught by the target key in the
    window, at an invariant-free site too), and a start that clears the neighbour's bit and
    restores it before publishing its own (caught by the poll-phase invariant check).
    """
    __tracebackhide__ = _HIDE_OUR_ASSERTIONS

    invariants = invariants or {}

    t_start = time.monotonic()
    body = _read_sniffer_status(api, what, "poll read 1")
    t_rtt = time.monotonic() - t_start
    # Two statistics over the same samples, because the budget and the settle window want
    # opposite things from them: the budget the WORST read seen, the window the least
    # distorted one. See the docstring. On the first read they are the same number.
    t_rtt_budget = t_rtt
    reads = 1
    budget_s = SNIFFER_STATUS_TIMEOUT_S + 2 * t_rtt_budget

    # A key this body does not carry would otherwise burn the whole budget and then blame the
    # firmware for the wrong thing ("port_3 never became True"), or report a port that does
    # not exist as having changed. One check against the first body replaces that with an
    # immediate, accurate statement of WHAT is missing — but not of WHY, and the message says
    # so rather than guessing. The two candidates are a typo in the test and a firmware that
    # stopped publishing the key, and nothing observable here separates them: this helper
    # sees one body and cannot know which key set the endpoint is supposed to have. Nor is
    # the second candidate excluded elsewhere — the only test of the key set,
    # test_sniffer_status_response_shape_and_content_type, reads /sniffer/status with both
    # overlays OFF, so a firmware that dropped a key only while an overlay is up would reach
    # this assertion with that test still green.
    missing = [k for k in [key, *invariants] if k not in body]
    assert not missing, (
        f"{what}: GET /sniffer/status has no {missing} in its body — either the test asked "
        f"for a key that does not exist, or the firmware stopped reporting one; this helper "
        f"cannot tell those apart, and the shape test covers the key set only with both "
        f"overlays off; body {body}"
    )

    _assert_state(body, invariants, what, f"on poll read 1, before {key}={expected} appeared")

    while body.get(key) is not expected:
        elapsed = time.monotonic() - t_start
        if elapsed >= budget_s:
            raise AssertionError(
                f"{what}: {key} never became {expected} — polled GET /sniffer/status for "
                f"{elapsed:.2f}s across {reads} reads (budget {budget_s:.2f}s = "
                f"{SNIFFER_STATUS_TIMEOUT_S}s client timeout + 2 x {t_rtt_budget:.3f}s, the "
                f"SLOWEST measured round trip, which is what the budget follows; the fastest "
                f"was {t_rtt:.3f}s and sizes the settle window instead); last body {body}"
            )
        t_read = time.monotonic()
        body = _read_sniffer_status(api, what, f"poll read {reads + 1}")
        t_read_s = time.monotonic() - t_read
        # The budget follows the MAXIMUM and so never shrinks, while `elapsed` above only
        # grows: a budget cut to fit a later cheap read can land under an `elapsed` that an
        # earlier expensive read already ran up, and give up in exactly the stalled-guest
        # regime this loop exists for. It also keeps the message's arithmetic checkable —
        # the budget is tested before each read and can only have grown since, so `elapsed`
        # is always under the printed budget plus one read.
        # The settle window gets the MINIMUM, which is a different question: not "how bad can
        # a read be here" but "what does a read normally cost".
        t_rtt = min(t_rtt, t_read_s)
        t_rtt_budget = max(t_rtt_budget, t_read_s)
        budget_s = SNIFFER_STATUS_TIMEOUT_S + 2 * t_rtt_budget
        reads += 1
        _assert_state(body, invariants, what,
                      f"on poll read {reads}, while waiting for {key}={expected}")

    # Everything asserted from here on is asserted on an already-published state, so the
    # target key rides along with the invariants instead of being trusted once and dropped.
    settled_state = {**invariants, key: expected}
    _assert_state(body, settled_state, what,
                  f"at the instant {key}={expected} was published")

    window_s = min(max(_SETTLE_ROUND_TRIPS * t_rtt, _SETTLE_FLOOR_S), _SETTLE_CEILING_S)
    # Pace the re-reads across the window instead of leaning on whatever per-request delay
    # the client happens to have (_DelayedSession's 100 ms today, which is no part of this
    # mechanism's derivation). When reads are fast the pacing is what spreads them over the
    # window and the cap ends it one interval early; when they are slow each read simply
    # follows the one before it and the pacing never binds.
    interval_s = window_s / _SETTLE_MAX_READS
    window_start = time.monotonic()
    when = (f"through the {window_s:.2f}s settle window after {key}={expected} was published "
            f"(sized from t_rtt {t_rtt:.3f}s, the FASTEST of the {reads} poll reads — the "
            f"budget above follows the slowest instead)")
    settle_reads = 0
    while True:
        next_read_at = window_start + settle_reads * interval_s
        pause_s = next_read_at - time.monotonic()
        if pause_s > 0:
            time.sleep(pause_s)
        # Exit on when this read STARTED, never on when it returned. What the window has to
        # deliver is one read that begins after the defect's delay has run out; a read that
        # returns late says nothing about when it began, and t_rtt cannot bound it either
        # (it is a minimum, so any later read may cost more). See _SETTLE_FLOOR_S.
        read_started_at = time.monotonic()
        body = _read_sniffer_status(api, what, f"settle re-read {settle_reads + 1}")
        settle_reads += 1
        _assert_state(body, settled_state, what, f"{when}, re-read {settle_reads}")
        if settle_reads >= _SETTLE_MAX_READS:
            return body
        if read_started_at - window_start >= window_s:
            return body


def _collect_packets(ws, min_count, timeout_sec, filter_fn=None):
    """
    Collect JSON packets from a WebSocket until min_count is reached or the
    deadline expires.

    Args:
        ws: Connected websocket.WebSocket instance.
        min_count: Stop early once this many packets have been collected.
        timeout_sec: Total wall-clock seconds to wait.
        filter_fn: Optional callable(packet) -> bool; only matching packets are
                   collected and count toward min_count.

    Returns:
        list of parsed packet dicts (filtered if filter_fn is given).
    """
    packets = []
    deadline = time.monotonic() + timeout_sec
    ws.settimeout(5)

    while time.monotonic() < deadline:
        if len(packets) >= min_count:
            break
        try:
            msg = ws.recv()
            if not msg:
                continue
            pkt = json.loads(msg)
            if filter_fn is None or filter_fn(pkt):
                packets.append(pkt)
        except websocket.WebSocketTimeoutException:
            pass
        except websocket.WebSocketPayloadException:
            pass
        except websocket.WebSocketProtocolException:
            pass
        except json.JSONDecodeError:
            pass

    return packets
