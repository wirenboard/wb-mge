"""
Regression test for cache_modbus_server_deinit() hang when a TCP client is
actively polling (continuously sending Modbus requests).

Bug: In main/bridge/tcp_server.c, receiver_task() only checks the
EVENT_TASK_EXIT_REQ flag in the EAGAIN/EWOULDBLOCK branch (when the 100 ms
SO_RCVTIMEO fires with no data). If an external device continuously sends
data, recv() never times out and the exit flag is never checked.
cache_modbus_server_deinit() (triggered by disabling "Serve cached values via
TCP") calls tcp_server_deinit(), which then never leaves
`while (desc->active_connections > 0)` (main/bridge/tcp_server.c:951).

Analogous to 36_test_tcp_server_deinit_hang.py (idle connection), but covers
the *active polling* case: the client sends FC03 requests continuously, which
is the exact scenario that triggers the hang.

WHY THE CLIENT'S SEND CADENCE IS PART OF THE DETECTOR, not an implementation detail.
run_receiver() checks the exit flag in TWO places: after data
(main/bridge/tcp_server.c:426-429 — the check the guarded regression deletes) and in the
EAGAIN branch produced by the 100 ms SO_RCVTIMEO set at :227-228 (:388-396 — untouched by
the regression). So a DEFECTIVE receiver still escapes, via EAGAIN, as soon as its recv()
finds NOTHING BUFFERED for a full 100 ms: it breaks out, active_connections drops to
zero, tcp_server_deinit() returns, the step 11 probe is answered 200 and — WITHOUT THE STEP 13
GUARDS — the test PASSES ON DEFECTIVE FIRMWARE. With them such a run is red, but red as
"THIS RUN PROVES NOTHING", which is the honest verdict: the guards make the disarmed run
visible, they do not make the firmware healthy. The hang therefore only reproduces while the
guest's receive queue is never empty for 100 ms on end, for the whole time the probe is
outstanding.

"NEVER EMPTY FOR 100 MS" IS NOT "FED EVERY 100 MS", and the difference is what step 13's
guard A is scoped by. recv() drains a queue, so a guest that is behind — still holding
requests it has not answered — cannot time out however long this process pauses; only a guest
that has caught up is exposed to a pause in sending. On a slow enough guest the client is
never caught up and the send cadence stops mattering entirely.

That is why the polling client sends on a HOST-DRIVEN cadence (_SEND_INTERVAL_S) with a
separate drainer thread, instead of the obvious lockstep send-then-recv loop. In lockstep the
inter-send gap EQUALS the guest's response latency, so an emulated ESP32 on a loaded node
drifts past 100 ms per round trip and silently disarms the detector — the exact regime this
test exists for. Decoupled, the SUBMISSION interval is a host sleep timer and does not depend
on the guest at all.

Sending on time is not the same as the guest RECEIVING on time, though, and step 13 therefore
does not take the sender's word for it. Which exit the receiver actually took is legible from
the response stream: the post-data check breaks out immediately after answering a request,
while the EAGAIN branch breaks out only after a full timeout with nothing to answer. So step
13's decisive measurement is the interval between the LAST RESPONSE and the teardown, and a
run whose receiver left through EAGAIN is failed as inconclusive whatever the firmware. That
distinction is not theoretical: with the regression reintroduced and a 300 ms vTaskDelay at the
head of the cache server's process_data_from_tcp(), this item measured a flat 20 ms send
cadence over 1621 sends in 32.4 s (31 ms worst gap, no throttled ticks) while the guest's
emulated NIC dropped frames underneath it, starved, and took the EAGAIN exit 103 ms after its
last response — a green run on defective firmware that a send-cadence check alone did not see.
(That delay costs 300 ms per recv() BATCH, not per request — process_data_from_tcp() is handed
a whole recv() buffer and mbtcp_reasm_feed() then answers every frame in it — so the guest
still returned 1170 responses inside that window and the backlog never reached
_MAX_OUTSTANDING. See the fuller account at guard A in step 13.)

HOW THE HANG IS OBSERVED HERE — and why it is NOT observed the way 36_ observes it.
36_ triggers its deinit from POST /ports/1/mode, whose handler calls
port_manager_set_mode() synchronously and only answers afterwards
(main/bridge/port_manager.c:1502-1538), so a hang holds the response and the client
sees a requests.ReadTimeout. This test triggers its deinit from POST /settings, and
since commit 021b92963 (2026-07-14) that path is ASYNCHRONOUS: the handler calls
settings_update_with_status() (main/settings_manager.c:942), which does
xTaskCreate(settings_update_task, ...) and returns ESP_OK without joining
(main/settings_update.c:440-448); the 200 is then sent by json_utils_send_response()
(main/settings_manager.c:1010) while cache_modbus_server_release() ->
cache_modbus_server_deinit() (main/settings_update.c:252-255) runs in that separate
task. Both tasks sit at priority 5 and the wait loop yields, so httpd is never
starved — the 200 goes out on time even while the deinit is stuck forever.

Consequence, and the reason this file was rewritten: between 021b92963 and this
change the test could not fail on the bug it names. The POST it timed does not CONTAIN
the deinit — it hands it to another task and answers without joining, so it may return
before, during or after that task runs, and its duration bounds the deinit neither above
nor below. Neither its wall-clock budget nor the HTTP client timeout could see the hang,
and the test passed on firmware with the defect reintroduced. The detector is now a SECOND
POST /settings (step 11) — see the comment there for the mechanism.

Run this test alone:
  pytest api_tests/37_test_cache_server_deinit_hang.py --qemu --qemu-skip-build -s
"""

import qemu_ports
import socket
import struct
import threading
import time
from urllib.parse import urlparse

import pytest

from modbus_helpers import make_mbap_request

CACHE_PORT = qemu_ports.CACHE_MODBUS_HOST_PORT

# Explicit (connect, read) timeout for the two requests this test issues itself rather than
# through WBMGEAPI — the step 11 probe and the post-test liveness check in `finally`.
# Deliberately wider than WBMGEAPI's 30 s scalar default (api_client.py:85/:89), and a TUPLE
# rather than a scalar.
#
# WHY WIDER. Both land on /settings, and the worst POST /settings actually measured on the
# shared CI node is 15.88 s (build #19). On top of that the probe pays
# settings_save_timer_wait() (up to 1 s, main/settings_manager.c:999), the cache server
# release+acquire, and whatever jitter the node is under, while the liveness check runs at the
# most loaded moment of the item, right after the restore calls. Against the 30 s scalar
# default the margin was ~1.9x, and a false expiry in EITHER place is a false accusation of
# firmware rather than a "slow node" report: the probe's reads as "the deinit hung", the
# liveness check's drives a hard WEDGED assert. That is exactly the flake class this work
# exists to remove. The read leg is therefore sized at >= 3 x 15.88 = 47.6 s, rounded to 50.
#
# WHY A TUPLE. In requests a scalar timeout bounds connect and read SEPARATELY
# (conftest.py:1246-1250), and _DelayedSession sends Connection: close so every call opens
# a fresh connection — a scalar 50 would mean 50 s connect + 50 s read = 100 s worst case,
# so the ceiling the marker below quotes would not be a real bound at all. Connect is a
# loopback handshake to a QEMU hostfwd port — immediate or never — so 5 s is already far
# past generous. Resulting ceiling per call: 0.1 s (_DelayedSession.DELAY_S) + 5 + 50 = 55.1 s.
_SETTINGS_HTTP_TIMEOUT = (5, 50)

# THE FIRMWARE CONSTANT THE WHOLE DETECTOR RESTS ON. Every accepted client socket gets
# SO_RCVTIMEO = 100 ms (main/bridge/tcp_server.c:227-228) so that run_receiver() reaches the
# EAGAIN branch and the check_task_exit_req() in it (:388-396) when no data arrives. That
# branch is NOT what the guarded regression removes — the regression removes the post-data
# check at :426-429 — so any gap longer than this lets a DEFECTIVE receiver exit anyway and
# hands this test a false pass. Every gap the GUEST sees must stay under it.
#
# The host cannot measure that gap: nothing in this process observes the guest's recv().
# Step 13 therefore approaches it from two sides, neither of which is the gap itself. Guard A
# measures when bytes left THIS PROCESS, and only across intervals in which the client had
# NOTHING OUTSTANDING. A send gap bounds the gap between ARRIVALS at the guest, which is not
# the gap recv() experiences: recv() DRAINS A QUEUE and EAGAIN fires only when it finds
# nothing buffered for a full timeout, so while requests the guest has not yet answered are
# still in the pipe it returns immediately and cannot starve however long this process pauses.
# Only once every request sent has been answered does "we sent nothing" mean "the guest got
# nothing". Guard B does not measure a gap at all: it decides AFTER THE FACT, from the response
# stream, which of the two exits the receiver actually took — which is the question the gap
# was only ever a proxy for.
_GUEST_RECV_TIMEOUT_S = 0.100

# Guard B's threshold: the longest interval between the last response the guest sent and the
# teardown it performed that is still consistent with the POST-DATA exit this test guards.
#
# A THRESHOLD BETWEEN TWO MEASURED DISTRIBUTIONS, NOT A PHYSICAL CONSTANT — and that is why it
# is not _GUEST_RECV_TIMEOUT_S itself. The EAGAIN exit produces its close gap BY CONSTRUCTION:
# SO_RCVTIMEO is counted from entry into recv(), which the receiver re-enters immediately after
# answering, so the teardown lands ~100 ms after the last response — and one-way latency on the
# response subtracts from what the host measures. A limit set AT 100 ms therefore sits on the
# bottom edge of the defective distribution, where a few milliseconds of drainer lag are enough
# to measure a defective run at 95 ms, pass the guard, and go green on defective firmware.
#
# MEASURED on this host (macOS, QEMU), both distributions produced by this very item:
#   healthy   — post-data exit, clean firmware, 9 runs:
#                                              3 / 3 / 3 / 3 / 3 / 3 / 5 / 6 / 6 ms
#   defective — EAGAIN exit, post-data check deleted AND
#               _SEND_INTERVAL_S temporarily raised to 150 ms
#               so the receiver is forced onto that exit at
#               all (at the normal 20 ms cadence a defective
#               receiver never leaves, and the run fails on
#               the probe's ReadTimeout instead), 6 runs:
#                                              101 / 102 / 102 / 103 / 108 / 117 ms
# The 412 ms quoted in earlier revisions of this file came from a DIFFERENT experiment — the
# same regression PLUS an artificial 300 ms delay in the cache server's response path — and it
# is NOT reproducible under the measurement this file now performs, so it calibrates nothing
# here and no constant is sized on it. It is in particular NOT the 300 ms landing in the gap:
# SO_RCVTIMEO is counted from ENTRY into recv(), which the receiver re-enters immediately after
# answering, so a slower response moves the timestamp of the LAST RESPONSE and leaves the
# interval from there to the teardown alone. What that revision measured was also a different
# quantity: it predates the drainer-owned pair, so it could only have been the CROSS-THREAD
# subtraction (connection_ended_at minus last_response_at), which the note at guard B explains
# is not a well-formed interval at all. Rerunning that same slow-guest experiment for this
# change, without touching the send cadence, produced a 103 ms gap — in line with the
# construction above — while the host-side cadence looked perfect (1621 sends in 32.4 s, 31 ms
# worst gap, 1170 responses, no throttled ticks), which is the case guard A is blind to and
# guard B exists for. Why a "300 ms" guest still answered 1170 requests in 32 s, and why the
# outstanding cap therefore stayed out of it, is worked through at guard A in step 13.
#
# WHY NOT 100 ms, when every defective run measured came out above it. Because that
# distribution is pinned to 100 ms FROM ABOVE and its spread is one-sided in the dangerous
# direction: the jitter is visible (101 -> 117 ms), and the same jitter on the other leg — a
# response whose delivery to this process lags relative to the teardown's — SUBTRACTS from the
# measured gap. A limit at 100 ms would sit ~1 ms from the observed minimum, which is a coin
# flip rather than a threshold. Half the firmware constant keeps 2.0x under the lowest
# defective run and 8.3x over the worst healthy one, and the healthy side is the tight one.
#
# Expressed as a fraction of _GUEST_RECV_TIMEOUT_S rather than as a bare 0.05 because the thing
# it must stay under is that constant: if the firmware ever changes SO_RCVTIMEO, this follows.
_CLOSE_GAP_MAX_S = _GUEST_RECV_TIMEOUT_S / 2

# Host-driven send cadence, 5x under _GUEST_RECV_TIMEOUT_S. The margin is not decoration:
# everything between this process and the guest's recv() — the host socket buffer, slirp, the
# emulated NIC, the guest's own scheduler — can only ADD to a gap, never remove one, and
# step 13's host-side measurement sees none of it. A cadence sitting just under the limit
# would leave nothing for that, exactly where having nothing left costs the detector.
_SEND_INTERVAL_S = 0.020

# Cap on requests sent but not yet answered, so the sender has a hard stop of its own and does
# not depend solely on the peer's flow control.
#
# SIZED AS A BACKSTOP. Hitting this cap makes the sender skip ticks, so the SUBMISSION cadence
# stops being 20 ms — which is the coupling the whole two-thread design exists to remove, and a
# cap that engaged at the first sign of slowness would quietly undo the fix. It engages on
# THROUGHPUT, not latency: the backlog grows whenever the guest answers fewer than
# 1/_SEND_INTERVAL_S requests per second, without bound, however large the cap. Measured
# against a local fake guest, a cap of 64 collapsed the achieved cadence from a flat 20 ms to
# the guest's own 300 ms reply time within ~1.3 s — i.e. back to lockstep. 1024 is ~12 KB of
# 12-byte requests in flight, more than an lwIP receive window holds, which was the argument for
# expecting the kernel's flow control (and with it the send-stall branch in _send_loop) to bite
# before this cap ever did. THAT ARGUMENT IS WRONG, as the next paragraph measures: the cap is
# what engages, and no send stall is recorded. It counts bytes not yet ANSWERED, while the
# window fills only with bytes not yet READ — and a guest that reads promptly and answers
# slowly keeps the window open while the backlog grows.
#
# IT DOES ENGAGE ON THE CI NODE, AND THERE THAT IS THE NORMAL CASE, NOT AN ANOMALY. That node
# answers ~178 ms per response (build #21: 134 responses inside a 23.86 s detector window)
# against ~19 ms on a free developer host — about 9x. A 20 ms sender against a 178 ms guest
# builds a backlog BY CONSTRUCTION, fills 1024 in ~23 s (50 sends/s against ~5.6 answers/s is
# ~44 outstanding added per second) and then throttles, which is exactly
# what #21 measured: 1146 sends, 47 ticks skipped. Nothing here may be written on the
# assumption that the cap is "far away" — in the regime this test ships into it is reached
# routinely, and any earlier claim that every measured window had zero throttled ticks was a
# statement about developer hardware only.
#
# WHAT HAPPENS WHEN IT ENGAGES: the sender waits for a response to free a slot, and NOTHING
# ELSE. It does not starve the guest — throttling happens only while _MAX_OUTSTANDING requests
# sit UNANSWERED, i.e. at the point furthest from "the guest has nothing left to read" — and no
# guard fails on it. Guard A measures only intervals in which the client had NOTHING
# outstanding (see step 13 and _longest_send_gap), so a throttled tick cannot produce a guard A
# failure at all. That scoping is not a nicety: build #21 failed a perfectly valid run because
# the previous guard A read those skipped ticks as a 140 ms host-side gap and called it a
# starved guest, while ~1000 requests sat queued for that guest the whole time.
# stats.throttled_ticks remains in the printed diagnostics as the explanation for a low
# submission rate.
_MAX_OUTSTANDING = 1024

# Hard cap on BOTH timestamp logs step 13 measures over — the sender's and the drainer's. At
# the cadence above this is 20 minutes of sending, i.e. unreachable inside this item's 600 s
# marker; it exists so the lists are bounded by construction rather than by an argument about
# how long the item runs.
#
# ONE FLAG COVERS BOTH LISTS. A response only ever follows the send it answers, so
# len(response_times) can never reach this cap before len(send_times) has, and the sender
# raises stats.send_log_truncated on ITS first dropped timestamp — in all but a few
# instructions' worth of interleaving, earlier. (The exception is the same one the max(0, ...)
# in _longest_send_gap covers: a sender preempted between its sendall() and the append. Both
# lists would have to be at the cap for it to matter, which the arithmetic below rules out
# anyway.) Step 13 skips guard A on that flag, which is what protects the response log too.
#
# OVERFLOW WOULD CORRUPT GUARD A, not merely truncate its input, which is why it is flagged
# rather than ignored. _longest_send_gap's trailing term runs from the last RECORDED send to
# the end of the window (see the note there — that term is what catches a receiver that left
# during the gap that let it). Once the log freezes while sending continues, that term grows
# without bound and guard A would report a gap spanning all time since the last record, i.e.
# blame the sender for a schedule it actually kept; a frozen RESPONSE log would corrupt the
# reconstruction of `outstanding` in the other direction, leaving the client looking
# permanently backlogged and guard A measuring nothing at all. Step 13 therefore reports guard
# A as NOT APPLICABLE instead of asserting on numbers it knows to be wrong. Guard B is
# unaffected: it reads two single timestamps, not these logs. Unreachable at today's constants
# (600 s x 50/s = 30 000 < 60 000), but 600 is a marker constant that can rise, and the failure
# it would cause would look exactly like a real one.
_SEND_LOG_MAX = 60000

# Bounded wait for the DRAINER to observe the connection being torn down, used by step 13.
# The probe being answered and the guest closing our socket are concurrent events (the deinit
# finishes, then the update task clears its handle, then the parked handler answers), so at
# the instant the probe returns the FIN or RST may still be in flight. This wait exits the
# moment the drainer sees it, and also the moment the drainer thread is gone (a thread that
# has exited will never observe anything, so waiting on it is pure delay); it runs to the
# deadline only when the drainer is still reading and no teardown ever arrives.
_CLOSE_OBSERVE_MAX_S = 5.0

# Preconditions of the detector, both in the polling client's own terms (both in step 6).
# Both are WAITS, not budgets: each exits as soon as the thing it proves becomes true, and
# runs to its deadline only when that thing is actually false. Neither is on the measured
# quantity — nothing here bounds the deinit.
_FIRST_RESPONSE_MAX_S = 10.0
_SETTLE_MAX_S = 10.0


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    """Enable the cache Modbus server on guest port 50504 as the baseline for this module."""
    resp = api.update_settings({
        "cache_modbus_server_enabled": True,
        "cache_modbus_port": qemu_ports.CACHE_MODBUS_GUEST_PORT,
        "cache_value_timeout_s": 0,
    })
    assert resp.status_code == 200, (
        f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    )


class _PollStats:
    """What the polling client's threads publish to the test thread.

    It exists because the polling thread used to be UNOBSERVABLE: it swallowed every
    exception and exited silently, so a client the guest never admitted looked exactly
    like one that was polling happily.  The whole detector in step 11 rests on "a client is
    connected and exchanging data with the cache server"; without a positive signal
    that this is true, the test can be green while exercising nothing at all.

    Two writers now, and they do not overlap.  The SENDER owns `sends`, `send_times`,
    `throttled_ticks` and `send_log_truncated`.  The DRAINER owns `responses`,
    `response_times`, `first_response`, `last_response_at`, `drainer_ended_at` and
    `drainer_closed_by_peer`.
    Either thread may set `error`, `closed_by_peer` and `connection_ended_at`.

    "FIRST WRITER WINS" APPLIES TO `error` AND `connection_ended_at` ONLY — those two are
    guarded by an `is None` test before the store, because for them the FIRST cause is the
    diagnostic one.  `closed_by_peer` does NOT follow that rule: _note_peer_close assigns
    True unconditionally, so a peer close observed after an earlier host-side death still
    latches it.  That is deliberate — a peer close is positive evidence whenever it is seen —
    but it makes the flag mean "a peer close was observed at some point", not "the FIRST
    cause was a peer close", and step 10 is written against that weaker meaning.

    WHY THE DRAINER HAS ITS OWN PAIR OF END FIELDS.  Guard B (step 13) subtracts two
    timestamps, and mixing threads there stops the difference from being an interval at all.
    `connection_ended_at` can be stamped by the SENDER, which meets the teardown as
    ECONNRESET/EPIPE on its next 20 ms tick — likely here, because the guest closes with a
    non-empty receive queue and lwIP therefore ends with an RST.  That instant bears no
    ordering relation to where the DRAINER stands in the response stream, so the difference
    can land anywhere, negative included.  `drainer_ended_at`/`drainer_closed_by_peer` are
    therefore written by the drainer alone, from its own observation, and guard B uses only
    them; `connection_ended_at` stays as the cross-thread diagnostic and as the end of guard
    A's window.  The full argument, including which direction each residual error goes, is at
    guard B itself.

    Plain ints and a plain list on purpose: every read from the test thread is either "did it
    move" or a snapshot taken with a single atomic copy, and no read needs a value that is
    exact to the instant.
    """

    def __init__(self):
        # Set on the FIRST fully parsed Modbus response.  A response is the only event
        # that proves the GUEST admitted this client: against the QEMU slirp hostfwd
        # port a bare connect() completes on the HOST before the SYN reaches the guest
        # (see the note on _connect_ready_bridge, conftest.py:571-575), so neither
        # connect() nor sendall() succeeding says anything about firmware state.
        self.first_response = threading.Event()
        self.responses = 0
        # Why the thread stopped, so a failed precondition names a cause instead of
        # just "no response arrived".
        self.error = None
        # True once EITHER thread has seen the PEER close or reset the connection. That is
        # proof the client socket was still live and the firmware tore it down; every other
        # way to stop (host-side error, stop_event) proves nothing about the firmware. Step 10
        # needs exactly this distinction. It is a flag set at the break sites rather than a
        # substring test on `error`, because str(TimeoutError) is "timed out" and
        # type(exc).__name__ is "TimeoutError" — neither contains a lowercase "timeout", so
        # string-sniffing would misclassify the vacuous case as the meaningful one.
        #
        # LATCHES, and is NOT first-writer-wins: a peer close seen late still sets it, even if
        # an earlier host-side death already stamped connection_ended_at. Sound, because the
        # observation itself is positive evidence whenever it happens — but it is why step 10
        # cannot read this flag as "the first cause".
        self.closed_by_peer = False
        # monotonic() at the moment either thread first observed the connection to be GONE
        # (peer close, reset, host-side socket error, unparseable stream). None while it is
        # still up. Step 13 ends guard A's measurement window here: once there is no
        # connection, there is no receiver task left to starve and send gaps stop meaning
        # anything. A send STALL does not set this — the connection is still up then, and the
        # guest really is going hungry, which is exactly what step 13 must catch.
        #
        # NOT usable as one end of guard B's subtraction: it is cross-thread, and the sender
        # can stamp it before the drainer has finished reading (see the class docstring).
        self.connection_ended_at = None

        # monotonic() at which the LAST response was read. Owned by the drainer, and one half
        # of guard B's measurement.
        self.last_response_at = None
        # monotonic() of EVERY response read, appended in order. Owned by the drainer and read
        # only by step 13, which merges it with send_times to reconstruct how many requests
        # were outstanding at each instant — the quantity that decides whether a host-side send
        # gap could have starved the guest at all (see _longest_send_gap). A counter cannot
        # substitute: the question is not how many responses arrived but WHEN the pipeline was
        # empty. Same list-not-deque and same _SEND_LOG_MAX bound as send_times, and it cannot
        # hit that bound first — see the note there.
        self.response_times = []
        # ...the other half: monotonic() at which the DRAINER ITSELF observed the connection
        # end, and whether that observation was a peer close/reset (as opposed to a host-side
        # read error or an unparseable frame, which prove nothing about the firmware). Both
        # are written by the drainer only, so guard B never subtracts across threads. None
        # when the drainer never saw an ending — including the case where only the SENDER did,
        # which step 13 reports as a run that cannot support the inference rather than
        # computing a mixed-source gap.
        self.drainer_ended_at = None
        self.drainer_closed_by_peer = False

        # Send side, owned by the sender thread and read by step 13.
        self.sends = 0
        # monotonic() of every completed sendall(), appended in order. Step 13 needs the
        # LONGEST gap inside a window, which no pair of counters can reconstruct — a mean
        # over thousands of sends hides the single 300 ms stall that disarms the detector.
        # A plain list, not a deque: `list[:]` is one uninterruptible copy under the GIL,
        # while iterating a deque another thread is appending to can raise. Bounded by
        # _SEND_LOG_MAX, which the sender checks before appending.
        self.send_times = []
        # Raised by the sender the first time _SEND_LOG_MAX made it DROP a timestamp. Once
        # that happens send_times no longer describes the sending that actually took place
        # and guard A's gap is not merely imprecise but wrong in the accusing direction — see
        # the note on _SEND_LOG_MAX. Step 13 skips guard A entirely when this is set, which
        # also covers response_times: that list cannot overflow before this one does.
        self.send_log_truncated = False
        # Ticks the sender skipped because _MAX_OUTSTANDING requests were still unanswered.
        # Purely diagnostic: the starvation they cause shows up in step 13's gap measurement
        # on its own, and this says WHY it happened.
        self.throttled_ticks = 0


def _recv_frame_bytes(sock: socket.socket, n: int, stop_event: threading.Event,
                      dead_event: threading.Event):
    """Read exactly n bytes, treating the socket's own timeout as "not yet", not "dead".

    The 1 s timeout on this socket is NOT A HEALTH SIGNAL FOR READS, which is all this
    function cares about: a socket that has gone a second without an answer is still open, and
    the firmware still counts it in desc->active_connections. (The same timeout does have
    three other jobs, and on the write side it IS a health signal — it bounds connect(), it
    lets both threads notice stop_event instead of blocking forever, and a 12-byte sendall()
    that cannot complete within it means the guest has stopped reading. See
    _PollingClient.start and the socket.timeout branch of _send_loop.)

    Treating it as fatal — which modbus_helpers.recv_exactly does implicitly, since
    socket.timeout is an OSError subclass — was the root of two separate fragilities. The
    client could only ever produce its first response inside its first ~1 s, and after that
    ANY round trip slower than 1 s killed it. Both matter here because a dead client closes
    its socket, active_connections drops to zero, the deinit under test completes instantly
    and the item becomes a vacuous pass. Guest startup jitter and node slowness must cost
    time, not the connection.

    Absorbing the timeout is only SAFE because the sender no longer waits on this function:
    with the lockstep loop it replaced, a slow guest also throttled the sends, and absorbing
    the timeout traded a loud failure for a quiet one. Sending is now on its own thread and
    its own clock (_PollingClient), so patience here delays nothing the guest can see.

    The buffer lives HERE rather than in the caller so that a frame split across a timeout is
    RESUMED; re-issuing the request mid-frame would desynchronise the stream.

    dead_event — the SENDER's verdict that the connection is unusable — is checked ONLY AFTER
    a recv() has come back empty-handed, never before it, and that ordering is load-bearing in
    both directions.

    - It must be checked at all, or the drainer outlives the connection. The sender can die
      host-side (a plain OSError on sendall) while this function sits in its recv/continue
      loop forever, leaving is_alive() answering True for a client that is not being served.
      Step 10 is NOT what this protects: it reads closed_by_peer/connection_ended_at rather
      than is_alive(), and an ordinary host-side OSError stamps connection_ended_at while
      closed_by_peer stays False — so that assert already fails such a run whether or not the
      drainer has noticed. What the check protects is every OTHER reader of is_alive(): step
      6's settle loop, which would otherwise keep waiting on a client nothing is feeding, and
      step 13's bounded wait for the drainer to observe the teardown, which would burn its
      full deadline on a thread that can no longer observe one. And the thread itself, which
      would otherwise never exit.
    - It must not be checked FIRST, or the drainer loses guard B. When the sender declares the
      connection dead it has just hit ECONNRESET/EPIPE, which means the RST or FIN is already
      queued on this socket, so one more recv() returns or raises immediately and the DRAINER
      gets to observe the ending itself — the only observation guard B is allowed to measure
      from. Bailing out on the flag alone would hand the sender that race and turn healthy runs
      into "the end was observed only by the sender" inconclusives.

    Returns the n bytes, or None if stop_event fired first, or if dead_event was already set
    and a full recv() timeout then produced nothing (i.e. there is no ending left to observe).
    Raises ConnectionError if the peer closed or reset the connection.
    """
    buf = b""
    while len(buf) < n:
        if stop_event.is_set():
            return None
        try:
            chunk = sock.recv(n - len(buf))
        except socket.timeout:
            if dead_event.is_set():
                # The sender already called this connection dead AND a full second of reading
                # produced nothing, so no teardown is coming for this thread to see. Leave, so
                # is_alive() stops claiming a connection that is NOT being served.
                return None
            continue
        if not chunk:
            raise ConnectionError(f"socket closed after {len(buf)}/{n} bytes")
        buf += chunk
    return buf


def _longest_send_gap(send_times, response_times, t0: float, t1: float):
    """Longest interval inside (t0, t1] during which no request left this process — twice.

    Returns (sends_in_window, longest_gap_s, longest_drained_gap_s).  Caller must pass
    t1 > t0.

    The two figures differ only in which gaps they are allowed to count, and that difference
    is guard A's entire correctness argument.

    longest_gap_s counts EVERY gap.  It is a diagnostic and nothing asserts on it.  A pause in
    submission is not a pause in ARRIVALS at the guest (TCP backpressure alone breaks that
    link), and even a pause in arrivals is not a pause in what recv() sees: recv() DRAINS A
    QUEUE, and the EAGAIN branch this test's detector depends on fires only when recv() finds
    NOTHING buffered for a full SO_RCVTIMEO.  While requests the guest has not yet answered are
    still in the pipe, recv() returns immediately every time and the guest cannot starve
    however long this process pauses.  Asserting on this number is what failed build #21: 1146
    sends against 134 responses — ~1000 requests queued for the whole window — reported as "the
    polling client left the guest with no incoming bytes for 140 ms".

    longest_drained_gap_s counts only the gaps the client spent FULLY DRAINED: every request it
    had sent already answered.  That is the one state in which "we sent nothing" really does
    mean "the guest has nothing" — all our bytes have been consumed and replied to, so the
    receiver is back in recv() with an empty queue and every further millisecond of host-side
    silence is a millisecond of the timeout it is counting down.  Guard A asserts on this.

    OUTSTANDING IS RECONSTRUCTED, NOT RECORDED: the two logs are merged in timestamp order and
    a running counter is stepped +1 per send and -1 per response.  Modbus TCP over one
    connection is answered in order and this client never abandons a request, so the k-th
    response retires the k-th request and the counter is exact up to the two threads' own
    timestamping lag.  The DOMINANT lag is bounded by one round trip and falls in the safe
    direction: a request the guest has already answered but whose reply this process has not
    yet read still counts as outstanding, so such intervals are dropped from the drained set
    and guard A under-fires.

    THAT IS NOT STRICTLY ONE-SIDED, though, and the max(0, ...) below marks where it stops
    being so.  The clamp exists for the race in which the drainer stamps a response BEFORE the
    sender stamps the send it answers — the sender is preempted between its sendall() and the
    append on the next line — and in that ordering the counter reaches zero one event early, so
    a drained interval OPENS at the response instead of at the send that followed it.  The
    interval is then stretched backwards by the sender's own stamping lag: an interval is ADDED
    to the drained set that the client did not fully spend drained, and guard A can over-fire.
    It stays sound because the error is the gap between a syscall returning and the next
    bytecode appending a timestamp — microseconds unless this process is descheduled, and a
    sender descheduled long enough to matter here IS the host stall guard A exists to catch,
    measured slightly early rather than invented — and because it pushes only toward
    "inconclusive" (a red run on healthy firmware), never toward a false pass.

    ONE VISIBLE CONSEQUENCE, so the printed "X of which Y" is not misread as broken.  The two
    figures anchor differently at the left edge: the all-gaps figure starts from the last send
    at or before t0 and CLAMPS to t0 when there is none, while a drained interval is measured
    from the response that opened it even if that response is older than t0.  So when no send
    is logged at or before t0, Y can come out LARGER than X.  Checked by brute force over
    200 000 random event streams: that is the only way the inversion happens — every violation
    had no send at or before t0, and none survived once one was present.  In the shipped flow
    step 6 has already put both sends and responses before t0, so it takes the clamp race above
    (a sendall() before t0 whose timestamp lands after it) to produce the case at all.  Both
    figures still describe real intervals when it does.

    ANCHOR and TRAILING TERM, inherited from the all-gaps version and applied to both figures.
    A gap that STARTED before t0 and ENDED inside the window is measured in full: SO_RCVTIMEO
    is counted from entry into recv(), so a straddling gap can be exactly the one that expires
    once the exit flag is up.  And the final gap runs to t1 with no closing send, because a
    defective receiver that leaves via EAGAIN closes DURING the gap that let it and that gap
    never gets a send to be measured against — without the term, the one case that matters
    measures as zero.  Before the client's first send both figures are zero by construction
    (the counter starts at zero but no drained interval is open, and the all-gaps anchor is
    t0); step 6 has already established that sends AND responses precede t0, so that state is
    never live inside the window.
    """
    events = [(ts, 1) for ts in send_times if ts <= t1]
    events += [(ts, -1) for ts in response_times if ts <= t1]
    # Sends before responses at an identical timestamp: that ordering can only ever leave the
    # counter higher, i.e. fewer drained intervals, which is the safe direction.
    events.sort(key=lambda ev: (ev[0], -ev[1]))

    prev_send = t0        # anchor for the all-gaps figure; keeps a pre-window send
    drained_since = None  # when the pipeline last emptied; None while anything is outstanding
    outstanding = 0
    longest = 0.0
    longest_drained = 0.0
    n = 0
    for ts, delta in events:
        if delta > 0:
            if ts > t0:
                longest = max(longest, ts - prev_send)
                if drained_since is not None:
                    longest_drained = max(longest_drained, ts - drained_since)
                n += 1
            prev_send = ts
            outstanding += 1
            drained_since = None
        else:
            outstanding = max(0, outstanding - 1)
            if outstanding == 0 and drained_since is None:
                drained_since = ts
    longest = max(longest, t1 - prev_send)
    if drained_since is not None:
        longest_drained = max(longest_drained, t1 - drained_since)
    return n, longest, longest_drained


class _PollingClient:
    """A Modbus TCP client whose SEND cadence is set by the HOST, not by the guest.

    Two threads over one socket, which is the entire reason this class exists.

    - The SENDER emits an FC03 request every _SEND_INTERVAL_S off its own clock and never
      waits for a reply.
    - The DRAINER reads replies with _recv_frame_bytes and counts them.

    The obvious single-threaded loop (send -> recv reply -> send) makes the inter-send gap
    EQUAL to the guest's response latency.  That is fatal here and not merely inelegant: the
    firmware only stays wedged while it keeps receiving bytes, because a receiver that goes
    100 ms without data reaches the EAGAIN branch at main/bridge/tcp_server.c:388-396, which
    the guarded regression does not remove, and exits through it even when defective.  On an
    emulated ESP32 under load a lockstep round trip drifts past 100 ms easily, so the
    lockstep client silently turns a defective guest into a passing test.  Decoupled, the
    SUBMISSION interval is a host sleep timer and does not depend on the guest at all.

    Submission is not delivery, though, and this class does not pretend otherwise.  Once the
    guest stops draining, TCP backpressure parks requests in the host's socket buffer and
    sendall() keeps returning on schedule while nothing reaches the guest — measured on this
    very test, a flat 20 ms cadence over 49 s while the guest received nothing and starved.
    Whether the guest was actually fed is decided in step 13, from the response stream.

    Transaction ids keep advancing across the pipeline so the stream stays interpretable;
    nothing correlates a reply to its request, and nothing needs to — what is being proved
    is that the guest is serving this connection, not what it answers.

    The socket is owned by this object, not by either thread: stop() closes it after joining
    them, and it stays open through every non-fatal hiccup.  Closing it early is the one
    thing that would destroy the test, since active_connections would drop to zero and the
    deinit under test would finish for the wrong reason.

    There is deliberately no connect retry and no connect-timeout PARAMETER — connect is
    bounded all the same, at 1 s, because start() calls settimeout() before connect() and a
    socket timeout applies to the handshake too. Against the
    QEMU slirp hostfwd port connect() cannot usefully fail: the port is bound by QEMU for
    the life of the VM, and a guest that is NOT listening still gets a host-side connect()
    success (conftest.py:571-575), surfacing later as an RST or an unanswered request. A
    retry loop would only ever spin on a condition that does not arise, and the condition it
    was imagined to cover is caught by the caller's step-6 response assert instead.
    """

    def __init__(self, host: str, port: int, stop_event: threading.Event,
                 stats: _PollStats):
        self._host = host
        self._port = port
        self._stop_event = stop_event
        self._stats = stats
        # Set when the connection is known to be unusable, so BOTH threads wind down
        # together. Separate from stop_event, which is the test asking them to stop.
        self._dead = threading.Event()
        self._sock = None
        self._sender = None
        self._drainer = None

    def start(self) -> None:
        """Connect and start both threads. A failure is reported through stats.error.

        Does not raise: a connect failure surfaces at step 6's assert, which names the
        cause from stats.error, rather than as a traceback from a helper.
        """
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        # 1 s socket timeout, set BEFORE connect() so it bounds the handshake as well. It does
        # three more jobs afterwards and is a health signal for exactly one of them: reads
        # (_recv_frame_bytes — not a signal, a slow guest is still a connected guest), writes
        # (_send_loop — it IS a signal, a 12-byte sendall() that cannot finish in a second
        # means the guest stopped reading), and letting both threads notice stop_event.
        sock.settimeout(1.0)
        try:
            sock.connect((self._host, self._port))
        except OSError as exc:
            self._stats.error = (f"connect to {self._host}:{self._port} failed: "
                                 f"{type(exc).__name__}: {exc}")
            sock.close()
            self._dead.set()
            return
        self._sock = sock
        self._drainer = threading.Thread(target=self._drain_loop, daemon=True)
        self._sender = threading.Thread(target=self._send_loop, daemon=True)
        # Drainer first, so no reply can be sitting unread while the first request goes out.
        self._drainer.start()
        self._sender.start()

    def is_alive(self) -> bool:
        """True while the drainer thread is still reading this connection.

        The DRAINER is the liveness signal, not the sender: it is the thread that touches
        the read side, so it is the one that learns the connection is gone. A sender that
        stopped while the drainer runs is a starving guest, which step 13 catches by
        measurement, not a dead connection.

        It is a proxy, and it LAGS: when the sender is the one that finds the connection
        dead, the drainer needs up to one socket timeout (1 s) to notice via dead_event —
        see _recv_frame_bytes. Nothing that must be exact about the connection's state reads
        this; step 10 reads stats.connection_ended_at, which is the fact itself.
        """
        return self._drainer is not None and self._drainer.is_alive()

    def stop(self, timeout: float = 3.0):
        """Stop both threads and close the socket. Returns a warning string, or None.

        Closing here rather than in a thread's `finally` is what makes the blast-radius
        recovery in the test's `finally` block deterministic: after this returns the guest
        has seen a FIN whether or not either thread was still healthy enough to notice.

        SHUTDOWN BEFORE JOIN. close() does not wake a thread already blocked in recv() — it
        only drops this process's reference to the fd — while shutdown(SHUT_RDWR) makes that
        recv() return at once, so each thread leaves on its own instead of waiting out its 1 s
        socket timeout. The drain loop treats a ConnectionError raised while stop_event is set
        as this shutdown rather than as the peer, so tearing our own socket down cannot forge
        evidence about the firmware.

        AND THE JOIN RESULT IS NOT IGNORED. If a thread outlives its join, the close() below
        frees an fd NUMBER that the very next HTTP call can be handed (api_client sends
        Connection: close, so every call opens a fresh socket) — leaving the straggler to
        recv()/sendall() on some other connection's socket. A daemon thread cannot be killed,
        so all this can do is say so; the caller prints it. It deliberately does NOT raise:
        stop() is called from the test's `finally`, where an exception would displace the real
        failure.
        """
        self._stop_event.set()
        if self._sock is not None:
            try:
                self._sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass  # already torn down, or never connected: nothing to wake
        straggling = []
        for name, thread in (("sender", self._sender), ("drainer", self._drainer)):
            if thread is not None and thread.is_alive():
                thread.join(timeout=timeout)
                if thread.is_alive():
                    straggling.append(name)
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
        if straggling:
            return (f"polling client {'+'.join(straggling)} thread(s) did not stop within "
                    f"{timeout:.0f}s and the socket has been closed under them; their fd "
                    f"number can be re-used by a later HTTP call, so treat anything odd in "
                    f"the requests that follow as a consequence of this")
        return None

    def _note(self, message: str) -> None:
        """First writer wins: the first cause is the one worth reporting."""
        if self._stats.error is None:
            self._stats.error = message

    def _note_peer_close(self, message: str) -> None:
        # Latches unconditionally, unlike error/connection_ended_at — see the field comment.
        self._stats.closed_by_peer = True
        self._end_connection(message)

    def _end_connection(self, message: str) -> None:
        if self._stats.connection_ended_at is None:
            self._stats.connection_ended_at = time.monotonic()
        self._note(message)
        self._dead.set()

    def _drainer_end(self, message: str, peer_closed: bool) -> None:
        """Record the DRAINER's OWN observation that the connection ended, then end it.

        Called only from _drain_loop, so drainer_ended_at is single-writer and is paired with
        last_response_at — which the same thread stamps — as guard B's two ends. Stamped
        BEFORE the shared bookkeeping so that connection_ended_at can never be earlier than
        the observation it came from when the drainer is the one that saw it first.
        """
        if self._stats.drainer_ended_at is None:
            self._stats.drainer_ended_at = time.monotonic()
            self._stats.drainer_closed_by_peer = peer_closed
        if peer_closed:
            self._note_peer_close(message)
        else:
            self._end_connection(message)

    def _send_loop(self) -> None:
        """Emit one FC03 request per _SEND_INTERVAL_S, off this thread's own clock."""
        tid = 1
        next_send = time.monotonic()
        while not (self._stop_event.is_set() or self._dead.is_set()):
            delay = next_send - time.monotonic()
            if delay > 0:
                # Never longer than one interval, so a stop request is noticed promptly.
                time.sleep(delay)
                continue

            # Advance the schedule on an ABSOLUTE clock so per-iteration overhead cannot
            # accumulate into drift; resync instead of bursting if we ever fall a whole
            # interval behind (a burst would not undo a gap the guest has already seen).
            now = time.monotonic()
            next_send += _SEND_INTERVAL_S
            if next_send < now:
                next_send = now + _SEND_INTERVAL_S

            if self._stats.sends - self._stats.responses >= _MAX_OUTSTANDING:
                self._stats.throttled_ticks += 1
                continue

            request = make_mbap_request(tid, slave_id=1, fc=0x03, start_addr=0, count=1)
            try:
                self._sock.sendall(request)
            except socket.timeout:
                # The send buffer stayed full for a whole second for a 12-byte write: the
                # guest has stopped reading entirely. sendall() does not report how much of
                # the request got out, so continuing would risk splicing a half request into
                # the stream — stop sending, but leave the CONNECTION up (no _end_connection,
                # no close), because it is still registered in active_connections and the
                # starvation that follows is exactly what step 13 must see and report.
                self._note(f"send stalled after {self._stats.sends} requests: the guest "
                           f"stopped reading (socket send timeout)")
                break
            except ConnectionError as exc:
                # As in the drain loop: stop() shuts this socket down before joining, and
                # that reaches a blocked sendall() as EPIPE. Recording it as a peer close
                # would manufacture evidence about the firmware out of our own teardown.
                if self._stop_event.is_set():
                    break
                self._note_peer_close(
                    f"peer closed the connection during send after {self._stats.responses} "
                    f"responses: {type(exc).__name__}: {exc}")
                break
            except OSError as exc:
                if self._stop_event.is_set():
                    break
                self._end_connection(
                    f"send failed after {self._stats.responses} responses: "
                    f"{type(exc).__name__}: {exc}")
                break

            # Count first, timestamp second, and only after a COMPLETED sendall: step 13
            # measures when bytes actually left, not when we intended them to.
            self._stats.sends += 1
            if len(self._stats.send_times) < _SEND_LOG_MAX:
                self._stats.send_times.append(time.monotonic())
            else:
                # Say so rather than silently logging a partial schedule: step 13 must not
                # compute guard A's gap from a log that stopped moving. See _SEND_LOG_MAX.
                self._stats.send_log_truncated = True
            tid = (tid % 65535) + 1

    def _drain_loop(self) -> None:
        """Read and discard replies, counting them, until the connection or the test ends.

        The loop condition tests ONLY stop_event; _dead is handled inside _recv_frame_bytes,
        after a read attempt. Testing it here as well would let a sender-declared death take
        this thread out between frames, before it had a chance to see the teardown itself —
        and the drainer's own observation is the only thing guard B may measure from.
        """
        while not self._stop_event.is_set():
            # Parse the 8-byte MBAP + FC header; then drain any remaining payload.
            try:
                header = _recv_frame_bytes(self._sock, 8, self._stop_event, self._dead)
                if header is None:
                    # stop_event fired mid-read, or the sender had already declared the
                    # connection dead and a full read timeout produced nothing. Either way a
                    # clean exit, not an error, and NOT an observation of the ending.
                    break
                _tid, _proto, length, _uid, _fc = struct.unpack(">HHHBB", header)
                # PLAUSIBILITY CHECK, and it is load-bearing in both directions.
                #
                # Below 2: `remaining` would go negative, the PDU read would be skipped by
                # the `> 0` guard, and a "response" would still be counted — after which
                # every later frame is parsed from the middle of this one, silently, forever.
                #
                # Above the bound: `remaining` can reach 65533, and _recv_frame_bytes has
                # neither a deadline nor a size limit — it would spin on `continue` until
                # stop_event or the peer, never setting stats.error, i.e. a stuck client that
                # looks exactly like a patient one.
                #
                # 253 is one under the protocol's absolute ceiling of 254 (one unit-id byte
                # plus a 253-byte maximum PDU). The exact bound is immaterial: this client
                # only ever asks for one register, so every legitimate reply here has
                # length 5 (normal) or 3 (exception), and anything even close to either
                # bound already means the stream is out of sync.
                if not 2 <= length <= 253:
                    raise ValueError(
                        f"implausible MBAP length {length} in header {header.hex()} — the "
                        f"stream is out of sync")
                remaining = length - 2  # uid and fc already consumed
                if (remaining > 0
                        and _recv_frame_bytes(self._sock, remaining,
                                              self._stop_event, self._dead) is None):
                    break
            except ConnectionError as exc:
                # OUR OWN stop() shutting the socket down looks exactly like a peer close from
                # in here, so check first: recording it as one would forge evidence about the
                # firmware out of the test's own teardown. Nothing reads the stats after
                # stop_event is set, so dropping the record costs nothing.
                if self._stop_event.is_set():
                    break
                # The peer closed or reset the connection: the client WAS alive and the
                # firmware tore it down. Step 10 needs this apart from a host-side death, and
                # step 13 measures from the timestamp taken here.
                self._drainer_end(
                    f"peer closed the connection after {self._stats.responses} "
                    f"responses: {type(exc).__name__}: {exc}", peer_closed=True)
                break
            except (OSError, ValueError, struct.error) as exc:
                if self._stop_event.is_set():
                    break  # same as above: our own shutdown, not a fact about the firmware
                # A host-side socket error, or a frame we cannot parse. Proves nothing about
                # firmware state, so closed_by_peer stays False and step 10 fails.
                #
                # ValueError is the reachable half: it comes from the length check above.
                # struct.error cannot fire — the header is exactly 8 bytes by construction —
                # and is listed only so that a future change to the header read does not
                # turn a parse failure into an escaping exception.
                self._drainer_end(
                    f"recv failed after {self._stats.responses} responses: "
                    f"{type(exc).__name__}: {exc}", peer_closed=False)
                break

            # A parsed response — a Modbus exception reply counts, since what is being
            # proved is that the guest is serving this connection, not what it answers.
            # Counter first, event second: the test reads the counter only after the
            # event, so this order can never show it a zero count.
            now = time.monotonic()
            self._stats.last_response_at = now
            # One timestamp per response, for step 13's reconstruction of `outstanding`.
            # Bounded by the same cap as the send log and unable to reach it first — see
            # _SEND_LOG_MAX — so the sender's truncation flag speaks for this list too.
            if len(self._stats.response_times) < _SEND_LOG_MAX:
                self._stats.response_times.append(now)
            self._stats.responses += 1
            self._stats.first_response.set()


@pytest.mark.qemu
# pytest-timeout budget, raised above pytest.ini's `timeout = 180` and deliberately GENEROUS
# rather than derived. Same value and same reasoning as the sibling test 36_.
#
# Every HTTP call in this item is already bounded by a requests client timeout, so the marker
# has exactly one job: never fire FIRST. A client timeout raises requests.ReadTimeout out of
# the one call that hung, so the traceback frame names the call and the report reads as a
# specific request going unanswered; the marker instead kills the item wherever it happens to
# be, with a bare "Timeout >Ns" that names nothing. (requests' own message carries only the
# host, the port and the read timeout — the call is identified by the frame, not the text.)
# That job needs a number comfortably above every plausible path, not a number derived to one
# decimal place — earlier attempts to derive one produced a different answer every time.
#
# The one cost: a genuinely wedged device sits here for the full 600 s. That is acceptable,
# because a wedged device has already broken the run, and this is 1 item out of 229. Reality
# check: with the named regression reintroduced (the post-data check_task_exit_req deleted
# from run_receiver, main/bridge/tcp_server.c) this item was MEASURED at 62.3 s end to end —
# it fails on the probe's ReadTimeout and the device then recovers, so nothing approaches 600.
@pytest.mark.timeout(600)
def test_cache_server_deinit_with_active_polling(api):
    """
    cache_modbus_server_deinit() must return quickly even when a TCP client is
    actively polling (continuously sending Modbus requests).

    Coverage: BUG-FIX-cache_server_deinit_hang_active_polling

    Before the fix, tcp_server_deinit() (called via
    cache_modbus_server_deinit()) waited forever for active_connections to drop
    to zero.  When the client was continuously sending data, recv() in
    receiver_task never timed out and the exit flag was never checked.

    This test starts a background client that sends FC03 requests on a fixed
    host-side cadence (see _PollingClient for why the cadence may not be the
    guest's), then calls update_settings(cache_modbus_server_enabled=False) —
    the write that asks the firmware to stop the cache server.  That POST is NOT
    the detector: it hands the deinit to a separate task and answers without
    joining it, so it may return before, during or after that task runs and its
    duration bounds the deinit neither above nor below (see the module
    docstring).  The detector is the SECOND POST /settings in step 11,
    issued while the polling client is still connected.  It blocks inside the
    HTTP handler for as long as the deinit task has not finished
    (main/settings_update.c:368-373), so a hang reaches the test as a
    requests.ReadTimeout raised out of that call.

    That detector is only meaningful while a client is genuinely connected AND
    feeding the guest fast enough to keep it wedged, so both preconditions are
    asserted positively.  Connectedness: step 6 requires the polling client's
    response COUNTER to be moving before the trigger, and step 10 requires the
    client to have either survived the trigger POST or been closed BY THE
    FIRMWARE across it — the two outcomes that prove it was still connected when
    the deinit ran.  Feed rate: step 13 decides it from the RESPONSE STREAM
    (guard B) — the firmware must have torn the connection down within
    _CLOSE_GAP_MAX_S of the last response it sent, which is the signature of the
    post-data exit this test guards, as opposed to the EAGAIN exit that survives
    the regression and answers the probe just the same.  That is the guard that
    carries the run, and it is the only one of the three that reads the guest.
    Guard A, alongside it, checks that this process kept its own send schedule
    across the intervals in which the client had NOTHING OUTSTANDING — the only
    intervals in which not sending can starve the guest.  It is NOT a necessary
    condition on every host, and on the target one it is close to vacuous: where
    the guest stays permanently backlogged, as on the CI node, the client is
    essentially never drained and guard A passes having measured almost nothing.
    It exists to catch this PROCESS stalling on a host fast enough for the client
    to drain, not to make the run conclusive.  Without step 6, step 10 and guard
    B the test can be green while exercising nothing; see steps 6 and 13.  Step
    12 sits between the probe and those guards for a reason: every guard assumes
    the probe was ACCEPTED, and that is the assert which establishes it.

    No wall-clock budget is imposed on the MEASURED quantity: nothing asserts on
    how long the deinit takes, and step 14 only prints the timings (see the
    comment there for why any budget over them would measure the wrong thing).
    Wall-clock bounds remain only on the preconditions (_FIRST_RESPONSE_MAX_S,
    _SETTLE_MAX_S) and on the post-test liveness check — none of which is on the
    property under test, and each of which is a wait that exits early when the
    thing it proves is true.
    """
    # Step 1: save original settings for restoration in the finally block.
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
    original_settings = resp.json()
    original_port_mode = original_settings.get("rs485_1", {}).get("port_mode", "disabled")

    stop_event = threading.Event()
    client = None
    # Set by the blast-radius check at the end of `finally` and asserted after it.
    wedged_reason = None

    try:
        # Step 2: enable the cache Modbus server on guest port 50504 and disable the
        # value timeout so the cache always has entries to serve.
        #
        # Timed only as a DIAGNOSTIC printed in step 14 — nothing asserts on it, and the
        # comment there explains why nothing may. It is the same API operation as the
        # measured call (POST /settings carrying cache_modbus_server_enabled), on this
        # machine, in this run, with no polling client attached, so printing the pair makes
        # "the whole machine was slow" distinguishable from "this one call was slow" when
        # the detector in step 11 does fire. Timing it costs no extra API call.
        t_control = time.monotonic()
        resp = api.update_settings({
            "cache_modbus_server_enabled": True,
            "cache_modbus_port": qemu_ports.CACHE_MODBUS_GUEST_PORT,
            "cache_value_timeout_s": 0,
        })
        control_elapsed = time.monotonic() - t_control
        assert resp.status_code == 200, (
            f"update_settings (enable cache server) failed: {resp.status_code} {resp.text}"
        )

        # Step 3: wait for the cache TCP server to start accepting connections.
        time.sleep(1)

        # Step 4: open serial (passive) and enable the cache overlay on port 1 so
        # the cache multimaster is active and the server has a client to interact with.
        resp = api.set_port_mode(1, "passive")
        assert resp.status_code == 200, (
            f"set_port_mode(1, passive) failed: {resp.status_code}"
        )
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, (
            f"set_port_cache(1, True) failed: {resp.status_code}"
        )

        # Step 5: derive the host address from api.base_url and start the
        # background polling client.
        #
        # No _poll_tcp_connect() readiness probe here, and this is not an omission.
        # CACHE_PORT is a QEMU slirp hostfwd port, and against slirp a bare connect()
        # completes on the HOST before the SYN ever reaches the guest (conftest.py:571-575,
        # and the same reasoning at conftest.py:958-965), so the probe returns True
        # whether or not the cache server came up in the guest at all — an assert that
        # cannot fail. Readiness is established below, positively, by the only event that
        # actually reaches the guest: a Modbus response.
        parsed = urlparse(api.base_url)
        host = parsed.hostname or "127.0.0.1"

        stats = _PollStats()
        client = _PollingClient(host, CACHE_PORT, stop_event, stats)
        client.start()

        # Step 6: PRECONDITION OF THE WHOLE DETECTOR — the polling client must actually
        # be ADMITTED and exchanging data before the deinit is triggered.
        #
        # Everything step 11 detects depends on step 8 leaving a settings-update task
        # stuck in `while (desc->active_connections > 0)`. That needs a live client
        # connection registered by the guest. If instead the cache server never came up
        # — the port was taken by a previous module, cache_modbus_server_init() failed and
        # only logged (main/settings_update.c:97-104), or xTaskCreate returned non-pdPASS
        # and settings_manager.c:942 ignored the return of settings_update_with_status()
        # and answered success:true anyway — then slirp still accepts the connect locally,
        # sendall() still succeeds, and the drainer sits waiting for a response that
        # never comes (or is reset by slirp once the guest refuses). Either way the guest
        # holds no connection, so by step 8 active_connections would be 0, the deinit would
        # finish instantly, the probe would answer 200, and the test would be GREEN having
        # exercised nothing. That is the vacuous pass these two asserts close; the sibling
        # test 36_ closes the same hole with server_connections_count from /info
        # (36_:151-171), which is not an option here because main/info_handlers.c:111-119
        # only exposes that counter for TCP_SERVER_1/2, never for the cache server.
        #
        # The bound is a WAIT, not a budget: the drainer now stays alive through slow round
        # trips (see _recv_frame_bytes), so this expires only when the guest never answers
        # at all. 10 s is generous over the sub-second first response seen on free hardware
        # and exists solely so that case fails HERE, naming its cause, instead of drifting
        # into the detector and reading as a firmware hang.
        assert stats.first_response.wait(timeout=_FIRST_RESPONSE_MAX_S), (
            f"polling client never got a Modbus response from the cache server on "
            f"{host}:{CACHE_PORT} within {_FIRST_RESPONSE_MAX_S:.0f} s (last error: "
            f"{stats.error}) — it was not admitted by the guest, so the "
            f"deinit-with-active-client path this test exists to exercise would not be "
            f"exercised at all"
        )

        # ...and it must still be serving NOW, not merely once. A bounded WAIT for the
        # counter to move again, not a fixed sleep with an assert on it: it exits on the first
        # poll that sees the counter move and only burns the full deadline when the thing
        # being proved is actually false, so a slow node costs time instead of a red test.
        #
        # Not instant, though: the loop polls every 50 ms and takes its baseline immediately
        # before the first check, so ~50 ms is the FLOOR even though responses land every
        # ~19 ms. That is 50 ms once per run, which does not justify a tighter poll.
        #
        # is_alive() alone would not do: the drainer stays alive while waiting for a response
        # that never comes. The counter MOVING is what proves the guest is serving this
        # connection right now, which is what step 11's detector rests on.
        n_settle = stats.responses
        settle_deadline = time.monotonic() + _SETTLE_MAX_S
        while (stats.responses <= n_settle and client.is_alive()
               and time.monotonic() < settle_deadline):
            time.sleep(0.05)

        assert client.is_alive() and stats.responses > n_settle, (
            f"polling client is not exchanging data before the deinit is triggered "
            f"(alive={client.is_alive()}, responses {n_settle} -> {stats.responses} "
            f"in {_SETTLE_MAX_S:.0f} s, last error: {stats.error}) — active_connections "
            f"would already be back to 0, so step 11 would prove nothing"
        )

        # Step 7: open the window that step 13 measures the send cadence over, and record
        # the start time for the (diagnostic-only) timing of the trigger POST. Both are the
        # same instant — immediately before the call that SCHEDULES the deinit.
        n0 = stats.responses
        t0 = time.monotonic()

        # Step 8: ask the firmware to stop the cache Modbus server. This is the trigger,
        # NOT the observation point: the handler hands the work to settings_update_task and
        # answers straight away, so this call returns 200 whether or not the deinit it
        # scheduled ever completes. Step 11 is what observes the outcome.
        resp = api.update_settings({"cache_modbus_server_enabled": False})

        # Step 9: measure how long the call took.
        elapsed = time.monotonic() - t0

        # Step 10: the API must have returned HTTP 200.
        assert resp.status_code == 200, (
            f"update_settings (disable cache server) failed: {resp.status_code} {resp.text}"
        )

        # ...and the client must have still been connected while that POST ran. Step 8 covers
        # the body, validation, settings_save_timer_wait() and ~50 NVS writes before
        # settings_update_task even reaches `while (desc->active_connections > 0)`; the
        # worst POST /settings measured on the shared node is 15.88 s, so "it was alive at
        # step 6" is not the same claim. Costs no API call.
        #
        # Two outcomes are consistent with a live client at deinit time, and they are the
        # only two accepted here.
        #   - NO thread has observed the connection ending (connection_ended_at is None): the
        #     socket is open, so the firmware still counts it.
        #   - It ended because the PEER closed or reset it: that is the deinit itself tearing
        #     the client down, i.e. positive proof it had one.
        # `and` would be wrong: on healthy firmware the second outcome is the NORMAL one —
        # the deinit closes the connection and the drainer is already gone by the time the
        # POST returns — so requiring an open socket would fail green runs.
        #
        # NOT is_alive(), which an earlier version used and which is unsound HERE. The drainer
        # thread is only a proxy for the connection and it LAGS by up to one 1 s socket
        # timeout: when the SENDER is the thread that finds the connection dead host-side, the
        # drainer is still sitting in recv() and is_alive() answers True — for exactly the
        # vacuous case this assert exists to reject, which it would then pass.
        # connection_ended_at is the fact rather than a proxy, and whichever thread saw it
        # first has already stamped it.
        #
        # Also not "the response counter moved during the POST", which another version tried:
        # that is true essentially deterministically — the sample would be taken right after
        # step 6 confirmed a response every ~19 ms, and the POST lasts hundreds of ms at
        # minimum — so it asserts nothing and passes a client that died 100 ms in for an
        # unrelated reason. Death by anything other than the peer (host-side socket error, a
        # frame we could not parse) is exactly that vacuous case, and it fails here.
        assert stats.closed_by_peer or stats.connection_ended_at is None, (
            f"polling client died during the trigger POST for a host-side reason "
            f"({stats.error}) rather than being closed by the firmware; "
            f"active_connections may already have been 0 when the deinit ran, so the probe "
            f"below would prove nothing"
        )

        # Step 11: THE DETECTOR. A second POST /settings, issued while the polling client
        # is STILL CONNECTED. If cache_modbus_server_deinit() is stuck, this call cannot be
        # answered, and the hang reaches the test as a requests.ReadTimeout.
        #
        # Mechanism. settings_update_with_status() (main/settings_update.c:302), which every
        # POST /settings runs from main/settings_manager.c:942, reaches an UNBOUNDED wait
        # for the previous settings-update task at main/settings_update.c:368-373:
        #     if (update_task_handle != NULL) {
        #         while (update_task_handle != NULL) { vTaskDelay(10); }
        #     }
        # That loop runs INSIDE the httpd handler, so unlike step 8 it holds the response.
        # update_task_handle is cleared only by the last statement of settings_update_task,
        # and step 8's deinit runs inside that very task — so a deinit that never returns
        # means a handle that is never cleared means a POST that is never answered.
        #
        # Why a no-op body is enough, and why this one is a no-op. The wait (:368-373) sits
        # BEFORE settings_update_with_status() computes its `flags`
        # (main/settings_update.c:406), so it is NOT gated on "did this request change
        # anything" — re-sending exactly what step 8 already wrote still blocks, while
        # changing no device state and being trivially well-formed. What it IS gated on is
        # settings_process_request_json() reaching the call at all: validation failure
        # (main/settings_manager.c:849-855) and an NVS write failure both return ESP_OK
        # early, before :942, and answer 200 with success:false. That is precisely why the
        # assert below checks success and not just the status code — a probe that was
        # rejected never reached the wait and proves nothing.
        #
        # The probe cannot arrive too EARLY. xTaskCreate() strictly precedes the 200 that
        # step 8 received (it happens inside settings_update_with_status(), which returns
        # before json_utils_send_response() at main/settings_manager.c:1010), so by the time
        # step 8's response is in hand update_task_handle is already set — or the task has
        # already finished, which is precisely the "no hang" case. (Both tasks run at
        # priority 5 under a preemptive scheduler, so "already finished" is a real
        # possibility, not a theoretical one — which is exactly why this is an ORDERING
        # claim about xTaskCreate and NOT a claim that step 8's 200 precedes the deinit.)
        # It says nothing about how long the probe may take, which is why
        # _SETTINGS_HTTP_TIMEOUT is used instead of the 30 s scalar default, and nothing
        # about the client still being alive when the deinit runs, which is what step 6 and
        # step 10 assert.
        #
        # ORDERING IS THE MECHANISM — DO NOT MOVE THIS BELOW step 15. The probe must be
        # issued while the polling client still holds its socket open. Stop the client
        # first and the socket closes, desc->active_connections drops to zero, the
        # `while (desc->active_connections > 0)` loop in tcp_server_deinit() exits, the
        # update task finishes, and this POST is answered at once — the test would keep
        # passing while detecting nothing at all.
        #
        # For the same reason the client must keep FEEDING the guest for the whole time this
        # call is outstanding, not merely hold the socket. Step 13 is where that is decided —
        # but by GUARD B, after the fact, from the response stream, which is the guest's own
        # evidence about which exit its receiver took. Guard A does not stand in for it: it
        # measures this process's send schedule only across the intervals in which the client
        # had nothing outstanding, and on a guest slow enough to stay permanently backlogged
        # (the CI node) there are almost none of those, so it can pass having measured
        # essentially nothing. See the discussion at step 13.
        #
        # Do NOT wrap this in try/except. A ReadTimeout escaping here IS the test failing
        # on a real hang; swallowing it would restore the exact defect this replaced. The
        # surrounding finally only restores settings, so the exception propagates.
        #
        # The gap this rests on — if the deinit had no live client to wait on, the probe is
        # answered at once and proves nothing — is closed by steps 6 and 10, not here.
        #
        # api.session.post, not api.update_settings(), so the timeout is this test's own
        # and is a real ceiling rather than a per-phase scalar. Everything else about the
        # request is identical to what api_client.update_settings() would send.
        t_probe = time.monotonic()
        probe_resp = api.session.post(
            f"{api.base_url}/settings",
            json={"cache_modbus_server_enabled": False},
            timeout=_SETTINGS_HTTP_TIMEOUT,
        )
        # Close the cadence window at the same instant the probe was answered. (A probe that
        # times out raises above and never reaches here — that failure is the detected hang
        # and needs no cadence figure to be believed.)
        t1 = time.monotonic()
        probe_elapsed = t1 - t_probe
        n1 = stats.responses
        try:
            probe_body = probe_resp.json()
        except ValueError:
            probe_body = None

        # Step 12: the probe must have been ACCEPTED, not merely answered.
        #
        # BEFORE THE GUARDS BELOW, AND THAT ORDER IS THE POINT. Every guard in step 13 opens
        # with "the probe was answered, so the deinit finished" — a premise this assert is
        # what establishes. A rejected probe (validation failure at
        # main/settings_manager.c:849-855, or a failed NVS write: both answer HTTP 200 with
        # success:false) returns BEFORE settings_update_with_status(), so it never reaches the
        # wait that is the detector, and it is perfectly compatible with a deinit that is still
        # hung and a connection that is still up. Run the guards first and that case spends
        # _CLOSE_OBSERVE_MAX_S waiting for a teardown that cannot come and then fails with a
        # false premise, while the true diagnosis — the one below — never prints. The run is
        # red either way; the diagnosis is the whole point of these guards.
        #
        # WHAT THIS ASSERT MEANS, which is NOT "the deinit is stuck". A response arriving
        # at all proves the handler got PAST `while (update_task_handle != NULL)` — the
        # task finished, so the deinit is precisely NOT stuck. A stuck deinit reaches this
        # test as a requests.ReadTimeout raised by the call above, never as a failed
        # assert; that is where "the deinit is stuck" is diagnosed and it needs no help
        # from here.
        #
        # What a non-200 (401, 500, malformed body) or a 200 carrying success:false means
        # instead is that the probe was ANSWERED BUT NOT ACCEPTED. A rejected settings
        # write answers 200 with {"success": false} — every early return in
        # settings_manager.c returns ESP_OK so the HTTP layer can send the error JSON
        # (conftest.py:1426-1428) — and such a request returns BEFORE reaching
        # settings_update_with_status(), i.e. before the wait that is the whole detector.
        # It is therefore not evidence of a healthy deinit either: this run simply proves
        # nothing, and must not be reported as a pass.
        probe_ok = (probe_resp.status_code == 200
                    and isinstance(probe_body, dict)
                    and probe_body.get("success") is True)
        assert probe_ok, (
            f"probe POST /settings was answered in {probe_elapsed:.2f}s but NOT accepted "
            f"(HTTP {probe_resp.status_code}, body={probe_body!r}) — a rejected settings "
            f"write returns before settings_update_with_status() and never reaches the "
            f"`while (update_task_handle != NULL)` wait, so this run proves nothing about "
            f"cache_modbus_server_deinit() either way. This is NOT a detected hang: a hang "
            f"arrives as a requests.ReadTimeout on this call, not as this assert."
        )

        # Step 13: THE INCONCLUSIVE-RUN GUARDS — was this run CAPABLE of detecting the
        # regression at all?
        #
        # The probe being answered means the deinit finished. On a DEFECTIVE receiver the
        # deinit also finishes — within ~100 ms — as soon as the client leaves a gap longer
        # than the guest's SO_RCVTIMEO: run_receiver() drops into the EAGAIN branch, which
        # still checks the exit flag (main/bridge/tcp_server.c:388-396) because the guarded
        # regression only deletes the post-data check at :426-429, and breaks out. So "the
        # probe was answered" is evidence of healthy firmware ONLY IF the guest never got
        # such a gap while the probe was outstanding. That is a property of THIS RUN's
        # timing, not of the firmware, so it is measured rather than assumed — and a run
        # that fails it is INCONCLUSIVE, which must read as a failure and not as a pass.
        #
        # WINDOW. From t0 (immediately before the trigger POST) to t1 (the instant the probe
        # was answered) — or to the moment the connection ended, if that came first. After
        # the connection is gone there is no receiver task left to starve, so gaps past it
        # carry no information; on healthy firmware the deinit closing the client IS the
        # normal ending, and measuring to t1 regardless would fail every green run. (A
        # connection that ended host-side instead has already failed step 10, so shortening
        # the window here cannot rescue such a run.)
        #
        # LONGEST gap, not the mean: one 300 ms stall disarms the detector exactly as
        # thoroughly as a uniformly slow cadence, and a mean over thousands of sends buries
        # it. The mean is printed too, as context for reading the failure.
        #
        # TWO GUARDS, BECAUSE THE OBVIOUS ONE IS NOT SUFFICIENT ON ITS OWN.
        #
        # (A) The send cadence below is HOST-SIDE HYGIENE, and it is asserted on only across
        # the intervals in which this client had NOTHING OUTSTANDING. It measures when bytes
        # left THIS PROCESS, which is not what the guest received, and it is unsound as a proxy
        # for it in BOTH directions.
        #
        # It can look perfect while the guest starves. A sendall() returns as soon as the host
        # kernel accepts the bytes, so it keeps returning on time while TCP backpressure holds
        # everything in the host's send buffer. This is not hypothetical and reproduces on
        # demand: with the regression reintroduced AND a 300 ms vTaskDelay at the head of the
        # cache server's process_data_from_tcp(), this item measured 1621 sends in 32.42 s at a
        # flat 20 ms mean with a 31 ms worst gap — a perfect host-side cadence — while the
        # guest's emulated NIC dropped frames ("opencores.emac: RX frame dropped"), its
        # receiver starved, took the EAGAIN exit at tcp_server.c:388-396, and the deinit
        # completed. Guard B caught that run at 103 ms; guard A saw nothing wrong with it, and
        # before guard B existed it was a silent pass on defective firmware.
        #
        # THAT RUN'S FULL LINE, because the short version invites an arithmetic that does not
        # work out: "1621 sends in 32.42s (mean 20 ms, LONGEST host-side send gap 31 ms),
        # responses 3 -> 1173, throttled ticks 0". 32.42 s / 20 ms is 1621 ticks, so NOT ONE
        # tick was skipped — which reads as impossible against a guest supposedly taking 300 ms
        # per response, since 1024 outstanding would then fill in ~21 s and throttle the sender
        # (the paragraph on _MAX_OUTSTANDING derives exactly that for the ~178 ms CI node).
        # The resolution is that the delay was never per RESPONSE. process_data_from_tcp()
        # (main/bridge/cache_modbus_server.c:372) is called once per recv() with whatever that
        # recv() returned, and mbtcp_reasm_feed() then answers EVERY frame in the buffer, so a
        # delay at the head of it costs 300 ms per BATCH — and at 50 requests/s a batch holds
        # the ~15 requests that piled up during the previous one (rx_buffer is 1024 B,
        # tcp_server.c:15/:385, i.e. room for ~85 of these 12-byte requests: the batch size is
        # set by the arrival rate, not by the buffer). The guest therefore kept answering at
        # ~36 responses/s (1170 inside the window), the backlog grew only by what the NIC
        # dropped, and it ended around 1621 - 1170 = ~450 outstanding: below the 1024 cap,
        # hence zero throttled ticks. The figure is consistent, it is what the run printed, and
        # it is the intended calibration — a guest that answers almost everything on time and
        # STILL starves its receiver, which is the only shape in which guard A can be perfect
        # and the detector still disarmed. (The regime below, quoted as "~190 ms per response",
        # is the OTHER placement: a delay per FRAME rather than per recv(). Both were run; they
        # are different experiments and their numbers are not comparable.)
        #
        # And it can look terrible while the guest is perfectly fed, which is the correction
        # build #21 forced. recv() DRAINS A QUEUE: EAGAIN fires only when it finds nothing
        # buffered for a full 100 ms, so while requests the guest has not yet answered are
        # still queued it returns immediately and cannot time out however long this process
        # pauses. #21 was a valid run on the CI node — 1146 sends against 134 responses, i.e.
        # ~1000 requests queued across the whole window, 47 ticks skipped by the outstanding
        # cap — failed by a guard A that measured the sender's throttled cadence and announced
        # "the polling client left the guest with no incoming bytes for 140 ms". The guest was
        # never starved; the guard asserted a conclusion its evidence could not support.
        #
        # SO GUARD A NOW MEASURES ONLY THE DRAINED GAPS: intervals in which sends - responses
        # was zero, reconstructed after the fact by merging the send and response logs (see
        # _longest_send_gap). Only then does "this process sent nothing" imply "the guest had
        # nothing to read".
        #
        # REPRODUCED AND MEASURED, not argued. #21's regime was recreated locally by slowing the
        # cache server's response path to ~190 ms per response and stretching the detector
        # window to the node's ~24 s (a delay in settings_update_task before the cache release,
        # so the trigger POST costs what it costs on the node). Clean firmware, one run: 1172
        # sends in 31.84 s, 170 responses, 420 ticks skipped by the outstanding cap, longest
        # send gap of any kind 204 ms — which the OLD guard A failed on, exactly as in #21 —
        # against 0 ms drained, guard B at 2 ms, and the item green. On the same firmware and
        # the same regime with the post-data check deleted, the item still fails through the
        # probe's ReadTimeout, so weakening guard A did not cost the detector its teeth.
        #
        # WHAT IT STILL CATCHES — including the case it was introduced for: this PROCESS
        # stalling (a descheduled sender thread, GIL contention, a host that cannot keep a
        # 20 ms timer) long enough to starve a guest that HAD drained the queue and was sitting
        # in recv(). A guest keeping up is drained by definition between round trips, so such a
        # stall is measured in full and fails the run as inconclusive — correctly, because a
        # defective receiver would have escaped through EAGAIN during it.
        # WHAT IT NO LONGER CATCHES, deliberately: any gap taken while the guest still owed
        # answers, which could not have starved it. The corollary is worth stating plainly —
        # on a guest slow enough to stay permanently backlogged (the CI node at ~178 ms per
        # response) the client is essentially never drained, so guard A measures almost nothing
        # and guard B carries the run on its own. That is the intended division of labour and
        # not a hole: guard B reads the guest itself, and it is the guard that discriminates
        # between the two exits.
        # WHAT NEITHER CATCHES is the first paragraph above: bytes lost or delayed BELOW this
        # process are invisible to guard A by construction, and are guard B's business.
        #
        # (B) The close gap is the guard that actually OBSERVES THE GUEST, and it discriminates
        # between the two exits directly rather than by proxy:
        #   - post-data exit (the path under test): data arrives -> handler answers -> exit
        #     flag checked -> break -> close. The last response we read is immediately
        #     followed by the close. MEASURED on clean firmware: 3-6 ms (9 runs).
        #   - EAGAIN exit (the path that survives the regression): recv() finds nothing for a
        #     full SO_RCVTIMEO -> exit flag checked -> break -> close. The 100 ms is counted
        #     from ENTRY into recv(), which the receiver re-enters immediately after answering,
        #     so the teardown lands ~100 ms after the last response BY CONSTRUCTION. MEASURED
        #     on firmware with the post-data check deleted and the client forced onto that exit
        #     (_SEND_INTERVAL_S raised past the timeout): 101-117 ms (6 runs).
        # So the interval between the last response and the teardown IS the discriminator, and
        # it is measured on the response stream, which only advances when the guest genuinely
        # received and served a request. _CLOSE_GAP_MAX_S sits between those two measured
        # distributions rather than on the firmware constant — see the note there for why
        # putting it AT 100 ms would let a defective run pass.
        #
        # BOTH ENDS COME FROM THE DRAINER, and that is not tidiness: the mixed pair this used
        # to subtract is not a well-formed interval at all.
        #
        # connection_ended_at is stamped by whichever THREAD notices first, and the sender can
        # be that thread. The guest stops reading with a non-empty receive queue — this test
        # pushes ~50 requests/s at it, so the queue is essentially never empty — and its
        # shutdown()+close() at main/bridge/tcp_server.c:301-302 therefore ends with lwIP
        # sending an RST; the sender meets that as ECONNRESET/EPIPE on its next 20 ms tick.
        # That timestamp has no ordering relation to where the DRAINER stands in the response
        # stream. If the drainer still has buffered responses to read, the difference can even
        # come out NEGATIVE — and a negative reading passes any upper bound, i.e. hides a
        # defective run behind a very small number. Whichever way it lands, its error is
        # two-sided and comes from two unrelated mechanisms: the drainer's scheduling, and the
        # phase of the sender's tick.
        #
        # The drainer's own pair IS a well-formed interval: one thread, reading one stream in
        # order, timing the last response it read and the end of that same stream. It cannot
        # go negative, and its only error is that thread's own scheduling lag — which enters at
        # both ends and therefore largely cancels. What does not cancel splits into two cases
        # that push the threshold in OPPOSITE directions and must not be conflated:
        #   - Lag at the CLOSE INFLATES the gap: a HEALTHY run measures larger than it was and
        #     can cross the limit, i.e. a red "proves nothing" on good firmware. This is what
        #     the headroom over the healthy distribution buys off — healthy measures 3-6 ms
        #     against a 50 ms limit, so it takes tens of milliseconds of lag at the close to
        #     turn a healthy run red. It is the reason the limit is not TIGHTENED toward 3-6 ms.
        #   - Lag at the last RESPONSE COMPRESSES the gap toward zero: a DEFECTIVE run measures
        #     smaller than it was and can slip UNDER the limit, i.e. a green pass on firmware
        #     that took the EAGAIN exit. This is the residual soft spot, and it argues the other
        #     way — it is the reason the limit is not RAISED toward the ~100 ms the defective
        #     distribution sits at. Residual risk at 50 ms: compressing a >=101 ms defective run
        #     under the limit needs >51 ms of one-way lag at the last response.
        # Neither lag is observed in practice: the drainer does two recv() calls per response at
        # a few tens of responses per second and is blocked in recv() the rest of the time.
        #
        # THE RST IS ALSO WHY NOTHING HERE LEANS ON TCP ORDERING. Against a FIN, every byte the
        # guest sent before closing arrives before it. Against an RST the untransmitted tail can
        # be dropped with no retransmission, so last_response_at may name an earlier response
        # than the guest's true last one — which inflates the gap. Again toward "inconclusive",
        # never toward a false pass.
        #
        # Consequently: if the ending was observed ONLY by the sender, this run cannot support
        # the inference at all and says so, instead of subtracting timestamps from two threads.
        # Both figures are printed side by side, so a run where they diverge is visible. Across
        # the 9 clean runs measured for this change they agreed to the printed millisecond every
        # single time (3-6 ms), i.e. the drainer was blocked in recv() and saw the teardown
        # first in every one, and the race did not appear on this host. That is evidence about
        # this host, not a proof that it cannot happen — which is exactly why the measurement is
        # no longer built out of two threads' timestamps.
        #
        # A run that took the EAGAIN exit proves nothing about the deleted check either way,
        # so it fails as inconclusive — even if the firmware is in fact healthy.
        # The probe answering and the guest tearing our socket down are concurrent events, so
        # give the DRAINER a bounded moment to see it. The wait also ends when the drainer
        # thread is gone: an exited drainer will never stamp anything, so waiting on it is
        # pure delay.
        close_deadline = time.monotonic() + _CLOSE_OBSERVE_MAX_S
        while (stats.drainer_ended_at is None and client.is_alive()
               and time.monotonic() < close_deadline):
            time.sleep(0.01)

        t_feed_end = min(t1, stats.connection_ended_at or t1)
        assert t_feed_end > t0, (
            f"THIS RUN PROVES NOTHING about cache_modbus_server_deinit(): the polling "
            f"client's connection ended {t0 - t_feed_end:.2f}s BEFORE the trigger POST was "
            f"even issued ({stats.error}), so active_connections was already 0 when the "
            f"deinit ran and the probe was never going to block"
        )
        window_s = t_feed_end - t0
        # Both logs are snapshotted with a single `list[:]` each, as the field notes explain.
        window_sends, longest_gap, longest_drained_gap = _longest_send_gap(
            stats.send_times[:], stats.response_times[:], t0, t_feed_end)
        mean_interval_ms = (window_s / window_sends * 1000.0) if window_sends else float("inf")
        # Guard B's measurement: drainer-owned at BOTH ends. Never mixed with the sender's.
        close_gap = (
            stats.drainer_ended_at - stats.last_response_at
            if stats.drainer_ended_at is not None and stats.last_response_at is not None
            else None
        )
        # The same quantity computed from the CROSS-THREAD stamp, printed only — never
        # asserted on. Its relation to close_gap is TWO-SIDED, not one-sided. When the DRAINER
        # observed the ending first, _drainer_end stamps drainer_ended_at and only then calls
        # _end_connection, so connection_ended_at is stamped marginally AFTER it (same thread,
        # next statement) and this figure comes out marginally LARGER. When the SENDER got
        # there first, connection_ended_at can be far earlier — so a visibly SMALLER figure is
        # the signature of that case: the sender saw the teardown before the drainer did, and
        # the two stamps describe different points in the stream. That divergence is the thing
        # guard B refuses to measure across.
        sender_gap = (
            stats.connection_ended_at - stats.last_response_at
            if stats.connection_ended_at is not None and stats.last_response_at is not None
            else None
        )
        if stats.closed_by_peer:
            ending = "closed by the peer"
        elif stats.connection_ended_at is not None:
            ending = f"ended host-side ({stats.error})"
        else:
            ending = "still up"
        # Two host-side figures, and only the second is asserted on. `gap_all` is the longest
        # gap in submission of any kind — informative, but a gap taken while the guest still
        # owed answers cannot have starved it. The second, the "of which" one, is the longest
        # the client spent with nothing outstanding, which is the only kind guard A can draw a
        # conclusion from. Normally the second is a part of the first and the "of which" reads
        # literally; in one corner it can print LARGER, because the two anchor differently at
        # t0 — see the note at the end of _longest_send_gap, which also says why the verdict is
        # unaffected.
        #
        # `gap_pair` renders BOTH as one phrase, so that a truncated send log says
        # "n/a (send log truncated)" once instead of twice inside one sentence. `gap_all` alone
        # is still used where only the first figure is quoted.
        if stats.send_log_truncated:
            gap_all = "n/a (send log truncated)"
            gap_pair = gap_all
        else:
            gap_all = f"{longest_gap * 1000:.0f} ms"
            gap_pair = (f"{gap_all}, of which {longest_drained_gap * 1000:.0f} ms "
                        f"with nothing outstanding")
        print(
            f"detector window: {window_sends} sends in {window_s:.2f}s (mean "
            f"{mean_interval_ms:.0f} ms, LONGEST host-side send gap {gap_pair}, limit "
            f"{_GUEST_RECV_TIMEOUT_S * 1000:.0f} ms on the latter only), responses "
            f"{n0} -> {n1}, throttled ticks "
            f"{stats.throttled_ticks}, connection {ending}, last response -> teardown "
            f"{'n/a' if close_gap is None else f'{close_gap * 1000:.0f} ms'} as seen by the "
            f"drainer / "
            f"{'n/a' if sender_gap is None else f'{sender_gap * 1000:.0f} ms'} from the "
            f"cross-thread stamp (limit {_CLOSE_GAP_MAX_S * 1000:.0f} ms, drainer figure "
            f"decides)"
        )

        # GUARD (B), the one that observes the guest. See the discussion above.
        assert stats.drainer_closed_by_peer and close_gap is not None, (
            f"THIS RUN PROVES NOTHING about cache_modbus_server_deinit(): the probe was "
            f"answered AND accepted (step 12), so the deinit finished and tcp_server_deinit() "
            f"saw active_connections reach 0 — but the drainer, the thread that reads this "
            f"connection, never observed the firmware closing it "
            f"(drainer_closed_by_peer={stats.drainer_closed_by_peer}, "
            f"drainer_ended_at={stats.drainer_ended_at!r}, last_response_at="
            f"{stats.last_response_at!r}) within {_CLOSE_OBSERVE_MAX_S:.0f} s of that answer. "
            f"Cross-thread view for comparison: closed_by_peer={stats.closed_by_peer}, "
            f"connection_ended_at={stats.connection_ended_at!r}, error={stats.error} — if "
            f"THOSE show a peer close and the drainer's do not, the sender saw the teardown "
            f"and the drainer did not, and the two timestamps must not be subtracted from each "
            f"other. Either way: whatever made the count reach 0, this client did not watch "
            f"the receiver task let go of THIS connection, so the run did not exercise the "
            f"deinit-with-active-client path"
        )
        assert close_gap < _CLOSE_GAP_MAX_S, (
            f"THIS RUN PROVES NOTHING about cache_modbus_server_deinit(): the firmware tore "
            f"the connection down {close_gap * 1000:.0f} ms after the last response it sent, "
            f"over the {_CLOSE_GAP_MAX_S * 1000:.0f} ms limit (half the "
            f"{_GUEST_RECV_TIMEOUT_S * 1000:.0f} ms SO_RCVTIMEO on every accepted socket, "
            f"main/bridge/tcp_server.c:227-228 — measured healthy runs land at 3-6 ms and "
            f"EAGAIN-exit runs at 101-117 ms, so this is the wrong side of the divide). That "
            f"is the signature of the EAGAIN exit at tcp_server.c:388-396 — recv() found "
            f"nothing for a full timeout and left through the branch the guarded regression "
            f"does NOT touch — not of the post-data check at :426-429 that this test exists to "
            f"guard. A defective receiver leaves exactly this way and its deinit completes "
            f"too, so the probe answering says nothing here. Host side over the same window: "
            f"{window_sends} sends in {window_s:.2f}s, longest send gap {gap_pair}, "
            f"{stats.throttled_ticks} ticks skipped by the {_MAX_OUTSTANDING}-request "
            f"outstanding cap — if those look healthy, the bytes were lost or delayed BELOW "
            f"this process (host socket buffer, slirp, the guest's own NIC) rather than not "
            f"submitted. Either way the guest starved and the detector was disarmed; this is "
            f"NOT a firmware failure"
        )

        # GUARD (A), host-side hygiene only — neither necessary nor sufficient, and asserted
        # only over the DRAINED part of the window. It is not a necessary condition on every
        # host, and on the target one it is close to vacuous: where the guest stays permanently
        # backlogged, as on the CI node, the client is essentially never drained and this guard
        # passes having measured almost nothing. That is by design — it catches this PROCESS
        # stalling on a host fast enough for the client to drain, and does not make the run
        # conclusive. DO NOT RE-TIGHTEN IT into a necessary condition: asserting on the
        # unscoped send cadence is exactly what failed the valid build #21 run. See the
        # docstring above for what the DRAINED scoping does and does not buy.
        #
        # SKIPPED, not softened, when the send log overflowed: past _SEND_LOG_MAX the logs stop
        # recording while the connection keeps working, and _longest_send_gap's trailing term
        # then measures "time since the last RECORD", not "time since the last SEND", while the
        # reconstructed `outstanding` drifts from the truth in the other direction. Asserting
        # on either would be asserting on a number known to be wrong. Unreachable at today's
        # constants — see _SEND_LOG_MAX — so this branch exists to stay correct if the 600 s
        # marker grows, not because it has ever been taken.
        if stats.send_log_truncated:
            print(
                f"guard A (host-side send cadence): NOT APPLICABLE — the send log hit its "
                f"{_SEND_LOG_MAX}-entry cap, so the longest-gap figure would measure time "
                f"since the last RECORDED send rather than since the last actual send, and "
                f"the outstanding-request reconstruction it is scoped by would be wrong too. "
                f"Guard B above is unaffected: it reads two timestamps, not these logs."
            )
        else:
            assert longest_drained_gap < _GUEST_RECV_TIMEOUT_S, (
                f"THIS RUN PROVES NOTHING about cache_modbus_server_deinit(): the polling "
                f"client went {longest_drained_gap * 1000:.0f} ms without sending WHILE IT HAD "
                f"NOTHING OUTSTANDING — every request it had sent was already answered, so the "
                f"guest's receiver spent all of that back in recv() with an empty queue — over "
                f"the {_GUEST_RECV_TIMEOUT_S * 1000:.0f} ms SO_RCVTIMEO set on every accepted "
                f"socket (main/bridge/tcp_server.c:227-228). A gap that long lets even a "
                f"DEFECTIVE receiver leave through the EAGAIN branch at "
                f"tcp_server.c:388-396 — which the guarded regression does not touch — so "
                f"the deinit completes and the probe is answered whether the fix is present "
                f"or not. Window: {window_sends} sends in {window_s:.2f}s, longest gap of any "
                f"kind {gap_all} (not asserted on, and deliberately: while the guest still owes "
                f"answers recv() returns immediately and no host-side pause can starve it), "
                f"{stats.throttled_ticks} ticks skipped by the {_MAX_OUTSTANDING}-request "
                f"outstanding cap, last client error: {stats.error}. This is NOT a firmware "
                f"failure and must not be read as one: this PROCESS missed its own send "
                f"schedule at the one moment the guest had nothing else queued. On a machine "
                f"that cannot sustain a sub-{_GUEST_RECV_TIMEOUT_S * 1000:.0f} ms send cadence "
                f"this test cannot detect its regression at all, and failing loudly here is "
                f"the correct outcome"
            )

        # Step 14: timings, printed only. NOTHING ASSERTS ON THEM, AND NOTHING MAY.
        #
        # `elapsed` (step 8) does not BOUND the property under test. Since 021b92963 that POST
        # hands the deinit to settings_update_task and answers without joining it; both tasks
        # run at priority 5 under a preemptive scheduler, so the 200 may go out before, during
        # or after the deinit, and `elapsed` bounds the deinit neither above nor below. A
        # budget over it therefore guards nothing, tight or loose. The 15 s constant it used
        # to carry was bounding the wrong quantity, and so would any replacement.
        #
        # Making it relative to `control_elapsed` would not rescue it: a bound over a
        # quantity that does not bound the deinit guards nothing, whatever its shape.
        # (Unlike 36_, where the timed call DOES contain the deinit.)
        #
        # `probe_elapsed` DOES bound it — roughly however much of the deinit was
        # left when the probe arrived, plus one settings round trip. It is deliberately left
        # unasserted: it is already bounded by _SETTINGS_HTTP_TIMEOUT's 50 s read leg, whose
        # expiry is the detector in step 11. Any tighter wall-clock bound would re-import
        # the calibration flake that turned build #19 red on a green commit — CI runs on a
        # shared node where the full suite takes ~124 min against ~35 min on free hardware
        # (builds #17/#18/#19), and a constant calibrated on one is wrong on the other.
        #
        # Control caveat, so nobody reads more into the printed pair than it carries: in
        # steady state the control skips the cache server lifecycle entirely, because the
        # module-scoped _baseline fixture already enabled the server on the same port and
        # cache_modbus_wanted_port() == cache_modbus_server_get_port()
        # (main/settings_update.c:55). But if an earlier module left the cache server
        # DISABLED, _baseline itself starts the async task, and the control call then pays
        # the update_task_handle wait for it. So the control carries order-dependent
        # variance — fine for a diagnostic, disqualifying for a bound.
        print(
            f"cache server disable POST (async, does not contain the deinit): "
            f"{elapsed:.2f}s, control (same op, no client): {control_elapsed:.2f}s, "
            f"detector probe (blocks until the deinit task finishes): {probe_elapsed:.2f}s, "
            f"polling client requests/responses: {stats.sends}/{stats.responses}"
        )

        # Step 15: stop the polling client. Never before step 11 — see the ordering note.
        # A straggling thread is printed, not raised on: it invalidates nothing that has
        # already been asserted, and the reason it matters (a recycled fd number) is about
        # the requests that come AFTER it, which is what the message says.
        stop_warning = client.stop(timeout=3.0)
        if stop_warning is not None:
            print(f"WARNING: {stop_warning}")

        # Step 16: the web interface must still be responsive after the deinit.
        resp = api.get_settings()
        assert resp.status_code == 200, (
            f"Web interface unresponsive after cache server deinit: {resp.status_code}"
        )

    finally:
        # Ensure the polling client is stopped regardless of test outcome.
        #
        # BLAST RADIUS — read this before changing the order of anything below.
        # The step 11 probe deliberately parks the device's single httpd worker on an
        # UNBOUNDED wait. Before this change the test only timed a POST and had no such
        # reach; now it can leave the whole HTTP API wedged, and 12 files run after this
        # one. Recovery today rests entirely on this line: stop() sets stop_event, SHUTS THE
        # SOCKET DOWN, joins both threads and closes the socket itself, so the guest sees the
        # teardown whether or not either thread was still healthy enough to notice;
        # desc->active_connections drops to zero, the `while (desc->active_connections > 0)`
        # loop in tcp_server_deinit() exits, the update task finishes and clears
        # update_task_handle, and the parked handler returns.
        #
        # The 3 s per join is HEADROOM, NOT A BOUND. The shutdown() makes a thread parked in
        # recv()/sendall() return immediately, so the typical join is instant and the 1 s
        # socket timeout is the fallback rather than the expected cost — but nothing here can
        # force a daemon thread to finish, so stop() checks the join result and returns a
        # warning instead of assuming it succeeded. Recovery of the DEVICE does not depend on
        # that: the socket is closed either way, which is what the guest is waiting on.
        #
        # That recovery covers the NAMED regression only. A different deinit regression
        # need not be reachable this way at all — e.g. an acceptor task that never sets
        # EVENT_TASK_FINISHED leaves tcp_server_deinit() in xEventGroupWaitBits(...,
        # portMAX_DELAY) at main/bridge/tcp_server.c:943, one wait EARLIER than the
        # connection count, where closing the client changes nothing. update_task_handle
        # is then never cleared, httpd stays wedged until the device reboots, and every
        # later file fails on requests that never get answered. Hence the liveness check
        # at the end of this block: one wedged device must read as ONE diagnosis, not as
        # twelve independent bugs.
        stop_event.set()
        if client is not None:
            # Printed, never raised: an exception from `finally` would displace the real
            # failure. Idempotent when step 15 already ran (both threads gone, socket closed).
            stop_warning = client.stop(timeout=3.0)
            if stop_warning is not None:
                print(f"WARNING: {stop_warning}")

        # Disable the cache overlay and the port before restoring settings.
        try:
            api.set_port_cache(1, False)
        except Exception:
            pass
        try:
            api.set_port_mode(1, "disabled")
        except Exception:
            pass

        # Restore original settings (all cache-related keys and any others
        # that were changed during the test — mirrors the approach in test 36).
        try:
            api.update_settings(original_settings)
        except Exception:
            pass

        # Restore original port mode.
        try:
            api.set_port_mode(1, original_port_mode)
        except Exception:
            pass

        # Blast-radius check: did the device survive this test at all? One cheap, bounded
        # GET — see the note at the top of this block for why this test, unlike its
        # predecessor, can leave the HTTP API wedged for every file that follows.
        #
        # It PRINTS here and ASSERTS after the block, never raises inside it: an exception
        # raised in `finally` would displace whatever real failure came before it (the same
        # rule conftest's teardown follows). The print goes straight to the terminal —
        # api_tests/pytest.ini sets -s, so there is no capture and it does not attach to the
        # failure report — and it is the only signal when the body already raised, because
        # the assert below is then never evaluated.
        try:
            live = api.session.get(f"{api.base_url}/settings",
                                   timeout=_SETTINGS_HTTP_TIMEOUT)
            if live.status_code != 200:
                wedged_reason = f"HTTP {live.status_code}"
        except Exception as exc:  # pylint: disable=broad-except
            wedged_reason = f"{type(exc).__name__}: {exc}"
        if wedged_reason is not None:
            print(f"WEDGED: device HTTP server is wedged, subsequent tests are "
                  f"unreliable ({wedged_reason})")

    # Only reachable when the body completed, i.e. the step 11 probe WAS answered — so the
    # httpd worker was not left parked by it, and the device went unresponsive during the
    # restore calls in `finally` instead. (When the probe times out the body raises and this
    # line never runs; that failure is the ReadTimeout, and the WEDGED print above is the
    # only blast-radius signal.)
    assert wedged_reason is None, (
        f"device HTTP server is wedged, subsequent tests are unreliable "
        f"({wedged_reason}) — GET /settings did not answer after this test finished "
        f"restoring state, even though the step 11 probe had been answered normally. "
        f"Treat every later failure in this run as a consequence of this one, not as an "
        f"independent bug"
    )
