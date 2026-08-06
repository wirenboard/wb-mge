"""web_port setting test — verifies that the HTTP server starts on the configured port after reboot.

Requires QEMU with hostfwd 8081->8081 (see conftest.py).
Skip on real hardware: no hostfwd guarantee exists there.
"""

import sys
import time
import warnings

import pytest
import requests

from api_client import WBMGEAPI


# Port inside the firmware (NVS key web_port)
ALT_PORT_GUEST = 8081
# Corresponding host-side QEMU hostfwd port (127.0.0.1:8081 -> guest:8081)
ALT_PORT_HOST = 8081
# Default host-side port for the default web port mapping (127.0.0.1:8080 -> guest:80)
DEFAULT_PORT_HOST = 8080
# Firmware-side default web port, i.e. what the hostfwd above maps onto
# (HTTP_SERVER_DEFAULT_PORT in main/http_server.h).
DEFAULT_PORT_GUEST = 80

# ── Teardown windows ─────────────────────────────────────────────────────────────────────────
# Deliberately tighter than the 1800 s cold-boot window the test body uses: by teardown time
# the device has already answered at least once, so these only have to cover the TAIL of a
# reboot, not a boot from scratch.
_RESTORE_PORT_WAIT_S = 300      # find a port the device is serving on before writing through it
_RESTORE_DEFAULT_WAIT_S = 1800  # device back on the default port (a full reboot may be in flight)
_RESTORE_REARM_S = 60           # api fixture reconnect + re-auth once the port already answers
_RESTORE_ATTEMPTS = 4           # POST /auth + POST /settings tries per port
_RESTORE_ATTEMPT_PAUSE_S = 2

# Used for BOTH waits when the test body already failed. The device is then probably wedged and
# the full windows above would burn half an hour before the report reaches the real failure —
# the same trade 33_test_auth_settings._rearm_api_session documents.
_RESTORE_FAILFAST_WAIT_S = 300


def _is_reachable(url, timeout=5):
    """Returns True if url returns any HTTP response within timeout seconds."""
    try:
        requests.get(url, timeout=timeout)
        return True
    except requests.exceptions.RequestException:
        return False


def _wait_for_url(url, timeout=1800, probe_timeout=2, interval=1):
    """Poll url until an HTTP response is received or timeout expires. Returns True on success."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if _is_reachable(url, timeout=probe_timeout):
            return True
        time.sleep(interval)
    return False


def _wait_for_serving_base(base_urls, timeout, probe_timeout=2, interval=1):
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


def _read_web_port(api):
    """Read web_port back through the api fixture. Returns the value, or None if unreadable."""
    try:
        resp = api.get_settings()
        if resp.status_code != 200:
            return None
        return resp.json().get("web_port")
    except Exception:  # noqa: BLE001 - unreadable is unreadable, whatever the reason
        return None


def _restore_default_web_port(api, alt_url, default_url, port_wait_s, default_wait_s):
    """Put web_port back to the firmware default and confirm the device serves it again.

    Returns None on success, or a string describing what is still wrong.

    Judged by the END STATE, not by the individual requests: the runtime apply can tear the
    listening socket down while it is answering the very POST that requested the change, so a
    write whose response was lost may well have taken effect. A reported write error therefore
    does not short-circuit anything — only the final "does the device serve the default port,
    with the default port in NVS" check decides.
    """
    base_url = _wait_for_serving_base([alt_url, default_url], timeout=port_wait_s)
    if base_url is None:
        return (f"device answered on neither host port {ALT_PORT_HOST} nor {DEFAULT_PORT_HOST} "
                f"within {port_wait_s} s, so there was nowhere to write web_port through")

    write_error = _write_web_port(base_url, DEFAULT_PORT_GUEST)

    if base_url == alt_url:
        # Best effort only, and a failure here is not an error. The runtime apply moves the
        # server back to the default port about a second after the write is answered, so this
        # POST usually races a socket that is already gone. NVS is written before /settings
        # replies, so a reboot that DOES land comes up on the default port just the same.
        try:
            reboot = requests.Session()
            reboot.post(f"{alt_url}/cmd", json={"cmd": "reboot"}, timeout=30)
            reboot.close()
        except Exception:  # noqa: BLE001 - connection drop during reboot is expected
            pass

    if not _wait_for_url(f"{default_url}/favicon.webp", timeout=default_wait_s):
        return (f"device did not come back on host port {DEFAULT_PORT_HOST} within "
                f"{default_wait_s} s (write step: {write_error or 'reported success'})")

    # reconnect() + retried auth(), not a bare auth(): a reboot leaves the fixture session
    # holding dead keep-alive sockets, so one POST would fail on a stale connection.
    try:
        api.wait_for_ready(timeout=_RESTORE_REARM_S)
    except Exception as exc:  # noqa: BLE001 - reported, not swallowed
        return (f"device answers on host port {DEFAULT_PORT_HOST} but the api fixture could not "
                f"re-authenticate within {_RESTORE_REARM_S} s: {exc!r}")

    readback = _read_web_port(api)
    if readback == DEFAULT_PORT_GUEST:
        return None

    # Serving the default port while NVS says otherwise is a real firmware state, not a
    # theoretical one: http_server_acquire() falls back to the default port when it cannot bind
    # the configured one (main/settings_update.c:187-192). It looks healthy right up until the
    # next test reboots the device and it comes back on the alt port. Rewrite through the port
    # that is serving now — nothing has to be handed over, so this needs no restart.
    write_error = _write_web_port(default_url, DEFAULT_PORT_GUEST) or write_error
    readback = _read_web_port(api)
    if readback == DEFAULT_PORT_GUEST:
        return None

    return (f"device answers on host port {DEFAULT_PORT_HOST} but GET /settings reports "
            f"web_port={readback!r} (None = unreadable) instead of {DEFAULT_PORT_GUEST}; the "
            f"next reboot would move it off the default port again "
            f"(write step: {write_error or 'reported success'})")


@pytest.mark.timeout(2700)
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
        ready = _wait_for_url(f"{alt_url}/favicon.webp", timeout=1800)
        assert ready, (
            f"Server did not come up on alt host port {ALT_PORT_HOST} within 1800s "
            f"after setting web_port={ALT_PORT_GUEST} and rebooting"
        )

        # Old default port must be gone
        assert not _is_reachable(default_url, timeout=5), (
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

        problem = _restore_default_web_port(
            api, alt_url, default_url,
            port_wait_s=_RESTORE_FAILFAST_WAIT_S if body_failed else _RESTORE_PORT_WAIT_S,
            default_wait_s=_RESTORE_FAILFAST_WAIT_S if body_failed else _RESTORE_DEFAULT_WAIT_S,
        )
        if problem:
            message = (
                f"TEARDOWN FAILED: web_port was not restored to {DEFAULT_PORT_GUEST} — {problem}. "
                f"Every later test in this session talks to host port {DEFAULT_PORT_HOST} and "
                f"will fail with a connection error until the device is back on the default port."
            )
            if body_failed:
                # Raising here would replace the exception the body is already propagating and
                # demote the real cause to __context__. Warn instead: pytest lists warnings in
                # the run summary (pytest.ini sets no filterwarnings=error), so the teardown
                # failure stays visible while the primary failure keeps its place in the report.
                warnings.warn(message, stacklevel=2)
            else:
                raise RuntimeError(message)
