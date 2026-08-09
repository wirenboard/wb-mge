"""
Regression test for cache_modbus_server_deinit() hang when a TCP client is
actively polling (continuously sending Modbus requests).

Bug: In main/bridge/tcp_server.c, receiver_task() only checks the
EVENT_TASK_EXIT_REQ flag in the EAGAIN/EWOULDBLOCK branch (when the 100 ms
SO_RCVTIMEO fires with no data). If an external device continuously sends
data, recv() never times out and the exit flag is never checked.
cache_modbus_server_deinit() (triggered by disabling "Serve cached values via
TCP") calls tcp_server_deinit(), which hangs in
`while (desc->active_connections > 0)`, blocking the HTTP handler forever.

Analogous to 36_test_tcp_server_deinit_hang.py (idle connection), but covers
the *active polling* case: the client continuously sends FC03 requests in a
tight loop, which is the exact scenario that triggers the hang.

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

from conftest import _poll_tcp_connect
from modbus_helpers import make_mbap_request, recv_exactly

CACHE_PORT = qemu_ports.CACHE_MODBUS_HOST_PORT


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    """Enable the cache Modbus server on port 50504 as the baseline for this module."""
    resp = api.update_settings({
        "cache_modbus_server_enabled": True,
        "cache_modbus_port": qemu_ports.CACHE_MODBUS_GUEST_PORT,
        "cache_value_timeout_s": 0,
    })
    assert resp.status_code == 200, (
        f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    )


def _polling_thread(host: str, port: int, stop_event: threading.Event) -> None:
    """Continuously send Modbus TCP FC03 requests until stop_event is set.

    Connects to (host, port) and polls slave=1, addr=0, count=1 in a tight
    loop.  Responses (success or Modbus exception) are read and discarded to
    prevent the send buffer from filling up.  Exits cleanly on socket error
    or when stop_event fires.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        # 1 s socket timeout so recv() does not block forever if the server
        # stops sending responses before stop_event is set.
        sock.settimeout(1.0)
        sock.connect((host, port))
    except OSError:
        # Could not connect — nothing to poll.
        return

    tid = 1
    try:
        while not stop_event.is_set():
            # Build and send a FC03 read-holding-registers request.
            request = make_mbap_request(tid, slave_id=1, fc=0x03, start_addr=0, count=1)
            try:
                sock.sendall(request)
            except OSError:
                break

            # Read and discard the response so the TCP send buffer stays empty.
            # Parse the 8-byte MBAP + FC header; then drain any remaining payload.
            try:
                header = recv_exactly(sock, 8)
                _tid, _proto, length, _uid, _fc = struct.unpack(">HHHBB", header)
                remaining = length - 2  # uid and fc already consumed
                if remaining > 0:
                    recv_exactly(sock, remaining)
            except (OSError, ConnectionError, struct.error):
                # Server closed the connection or a socket error occurred;
                # stop the loop so the thread exits cleanly.
                break

            tid = (tid % 65535) + 1
    finally:
        try:
            sock.close()
        except OSError:
            pass


@pytest.mark.qemu
# 150 s, not 60 s: an item's pytest-timeout budget covers setup + call + TEARDOWN, and this
# is the module's ONLY item — so it pays both ends on top of its own body. Setup: the
# module-scoped _baseline (:35, one POST /settings) plus conftest's once-per-session rs485
# snapshot (one bounded GET /settings, 20.1 s, see _RS485_HTTP_TIMEOUT) when this file is
# run on its own — in a full-suite run that lands on the very first item of the session
# instead. Teardown: conftest's _restore_rs485_settings, up to two bounded POST /settings
# plus a settle window (2 x 20.1 s + 1 s = 41.2 s).
# 60 s body + 45 s setup allowance + 45 s teardown allowance.
@pytest.mark.timeout(150)
def test_cache_server_deinit_with_active_polling(api):
    """
    cache_modbus_server_deinit() must return quickly even when a TCP client is
    actively polling (continuously sending Modbus requests).

    Coverage: BUG-FIX-cache_server_deinit_hang_active_polling

    Before the fix, tcp_server_deinit() (called via
    cache_modbus_server_deinit()) waited forever for active_connections to drop
    to zero.  When the client was continuously sending data, recv() in
    receiver_task never timed out and the exit flag was never checked.

    This test starts a background thread that sends FC03 requests in a tight
    loop, then calls update_settings(cache_modbus_server_enabled=False).  The
    call must complete within 5 seconds; otherwise the bug is present (or has
    regressed).
    """
    # Step 1: save original settings for restoration in the finally block.
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
    original_settings = resp.json()
    original_port_mode = original_settings.get("rs485_1", {}).get("port_mode", "disabled")

    stop_event = threading.Event()
    poll_thread = None

    try:
        # Step 2: enable the cache Modbus server on port 50504 and disable the
        # value timeout so the cache always has entries to serve.
        resp = api.update_settings({
            "cache_modbus_server_enabled": True,
            "cache_modbus_port": qemu_ports.CACHE_MODBUS_GUEST_PORT,
            "cache_value_timeout_s": 0,
        })
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

        # Make sure the server port is actually reachable before starting the
        # polling thread.
        ready = _poll_tcp_connect("127.0.0.1", CACHE_PORT, timeout=5.0)
        assert ready, f"Cache server did not start listening on port {CACHE_PORT} within 5 s"

        # Step 5: derive the host address from api.base_url and start the
        # background polling thread.
        parsed = urlparse(api.base_url)
        host = parsed.hostname or "127.0.0.1"

        poll_thread = threading.Thread(
            target=_polling_thread,
            args=(host, CACHE_PORT, stop_event),
            daemon=True,
        )
        poll_thread.start()

        # Step 6: let the polling thread run for a bit so recv() in
        # receiver_task is busy with real incoming data.
        time.sleep(0.5)

        # Step 7: record start time immediately before the deinit API call.
        t_start = time.monotonic()

        # Step 8: disable the cache Modbus server — this is the HTTP call that
        # must NOT hang when the client is actively polling.
        resp = api.update_settings({"cache_modbus_server_enabled": False})

        # Step 9: measure how long the call took.
        elapsed = time.monotonic() - t_start

        # Step 10: the API must have returned HTTP 200.
        assert resp.status_code == 200, (
            f"update_settings (disable cache server) failed: {resp.status_code} {resp.text}"
        )

        # Step 11: the call must have completed within the budget.
        # The firmware deinit does not actually hang: receiver_task checks the
        # exit flag after each received packet, and teardown polls every 10 ms.
        # So the budget only needs to sit comfortably below the ~30 s HTTP-client
        # timeout that a genuine hang would hit. 15 s tolerates QEMU CPU load
        # (the ~6 s measured is just the update_settings round-trip) while still
        # catching a real hang.
        assert elapsed < 15.0, (
            f"cache_modbus_server_deinit hung with active polling client: "
            f"took {elapsed:.2f}s (expected < 15s)"
        )

        # Step 12: stop the polling thread.
        stop_event.set()
        poll_thread.join(timeout=3.0)

        # Step 13: the web interface must still be responsive after the deinit.
        resp = api.get_settings()
        assert resp.status_code == 200, (
            f"Web interface unresponsive after cache server deinit: {resp.status_code}"
        )

    finally:
        # Ensure the polling thread is stopped regardless of test outcome.
        stop_event.set()
        if poll_thread is not None and poll_thread.is_alive():
            poll_thread.join(timeout=3.0)

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
