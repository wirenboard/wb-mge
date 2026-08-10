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
import qemu_ports


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
# pytest-timeout budget, raised above pytest.ini's `timeout = 180` (150 -> 600) and
# deliberately GENEROUS rather than derived.
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
# because a wedged device has already broken the run, and this is 1 item out of 229.
# Reality check: a green run of this item was MEASURED at 17.5 s standalone, and most of
# that is the QEMU boot its setup happens to pay for.
@pytest.mark.timeout(600)
def test_tcp_server_deinit_completes_with_open_client(api):
    """
    tcp_server_deinit() must return quickly even when a TCP client connection
    is still open.

    Coverage: BUG-FIX-tcp_server_deinit_hang

    Before the fix, tcp_server_deinit() waited forever for active_connections
    to drop to zero. The receiver_task was blocked in recv() with no timeout,
    so it never checked EVENT_TASK_EXIT_REQ and never decremented the counter.

    This test opens a TCP connection to the gateway port and does NOT close it
    before calling set_port_mode(1, "disabled"). A missing fix shows up as a
    requests.ReadTimeout on that call (the HTTP client's 30 s timeout); the
    wall-clock assert in step 11 additionally flags a call that answers but takes
    pathologically longer than the same mode switch without a client. See the
    comment at that assert for why the budget is relative and not a constant.
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

        # Step 3: configure port 1 as a transparent TCP server on the GUEST bridge port
        # (fixed guest 50504 — what this slot's hostfwd forwards to); the client below connects to the
        # dynamic host port that reaches it.
        resp = api.update_settings({
            "rs485_1": {
                "bridge": {
                    "mode": "server",
                    "port": qemu_ports.TRANSPARENT_P1_GUEST_PORT,
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
        #
        # This call is ALSO the control measurement for the budget in step 11: the same
        # API operation (set_port_mode) on the same port, on this machine, in this run —
        # but with no client socket open. Timing it costs nothing (no extra API call) and
        # gives step 11 a per-machine reference instead of a hand-picked constant.
        t_control = time.monotonic()
        resp = api.set_port_mode(1, "tcp_bridge")
        control_elapsed = time.monotonic() - t_control
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
        client_sock = _connect_ready_bridge(
            qemu_ports.GATEWAY_HOST, qemu_ports.TRANSPARENT_HOST_PORT, timeout=15.0)
        client_sock.settimeout(5.0)

        # Poll (not a bare assert): accept() can lag the TCP handshake under load, so
        # give the firmware a few seconds to register the client. If it never reaches
        # exactly one, the client was not truly admitted and the deinit-with-open-client
        # regression would not be exercised — fail rather than pass vacuously.
        conns = None
        poll_start = time.monotonic()
        conn_deadline = poll_start + 5.0
        while time.monotonic() < conn_deadline:
            info = api.get_info()
            if info.status_code == 200:
                conns = info.json().get("rs485_1", {}).get("server_connections_count")
                if conns == 1:
                    break
            time.sleep(0.2)
        poll_elapsed = time.monotonic() - poll_start
        # Wall-clock deadline is 5 s, but a single api.get_info() can block up to its 10 s
        # client timeout, so print the ACTUAL time waited rather than a fixed "within 5 s".
        assert conns == 1, (
            f"firmware must see exactly one admitted transparent client before deinit, "
            f"got server_connections_count={conns!r} after polling {poll_elapsed:.1f} s"
        )

        # Step 7: record the start time immediately before the deinit call.
        t_start = time.monotonic()

        # Step 8: disable the port while the client connection is still open.
        # Without the fix the handler never answers, and this call ends as a
        # requests.ReadTimeout at api_client.set_port_mode()'s 30 s ceiling — NOT at the
        # pytest-timeout marker, which is a backstop sized well above it (see the marker).
        resp = api.set_port_mode(1, "disabled")

        # Step 9: measure elapsed time.
        elapsed = time.monotonic() - t_start

        # Step 10: assert the API returned success.
        assert resp.status_code == 200, (
            f"set_port_mode disabled failed: {resp.status_code}"
        )

        # Step 11: assert the call did not take pathologically longer than the same
        # operation without a client.
        #
        # What actually detects the guarded bug is NOT this assert. The bug is an
        # UNBOUNDED wait in tcp_server_deinit() for active_connections to reach zero, and
        # api_client.set_port_mode() posts with timeout=30 — so a genuine hang surfaces as
        # a requests.ReadTimeout out of step 8 and never reaches this line at all. Hang
        # detection is owned by the HTTP client timeout; this assert cannot see it.
        #
        # That claim holds HERE because the handler is synchronous: port_set_mode_handler()
        # calls port_manager_set_mode() and only reaches json_utils_send_response()
        # afterwards (main/bridge/port_manager.c:1502-1538), so the deinit runs INSIDE the
        # request and a hang holds the response. Do not copy the claim to a test whose
        # trigger is POST /settings — there the deinit was moved into an async task by
        # 021b92963, the 200 is sent before it starts, and no client timeout can see the
        # hang. That is exactly how the sibling test 37_ stopped detecting its own
        # regression; it now uses a second, blocking POST as its detector instead.
        #
        # What is left for this assert is only "the call answered, but pathologically
        # slower than the same call without a client". That comparison is meaningless
        # against a constant: the shared CI node is far slower than free hardware — the
        # full suite takes ~124 min there against ~35 min on free hardware, measured across
        # builds #17/#18/#19 — so a wall-clock budget calibrated on free hardware flakes
        # there. That is exactly how the sibling test 37_ failed CI build #19 (15.88s
        # against its 15 s constant) while #17 and #18 passed on the same commit. So the
        # budget is a ratio over the CONTROL measurement from step 4 — the same
        # set_port_mode, on the same port, on the same machine, in the same run, without a
        # client socket. (No second, tighter figure is derived from build #19's duration
        # here: doing so would assume the ratio invariance that is the very thing being
        # argued for.)
        #
        # The control is a reference for MACHINE SPEED, not an identical workload: step 4
        # switches INTO tcp_bridge (serial + bridge init) while step 8 switches OUT of it
        # (the deinit under test). What they share — the HTTP round trip and a full port
        # reconfigure on the same emulated hardware — is what dominates both, which is
        # exactly the per-machine cost the old constant failed to track.
        #
        # The old constant is kept as a FLOOR: behaviour on fast hardware is unchanged,
        # and a near-zero control cannot collapse the bound to something absurdly tight.
        #
        # RATIO measured, not guessed. Five runs on a free machine (macOS, QEMU), each
        # from a freshly rebuilt flash image:
        #   control  0.39 / 0.39 / 0.42 / 0.60 / 0.38 s
        #   measured 0.65 / 0.38 / 0.27 / 0.49 / 0.38 s
        #   measured/control  1.67 / 0.97 / 0.64 / 0.82 / 1.00   (worst 1.67)
        # RATIO 4.0 leaves ~2.4x headroom over the worst observed ratio while still
        # flagging a deinit costing several times a client-less mode switch. Note the
        # crossover: the ratio only overtakes the floor once control_elapsed > 0.75 s,
        # i.e. on a machine slow enough that the 3 s constant was heading for a flake.
        # This relies on control and measured scaling TOGETHER with machine speed, which
        # is what the runs above show — their absolute times move while their ratio stays
        # in a narrow band.
        #
        # Known limitations, accepted on purpose.
        #
        # (a) The control contends with step 3's async work. Step 3's update_settings()
        # changes rs485_1.bridge, so port_manager_check_settings_changed(0) is true and
        # settings_update_with_status() spawns settings_update_task
        # (main/settings_update.c:406-448), which asynchronously runs port_manager_release(0)
        # then port_manager_apply_settings(0). Step 4's set_port_mode() is synchronous and
        # takes the same pm_lock(0), so the two can serialise against each other and inflate
        # control_elapsed. That direction is SAFE — a larger control loosens the bound, so
        # the worst outcome is a false PASS, never a false FAIL — but it is real
        # order-dependent variance in the reference, exactly like the caveat the sibling
        # test 37_ records for its own control. Do not tighten RATIO on the assumption that
        # the control is a clean measurement.
        #
        # (b) Both calls are set_port_mode, so both carry api_client's ESP_FAIL retry
        # (api_client.py:152-162: up to 2 extra POSTs, 30 s each, 0.5 s apart). It is
        # UNREACHABLE today: port_set_mode_handler() answers a port_manager_set_mode()
        # failure with 500, not 400 (main/bridge/port_manager.c:1516-1529), and its only
        # 400s ("Invalid JSON", "Missing or invalid 'mode' field", "Unknown mode value")
        # never carry the string ESP_FAIL. api_client.set_port_mode should be reconciled
        # with that 500 — out of scope for this change.
        #
        # Neither limitation is worth guarding, because hang detection does not depend on
        # this assert at all: it is owned by the client timeout on step 8.
        bound = max(3.0, control_elapsed * 4.0)
        print(
            f"deinit with open client: {elapsed:.2f}s, "
            f"control (same op, no client): {control_elapsed:.2f}s, bound: {bound:.2f}s"
        )
        assert elapsed < bound, (
            f"tcp_server_deinit hung with open client: took {elapsed:.2f}s, "
            f"control (same set_port_mode, no client) took {control_elapsed:.2f}s, "
            f"bound {bound:.2f}s"
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
