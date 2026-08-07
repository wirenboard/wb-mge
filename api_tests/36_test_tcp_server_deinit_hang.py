"""
Regression test for tcp_server_deinit() hang with an open client connection.

Bug fixed: tcp_server_deinit() in main/bridge/tcp_server.c could hang
indefinitely when a TCP client connection was still open (i.e., the
receiver_task was blocked in recv() with no timeout). Fixed by adding
SO_RCVTIMEO = 100 ms to each accepted client socket and handling
EAGAIN/EWOULDBLOCK in receiver_task to check EVENT_TASK_EXIT_REQ.

Concrete reproduction: the test_gateway_dual_port_simultaneous teardown
called api.set_port_mode(2, "disabled") while a TCP client socket was still
open (Python test closed gw_sock2 only moments before teardown, and QEMU's
slirp networking hadn't propagated the FIN yet). tcp_server_deinit() hung
indefinitely waiting for active_connections == 0.

Run this test alone:
  pytest api_tests/36_test_tcp_server_deinit_hang.py --qemu --qemu-skip-build -s
"""
import time

import pytest

from conftest import _connect_ready_bridge


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    # Set rs485_1 to a known state: tx_disabled=False, 9600 baud.
    # Bridge settings are applied inside the test itself.
    resp = api.update_settings({
        "rs485_1": {
            "tx_disabled": False,
            "baudrate": 9600,
            "stopbits": "1",
            "parity": "none",
            "databits": "8",
        }
    })
    assert resp.status_code == 200, (
        f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    )


@pytest.mark.qemu
# 150 s, not 60 s: an item's pytest-timeout budget covers setup + call + TEARDOWN, and this
# is the module's ONLY item — so it pays both ends on top of its own body. Setup: the
# module-scoped _baseline (:27, one POST /settings) plus conftest's once-per-session rs485
# snapshot (one bounded GET /settings, 20.1 s, see _RS485_HTTP_TIMEOUT) when this file is
# run on its own — in a full-suite run that lands on the very first item of the session
# instead. Teardown: conftest's _restore_rs485_settings, up to two bounded POST /settings
# plus a settle window (2 x 20.1 s + 1 s = 41.2 s).
# 60 s body + 45 s setup allowance + 45 s teardown allowance. The body's admission poll
# is bounded ~15 s, not 5 s: its 5 s wall-clock deadline is checked between iterations, and
# a single api.get_info() can itself block up to its 10 s client timeout — well inside 60 s.
@pytest.mark.timeout(150)
def test_tcp_server_deinit_completes_with_open_client(api):
    """
    tcp_server_deinit() must return quickly even when a TCP client connection
    is still open.

    Coverage: BUG-FIX-tcp_server_deinit_hang

    Before the fix, tcp_server_deinit() waited forever for active_connections
    to drop to zero. The receiver_task was blocked in recv() with no timeout,
    so it never checked EVENT_TASK_EXIT_REQ and never decremented the counter.

    This test opens a TCP connection to the gateway port and does NOT close it
    before calling set_port_mode(1, "disabled"). The call must complete within
    3 seconds; if it does not, the fix is missing (or regressed).
    """
    # Step 1: save original settings so we can restore them in the finally block.
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
    original_settings = resp.json()
    original_mode = original_settings.get("rs485_1", {}).get("port_mode", "disabled")

    client_sock = None
    try:
        # Step 2: disable port 1 first to release any existing UART/bridge driver.
        resp = api.set_port_mode(1, "disabled")
        assert resp.status_code == 200, (
            f"set_port_mode(1, disabled) failed: {resp.status_code}"
        )
        time.sleep(0.3)

        # Step 3: configure port 1 as a transparent TCP server on port 50504.
        resp = api.update_settings({
            "rs485_1": {
                "bridge": {
                    "mode": "server",
                    "port": 50504,
                    "ip": "0.0.0.0",
                    "modbus": False,
                }
            }
        })
        assert resp.status_code == 200, (
            f"update_settings (bridge config) failed: {resp.status_code} {resp.text}"
        )
        result = resp.json()
        assert result.get("success") is True, (
            f"update_settings (bridge config) not successful: {result}"
        )

        # Step 4: switch port 1 to tcp_bridge mode.
        resp = api.set_port_mode(1, "tcp_bridge")
        assert resp.status_code == 200, (
            f"set_port_mode(1, tcp_bridge) failed: {resp.status_code}"
        )

        # Step 5+6: open a TCP client connection that is GENUINELY ADMITTED by the
        # bridge, and intentionally keep it open. This is the whole point of the
        # test: deinit must not hang while a real client is connected.
        #
        # _poll_tcp_connect() was wrong here twice over: (1) against the QEMU slirp
        # hostfwd port it returns True instantly without the guest admitting
        # anything, and (2) its probe occupies the single server slot
        # (max_connections == 1), so under load the client_sock below could be
        # rejected by the cap — a rejection the test never notices because it does
        # not read client_sock, leaving active_connections == 0 so deinit returns
        # instantly and the assert passes VACUOUSLY, exercising no regression.
        # _connect_ready_bridge() returns a connection that was not immediately
        # rejected (no FIN/RST within its hold window). But that admission check is
        # NEGATIVE, and a backlog>1 listen can leave a connection queued-but-unserved,
        # so we POSITIVELY confirm the firmware actually admitted exactly one client
        # before deinit — otherwise this test could pass vacuously (active_connections
        # == 0 -> instant deinit -> no regression exercised).
        client_sock = _connect_ready_bridge("127.0.0.1", 50504, timeout=15.0)
        client_sock.settimeout(5.0)

        # Poll (not a bare assert): accept() can lag the TCP handshake under load, so
        # give the firmware a few seconds to register the client. If it never reaches
        # exactly one, the client was not truly admitted and the deinit-with-open-client
        # regression would not be exercised — fail rather than pass vacuously.
        conns = None
        conn_deadline = time.monotonic() + 5.0
        while time.monotonic() < conn_deadline:
            info = api.get_info()
            if info.status_code == 200:
                conns = info.json().get("rs485_1", {}).get("server_connections_count")
                if conns == 1:
                    break
            time.sleep(0.2)
        assert conns == 1, (
            f"firmware must see exactly one admitted transparent client before deinit, "
            f"got server_connections_count={conns!r} within 5 s"
        )

        # Step 7: record the start time immediately before the deinit call.
        t_start = time.monotonic()

        # Step 8: disable the port while the client connection is still open.
        # Without the fix this call hangs indefinitely (30 s+ until pytest timeout).
        resp = api.set_port_mode(1, "disabled")

        # Step 9: measure elapsed time.
        elapsed = time.monotonic() - t_start

        # Step 10: assert the API returned success.
        assert resp.status_code == 200, (
            f"set_port_mode disabled failed: {resp.status_code}"
        )

        # Step 11: assert the call completed quickly — this is the core regression check.
        assert elapsed < 3.0, (
            f"tcp_server_deinit hung with open client: took {elapsed:.2f}s (expected < 3s)"
        )

    finally:
        # Close the client socket if still open.
        if client_sock is not None:
            try:
                client_sock.close()
            except Exception:
                pass

        # Ensure the port is disabled before restoring settings.
        try:
            api.set_port_mode(1, "disabled")
        except Exception:
            pass

        # Restore original settings.
        try:
            api.update_settings(original_settings)
        except Exception:
            pass

        # Restore original port mode.
        try:
            api.set_port_mode(1, original_mode)
        except Exception:
            pass
