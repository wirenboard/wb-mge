"""web_port setting test — verifies that the HTTP server starts on the configured port after reboot.

Requires QEMU with hostfwd 8081->8081 (see conftest.py).
Skip on real hardware: no hostfwd guarantee exists there.
"""

import qemu_ports
import sys
import time
import warnings

import pytest
import requests

from api_client import WBMGEAPI


# Port inside the firmware (NVS key web_port)
ALT_PORT_GUEST = qemu_ports.ALT_PORT_GUEST
# Corresponding host-side QEMU hostfwd port (127.0.0.1:8081 -> guest:8081)
ALT_PORT_HOST = qemu_ports.ALT_PORT_HOST
# Default host-side port for the default web port mapping (127.0.0.1:8080 -> guest:80)
DEFAULT_PORT_HOST = qemu_ports.DEFAULT_PORT_HOST
# Firmware-side default web port, i.e. what the hostfwd above maps onto
# (HTTP_SERVER_DEFAULT_PORT in main/http_server.h).
DEFAULT_PORT_GUEST = 80

# ── Probe timeouts ───────────────────────────────────────────────────────────────────────────
# A loopback TCP connect to a QEMU hostfwd port either answers or it does not, so these only
# need to cover "the socket is there but the httpd task is busy", not a slow response body.
_REACHABLE_TIMEOUT_S = 5   # one-shot _is_reachable() check
_PROBE_TIMEOUT_S = 2       # per-probe timeout inside the polling helpers
_POLL_INTERVAL_S = 1       # sleep between polling iterations

# ── Cold-boot window ─────────────────────────────────────────────────────────────────────────
# One shared ceiling for "the device is rebooting and we are waiting for it to answer again",
# used by the test body AND by the teardown, because after this commit both of them can be
# waiting out a reboot THEY sent (see _restore_default_web_port).
#
# 600 s, matching the precedent at 33_test_auth_settings.py:257-262, which argues the number
# out: a cold ESP32 boot under QEMU on a CPU-starved CI node "can take well over two minutes",
# so 600 s is ~5x the observed worst case. It replaces the 1800 s this file used before; 1800 s
# is the number the pre-reboot waits elsewhere in the suite use, but it does not fit any
# honest budget here (see the budget block below) and buys nothing above 600 s except a
# guarantee that pytest-timeout's SIGALRM, not this wait, is what ends a hung run.
_COLD_BOOT_WAIT_S = 600

# ── Teardown windows ─────────────────────────────────────────────────────────────────────────
_RESTORE_PORT_WAIT_S = 300      # find a port the device is serving on before writing through it
_RESTORE_DEFAULT_WAIT_S = _COLD_BOOT_WAIT_S  # back on the default port after the restore reboot
_RESTORE_RETRY_PORT_WAIT_S = 60      # second pass: is the device serving ANYWHERE after that?
_RESTORE_RETRY_DEFAULT_WAIT_S = 120  # second pass never reboots, so only the runtime apply
_RESTORE_REARM_S = 300          # api fixture reconnect + re-auth once the port already answers
_RESTORE_ATTEMPTS = 4           # POST /auth + POST /settings tries per port
_RESTORE_ATTEMPT_PAUSE_S = 2
_RESTORE_REWRITE_ATTEMPTS = 2   # last-resort rewrite, on a device already confirmed healthy
_RESTORE_READ_ATTEMPTS = 3      # GET /settings tries per readback
_RESTORE_READ_PAUSE_S = 2

# Used for the default-port wait when the test body already failed. The device is then probably
# wedged and the full window above would burn ten minutes before the report reaches the real
# failure — the same trade 33_test_auth_settings._rearm_api_session documents. The failfast path
# ALSO skips the restore's own reboot; see _restore_default_web_port for why the two go together.
_RESTORE_FAILFAST_WAIT_S = 300

# ── Timeout budget ───────────────────────────────────────────────────────────────────────────
# pytest-timeout wraps pytest_runtest_protocol, so setup + call + teardown of this item share
# ONE budget, and api_tests/pytest.ini sets no timeout_method — which means the `signal` method:
# SIGALRM fires wherever execution happens to be, including in the middle of the finally block
# below. An alarm there leaves the device on the alt port with no "TEARDOWN FAILED" message,
# i.e. exactly the state this teardown exists to prevent. So the marker has to cover the body
# and the teardown SUMMED, not maxed, and the arithmetic is written out here so that raising any
# window shows what it costs. Every term is a read timeout, so the total is a lower bound on the
# wall clock, but it is the only part that is knowable from the constants.
#
# One api_client call, worst case: its read timeout plus the 0.1 s _DelayedSession pre-delay
# (api_client.py:11), rounded up to whole seconds. Connect is a loopback handshake — negligible.
_CALL_AUTH_S = 11      # WBMGEAPI.auth              (api_client.py:65,     timeout=10)
_CALL_INFO_S = 11      # WBMGEAPI.get_info          (api_client.py:72,     timeout=10)
_CALL_SETTINGS_S = 31  # WBMGEAPI.get/update_settings (api_client.py:79/83, timeout=30)
_CALL_CMD_S = 31       # WBMGEAPI.execute_command   (api_client.py:212,    timeout=30)

# Each polling helper checks its deadline at the TOP of the loop, so a wait can overshoot its
# nominal window by one whole iteration. These are the overshoots.
_URL_POLL_OVERRUN_S = _PROBE_TIMEOUT_S + _POLL_INTERVAL_S           # _wait_for_url
_BASE_POLL_OVERRUN_S = 2 * _PROBE_TIMEOUT_S + _POLL_INTERVAL_S      # _wait_for_serving_base, 2 urls
_READY_POLL_OVERRUN_S = _POLL_INTERVAL_S + _CALL_AUTH_S + _CALL_INFO_S  # api.wait_for_ready

_WRITE_COST_S = (_RESTORE_ATTEMPTS * (_CALL_AUTH_S + _CALL_SETTINGS_S)
                 + (_RESTORE_ATTEMPTS - 1) * _RESTORE_ATTEMPT_PAUSE_S)          # 174
_REWRITE_COST_S = (_RESTORE_REWRITE_ATTEMPTS * (_CALL_AUTH_S + _CALL_SETTINGS_S)
                   + (_RESTORE_REWRITE_ATTEMPTS - 1) * _RESTORE_ATTEMPT_PAUSE_S)  # 86
_READ_COST_S = (_RESTORE_READ_ATTEMPTS * _CALL_SETTINGS_S
                + (_RESTORE_READ_ATTEMPTS - 1) * _RESTORE_READ_PAUSE_S)         # 97

# Test body: precondition probe, POST /settings, POST /cmd reboot, wait for the alt port,
# "default port is gone" probe.
_BODY_BUDGET_S = (_REACHABLE_TIMEOUT_S
                  + _CALL_SETTINGS_S
                  + _CALL_CMD_S
                  + _COLD_BOOT_WAIT_S + _URL_POLL_OVERRUN_S
                  + _REACHABLE_TIMEOUT_S)                                       # 675

# Teardown, worst case: every branch of _restore_default_web_port taken, on the slow (non
# failfast) path. The failfast path is strictly cheaper (300 s instead of 600 s for the
# default-port wait, and no reboot at all), so it does not need its own line.
_TEARDOWN_BUDGET_S = (_RESTORE_PORT_WAIT_S + _BASE_POLL_OVERRUN_S               # 305
                      + _WRITE_COST_S                                           # 174
                      + _CALL_AUTH_S + _CALL_CMD_S                              #  42  reboot
                      + _RESTORE_DEFAULT_WAIT_S + _URL_POLL_OVERRUN_S           # 603
                      + _RESTORE_RETRY_PORT_WAIT_S + _BASE_POLL_OVERRUN_S       #  65
                      + _WRITE_COST_S                                           # 174
                      + _RESTORE_RETRY_DEFAULT_WAIT_S + _URL_POLL_OVERRUN_S     # 123
                      + _RESTORE_REARM_S + _READY_POLL_OVERRUN_S                # 323
                      + _READ_COST_S                                            #  97
                      + _REWRITE_COST_S                                         #  86
                      + _READ_COST_S)                                           #  97 => 2089

# Fixture setup + fixture teardown are charged to this item too. 45 s is the allowance every
# @pytest.mark.timeout comment in this suite quotes; it is dominated by the module-scoped rs485
# restore, whose ceiling conftest.py:566-568 computes as 41.2 s.
_FIXTURE_BUDGET_S = 45

_TEST_TIMEOUT_S = _BODY_BUDGET_S + _TEARDOWN_BUDGET_S + _FIXTURE_BUDGET_S       # 2809


def _is_reachable(url, timeout=_REACHABLE_TIMEOUT_S):
    """Returns True if url returns any HTTP response within timeout seconds."""
    try:
        requests.get(url, timeout=timeout)
        return True
    except requests.exceptions.RequestException:
        return False


def _wait_for_url(url, timeout, probe_timeout=_PROBE_TIMEOUT_S, interval=_POLL_INTERVAL_S):
    """Poll url until an HTTP response is received or timeout expires. Returns True on success."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if _is_reachable(url, timeout=probe_timeout):
            return True
        time.sleep(interval)
    return False


def _wait_for_serving_base(base_urls, timeout, probe_timeout=_PROBE_TIMEOUT_S,
                           interval=_POLL_INTERVAL_S):
    """Poll several base URLs and return the first one that answers, or None if none does.

    The restore has to write through whichever port the device is actually on, and that is not
    knowable up front: after a passing body it is the alt port, but a body that failed before
    the switch took effect leaves the device on the default port with NVS possibly already
    holding the alt one — the state that breaks the NEXT reboot rather than this test.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for base_url in base_urls:
            if _is_reachable(f"{base_url}/favicon.webp", timeout=probe_timeout):
                return base_url
        time.sleep(interval)
    return None


def _write_web_port(base_url, port, attempts=_RESTORE_ATTEMPTS, pause_s=_RESTORE_ATTEMPT_PAUSE_S):
    """POST web_port=<port> through base_url, retrying a refused or dropped request.

    Returns None when the device confirmed the write, otherwise a string describing the last
    failure. A single RemoteDisconnected must not be terminal here: the firmware applies a
    web_port change at RUNTIME from an async task (main/settings_update.c settings_update_task),
    so the listening socket can go away around any request in this sequence — including one
    belonging to a reboot the test body triggered but that has not landed yet.
    """
    last_error = "no attempt was made"
    for attempt in range(1, attempts + 1):
        # A fresh client per attempt. After a dropped request the previous one may still hold a
        # dead pooled socket, which would fail the retry for the wrong reason.
        client = WBMGEAPI(base_url)
        try:
            auth_resp = client.auth()
            resp = client.update_settings({"web_port": port})
            if resp.status_code != 200:
                last_error = (f"POST /settings -> HTTP {resp.status_code} "
                              f"(POST /auth -> HTTP {auth_resp.status_code})")
            else:
                body = resp.json()
                # A REJECTED settings write answers HTTP 200 with {"success": false}
                # (main/settings_manager.c:849-855, issue #113), so the status code on its own
                # proves nothing — the body is the only thing that says the write took.
                if body.get("success") is True:
                    return None
                last_error = f"POST /settings -> 200 but body reports no success: {body}"
        # Deliberately broad: every failure mode here (connection reset, read timeout, a
        # non-JSON body) is equally retryable, and the caller only needs the description.
        except Exception as exc:  # noqa: BLE001 - see above
            last_error = repr(exc)
        finally:
            client.session.close()
        if attempt < attempts:
            time.sleep(pause_s)
    return f"{attempts} write attempt(s) via {base_url} failed, last: {last_error}"


def _reboot_via(base_url):
    """Send an AUTHENTICATED POST /cmd {"cmd": "reboot"} through base_url.

    Returns None when the reboot fired, otherwise a string describing why it did not.

    Authentication is the whole point of this helper. cmd_post_handler runs
    auth_middleware_check before it looks at the command (main/cmd_handler.c:167), and that
    answers a bare 401 with no body when the request carries no valid session_id cookie
    (main/auth.c:245-267). A plain requests.Session therefore raises nothing, returns a
    perfectly ordinary response object, and reboots nothing at all — a silent no-op. The 401
    is only ever observable by looking at resp.status_code, which is why this function checks
    it and hands the description back to the caller instead of discarding the response.

    A dropped connection ON THE /cmd REQUEST, by contrast, is the SUCCESS path: the firmware
    resets while it is still answering, so the socket dies before the response is flushed. Same
    asymmetry 33_test_auth_settings._reboot_with_session documents. The two requests are
    therefore guarded separately — a connection error on /auth means the reboot was never even
    attempted, and reporting that as "fired" would hide it.
    """
    client = WBMGEAPI(base_url)
    try:
        try:
            auth_resp = client.auth()
        except requests.exceptions.RequestException as exc:
            return f"POST /auth failed, so no reboot was attempted: {exc!r}"
        try:
            resp = client.execute_command("reboot")
        except requests.exceptions.RequestException:
            return None  # device reset before responding -> reboot fired
        if resp.status_code == 200:
            return None
        return (f"POST /cmd -> HTTP {resp.status_code} "
                f"(POST /auth -> HTTP {auth_resp.status_code}); the command was rejected "
                f"before it ran, so no reboot happened")
    except Exception as exc:  # noqa: BLE001 - a failed best-effort reboot is described, not raised
        return repr(exc)
    finally:
        client.session.close()


# Distinguishes "GET /settings never gave us a web_port" from "it gave us the wrong one".
# Conflating the two is how a healthy device ends up reported as TEARDOWN FAILED: /settings
# serialises ~50 keyed NVS reads and can exceed its 30 s read timeout under QEMU flash load
# (api_client.py:76-78), so an unreadable value says nothing about the port at all.
_UNREADABLE = object()


def _read_web_port(api, attempts=_RESTORE_READ_ATTEMPTS, pause_s=_RESTORE_READ_PAUSE_S):
    """Read web_port back through the api fixture, retrying a transient read failure.

    Returns (value, None) once GET /settings answers with a web_port, or
    (_UNREADABLE, description) if it never does.
    """
    last_error = "no attempt was made"
    for attempt in range(1, attempts + 1):
        try:
            resp = api.get_settings()
            if resp.status_code != 200:
                last_error = f"GET /settings -> HTTP {resp.status_code}"
            else:
                body = resp.json()
                if "web_port" in body:
                    return body["web_port"], None
                last_error = "GET /settings -> 200 but the body carries no web_port field"
        # Broad for the same reason as _write_web_port: a read timeout, a reset connection and
        # a non-JSON body are all "we did not learn the value", and all worth one more try.
        except Exception as exc:  # noqa: BLE001 - see above
            last_error = repr(exc)
        if attempt < attempts:
            time.sleep(pause_s)
    return _UNREADABLE, f"{attempts} read attempt(s) failed, last: {last_error}"


def _restore_default_web_port(api, alt_url, default_url, port_wait_s, default_wait_s,
                              allow_reboot):
    """Put web_port back to the firmware default and confirm the device serves it again.

    Returns (None, None) on success, or (description, severity) where severity is "error" for
    a state we know is broken and "warning" for one we simply could not verify.

    Judged by the END STATE, not by the individual requests: the runtime apply can tear the
    listening socket down while it is answering the very POST that requested the change, so a
    write whose response was lost may well have taken effect. A reported write error therefore
    does not short-circuit anything — only the final "does the device serve the default port,
    with the default port in NVS" check decides.
    """
    write_errors = []

    base_url = _wait_for_serving_base([alt_url, default_url], timeout=port_wait_s)
    if base_url is None:
        return ((f"device answered on neither host port {ALT_PORT_HOST} nor {DEFAULT_PORT_HOST} "
                 f"within {port_wait_s} s, so there was nowhere to write web_port through"),
                "error")

    error = _write_web_port(base_url, DEFAULT_PORT_GUEST)
    if error:
        write_errors.append(error)

    if base_url == alt_url and allow_reboot:
        # Belt and braces, not the main path: the runtime apply moves the server back to the
        # default port about a second after the write is answered, so this POST usually races a
        # socket that is already gone and the connection drop is the expected outcome. NVS is
        # written before /settings replies, so a reboot that DOES land comes up on the default
        # port just the same. It has to be an authenticated request to be that safety net at
        # all — see _reboot_via.
        error = _reboot_via(alt_url)
        if error:
            write_errors.append(f"restore reboot via {alt_url}: {error}")

    if not _wait_for_url(f"{default_url}/favicon.webp", timeout=default_wait_s):
        # Second pass. The interesting case is not a slow boot — default_wait_s already covers
        # one — but a write that was REJECTED outright, leaving the device happily serving the
        # alt port forever: writing web_port marks that listener touched, and one touched
        # collision rejects the whole request with {"success": false}
        # (main/settings_manager.c:445-447 + :849-855). Without this pass the restore burns the
        # whole default-port window on a port that will never answer and then gives up with the
        # device still broken. Short window: the device either answers somewhere right now or
        # it is not coming back, and no reboot is sent here, so nothing has to boot.
        retry_base = _wait_for_serving_base([alt_url, default_url],
                                            timeout=_RESTORE_RETRY_PORT_WAIT_S)
        if retry_base is None:
            return ((f"device did not come back on host port {DEFAULT_PORT_HOST} within "
                     f"{default_wait_s} s and then answered on neither host port "
                     f"{ALT_PORT_HOST} nor {DEFAULT_PORT_HOST} within "
                     f"{_RESTORE_RETRY_PORT_WAIT_S} s "
                     f"(write step: {_describe(write_errors)})"), "error")

        error = _write_web_port(retry_base, DEFAULT_PORT_GUEST)
        if error:
            write_errors.append(f"retry via {retry_base}: {error}")

        # No reboot on this pass, deliberately: the runtime apply alone moves the server within
        # about a second of a write that took, and a reboot here would need another full
        # cold-boot window that the test's timeout budget does not have room for.
        if not _wait_for_url(f"{default_url}/favicon.webp",
                             timeout=_RESTORE_RETRY_DEFAULT_WAIT_S):
            return ((f"device did not come back on host port {DEFAULT_PORT_HOST} within "
                     f"{default_wait_s} s, and still had not {_RESTORE_RETRY_DEFAULT_WAIT_S} s "
                     f"after a second write through {retry_base} "
                     f"(write step: {_describe(write_errors)})"), "error")

    # reconnect() + retried auth(), not a bare auth(): a reboot leaves the fixture session
    # holding dead keep-alive sockets, so one POST would fail on a stale connection.
    try:
        api.wait_for_ready(timeout=_RESTORE_REARM_S)
    except Exception as exc:  # noqa: BLE001 - reported, not swallowed
        return ((f"device answers on host port {DEFAULT_PORT_HOST} but the api fixture could "
                 f"not re-authenticate within {_RESTORE_REARM_S} s: {exc!r}"), "error")

    readback, read_error = _read_web_port(api)
    if readback == DEFAULT_PORT_GUEST:
        return None, None

    # Serving the default port while NVS says otherwise is a real firmware state, not a
    # theoretical one: http_server_acquire() falls back to the default port when it cannot bind
    # the configured one (main/settings_update.c:187-192). It looks healthy right up until the
    # next test reboots the device and it comes back on the alt port. Rewrite through the port
    # that is serving now — nothing has to be handed over, so this needs no restart. Fewer
    # attempts than the writes above: the device has just answered GET /info and is not in the
    # middle of a socket handover, so a retry storm here would only spend budget.
    error = _write_web_port(default_url, DEFAULT_PORT_GUEST, attempts=_RESTORE_REWRITE_ATTEMPTS)
    if error:
        write_errors.append(f"rewrite via {default_url}: {error}")

    readback, read_error = _read_web_port(api)
    if readback == DEFAULT_PORT_GUEST:
        return None, None

    if readback is _UNREADABLE:
        # Not an error: every observable thing about the device is fine — it serves the default
        # port and it authenticates — we just could not get the one field that would prove NVS
        # agrees. Failing the run on that would turn two slow GET /settings into a red build.
        return ((f"device answers on host port {DEFAULT_PORT_HOST} and authenticates, but "
                 f"web_port could not be read back to confirm it is {DEFAULT_PORT_GUEST} again "
                 f"({read_error}); write step: {_describe(write_errors)}"), "warning")

    return ((f"device answers on host port {DEFAULT_PORT_HOST} but GET /settings reports "
             f"web_port={readback!r} instead of {DEFAULT_PORT_GUEST}; the next reboot would "
             f"move it off the default port again "
             f"(write step: {_describe(write_errors)})"), "error")


def _describe(write_errors):
    """Render the accumulated write/reboot failures, or say there were none."""
    return "; ".join(write_errors) if write_errors else "every write reported success"


@pytest.mark.timeout(_TEST_TIMEOUT_S)
def test_web_port_change(api, is_qemu):
    """web_port setting: after changing port and rebooting, server must be reachable on the new port only."""
    if not is_qemu:
        pytest.skip("Requires QEMU (hostfwd for alt port 8081)")

    from urllib.parse import urlparse
    host = urlparse(api.base_url).hostname or "localhost"

    default_url = f"http://{host}:{DEFAULT_PORT_HOST}"
    alt_url = f"http://{host}:{ALT_PORT_HOST}"

    # Precondition: server is reachable on the default port
    assert _is_reachable(default_url), \
        f"Server not reachable on default host port {DEFAULT_PORT_HOST} before test"

    try:
        # Change web_port and reboot
        resp = api.update_settings({"web_port": ALT_PORT_GUEST})
        assert resp.status_code == 200, f"POST /settings failed: {resp.status_code}"
        assert resp.json().get("success") is True, f"Settings update not successful: {resp.json()}"

        try:
            api.execute_command("reboot")
        except Exception:
            # Expected either way: the device resets while answering, or the runtime apply has
            # already moved the server off the default port before this POST is sent.
            pass

        # Server must come up on the new port
        ready = _wait_for_url(f"{alt_url}/favicon.webp", timeout=_COLD_BOOT_WAIT_S)
        assert ready, (
            f"Server did not come up on alt host port {ALT_PORT_HOST} within "
            f"{_COLD_BOOT_WAIT_S} s after setting web_port={ALT_PORT_GUEST} and rebooting"
        )

        # Old default port must be gone
        assert not _is_reachable(default_url, timeout=_REACHABLE_TIMEOUT_S), (
            f"Server still responds on default host port {DEFAULT_PORT_HOST} "
            f"after switching to web_port={ALT_PORT_GUEST}"
        )

    finally:
        # This teardown is load-bearing for the whole session, not cosmetic. QEMU forwards host
        # DEFAULT_PORT_HOST to GUEST port 80, so a device left on the alt port is unreachable
        # for every test that runs after this one — and the @pytest.mark.reboot marker puts this
        # test late in the run with several files still to come. They see a bare
        # ConnectionResetError(104) with nothing pointing back here. That is not hypothetical:
        # one swallowed RemoteDisconnected in this block reported PASSED and took out 10
        # downstream tests. Hence retries, an end-state check, and a raise instead of a print.
        body_failed = sys.exc_info()[0] is not None

        # The whole call is wrapped, not just its expected failure mode. _restore_default_web_port
        # returns a string for everything it anticipates, but plenty inside it is not anticipated:
        # _is_reachable only catches RequestException, and the WBMGEAPI() constructions and
        # session.close() calls sit outside their own try blocks. Any of those escaping would
        # sail straight past the body_failed check below and replace the primary exception —
        # precisely what 33_test_auth_settings.py:144-149 spells out must never happen from a
        # finally block. Folded into `problem` instead, so it is reported the same way.
        default_wait_s = _RESTORE_FAILFAST_WAIT_S if body_failed else _RESTORE_DEFAULT_WAIT_S
        try:
            problem, severity = _restore_default_web_port(
                api, alt_url, default_url,
                port_wait_s=_RESTORE_PORT_WAIT_S,
                default_wait_s=default_wait_s,
                # The shortened wait and the suppressed reboot are ONE decision, not two. A
                # reboot needs a full cold-boot window to wait out, and the failfast path
                # deliberately does not have one: allowing it there would reboot the device,
                # give up while it is still booting, and hand a mid-boot device to the next
                # tests — most of which run pytest.ini's 180 s budget and do not call
                # wait_for_ready() on entry. The write alone still restores the port via the
                # runtime apply (settings_update_task); the reboot is only ever the backstop.
                allow_reboot=not body_failed,
            )
        except Exception as exc:  # noqa: BLE001 - see above; breadth is the point
            problem, severity = f"the restore itself raised {exc!r}", "error"

        if problem:
            message = (
                f"TEARDOWN {'FAILED' if severity == 'error' else 'UNCONFIRMED'}: web_port was "
                f"not confirmed back at {DEFAULT_PORT_GUEST} — {problem}. Every later test in "
                f"this session talks to host port {DEFAULT_PORT_HOST} and will fail with a "
                f"connection error if the device is not back on the default port."
            )
            if body_failed or severity == "warning":
                # Raising here would replace the exception the body is already propagating and
                # demote the real cause to __context__. Warn instead: pytest lists warnings in
                # the run summary (pytest.ini sets no filterwarnings=error), so the teardown
                # failure stays visible while the primary failure keeps its place in the report.
                # A "warning" severity warns even on a passing body: the device looks healthy and
                # only the confirming readback was unavailable.
                warnings.warn(message, stacklevel=2)
            else:
                raise RuntimeError(message)
