"""
Regression test for bug 09: sniffer writes WS frames to a stale/recycled fd.

When a WS client TCP-closes without sending a WS close frame, the saved
ws_client_fd can be reused by httpd for a subsequent plain-HTTP connection.
Without the fix, sniffer_ws_task would write WS bytes into the HTTP response
stream, causing the HTTP client to receive garbage (BadStatusLine).

The fix: verify fd is still a WebSocket via httpd_ws_get_fd_info before sending.

Reproduction:
1. Connect WS sniffer client, start receiving packets
2. Abruptly close TCP (ws.sock.close(), not ws.close()) — no WS close frame
3. Immediately make a plain HTTP GET /info request
4. Check that the HTTP response is valid JSON, not WS garbage

Without fix: ~18% of attempts fail with connection error or garbage response.
With fix: 0 failures across all attempts.
"""

import json
import time
import pytest
import websocket
from urllib.parse import urlparse
from packet_injector import PacketInjector


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    resp = api.update_settings({
        "rs485_1": {
            "tx_disabled": True,    # required for sniffer mode in QEMU
            "baudrate": 9600,       # RTU framing inter-frame gap depends on baud
            "stopbits": "1",
            "parity": "none",
            "databits": "8",
        }
    })
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    resp = api.set_port_mode(1, "tcp_bridge")
    assert resp.status_code == 200, f"_baseline: set_port_mode(1, tcp_bridge) failed: {resp.status_code} {resp.text}"


ATTEMPTS = 30  # ~18% failure rate without fix → P(miss) = 0.82^30 ≈ 0.3% at 30 attempts


@pytest.mark.timeout(180)
def test_stale_ws_fd_no_http_corruption(api):
    """Verify that abrupt WS disconnect does not corrupt subsequent HTTP responses."""
    parsed = urlparse(api.base_url)
    host = parsed.hostname
    port = parsed.port or 80

    original_mode = None
    try:
        info = api.get_info()
        assert info.status_code == 200
        original_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200
        time.sleep(0.3)

        failures = []
        successful_checks = 0  # count attempts that actually ran the HTTP check
        with PacketInjector(port=1):
            for attempt in range(ATTEMPTS):
                # Step 1: connect WS and collect at least 1 packet so that the
                # sniffer task has cached our fd
                ws_url = f"ws://{host}:{port}/sniffer/ws"
                cookies = "; ".join(
                    [f"{k}={v}" for k, v in api.session.cookies.items()]
                )
                ws = websocket.WebSocket()
                ws.settimeout(10)
                try:
                    ws.connect(ws_url, cookie=cookies)
                    ws.send(json.dumps({"cmd": "start", "port": 1}))

                    # Wait for at least 1 packet to ensure sniffer task has our fd cached
                    deadline = time.monotonic() + 5
                    got_packet = False
                    while time.monotonic() < deadline:
                        try:
                            msg = ws.recv()
                            if msg:
                                json.loads(msg)
                                got_packet = True
                                break
                        except websocket.WebSocketTimeoutException:
                            pass

                    if not got_packet:
                        ws.close()
                        continue  # no packets injected yet — skip attempt

                    # Step 2: abrupt TCP close — no WS close frame sent.
                    # The OS marks the fd as free; httpd may hand it to the next
                    # accepted connection.
                    try:
                        ws.sock.close()
                    except Exception:
                        pass

                    # Step 3: immediately open a plain HTTP connection.
                    # The 10 ms gap opens the race window: httpd may accept a new
                    # connection on the just-freed fd before sniffer_ws_task checks
                    # whether the fd is still a WebSocket.  Without the fix this
                    # race fires ~18% of the time; with 30 attempts the probability
                    # of missing it is 0.82^30 ≈ 0.3%, giving a reliable regression
                    # signal while keeping the test deterministic enough for CI.
                    time.sleep(0.01)

                    try:
                        resp = api.get_info()
                        if resp.status_code != 200:
                            failures.append(
                                f"attempt {attempt}: HTTP status {resp.status_code}"
                            )
                            continue
                        # Verify response is valid JSON info, not WS frame garbage
                        data = resp.json()
                        assert (
                            "rs485_1" in data or "uptime" in data or "fw_version" in data
                        ), (
                            f"attempt {attempt}: unexpected response keys: "
                            f"{list(data.keys())}"
                        )
                        successful_checks += 1
                    except Exception as e:
                        failures.append(
                            f"attempt {attempt}: HTTP request failed: {e}"
                        )

                except Exception as e:
                    failures.append(f"attempt {attempt}: WS setup failed: {e}")
                    try:
                        ws.close()
                    except Exception:
                        pass

        # Guard against false-positive: ensure PacketInjector was actually running
        # and at least half the attempts reached the HTTP check phase.
        MIN_CHECKS = ATTEMPTS // 2
        assert successful_checks >= MIN_CHECKS, (
            f"Too few attempts reached the HTTP check ({successful_checks}/{ATTEMPTS}); "
            f"PacketInjector may not be producing traffic — test result is unreliable"
        )
        assert len(failures) == 0, (
            f"Bug 09 regression: {len(failures)}/{successful_checks} checked attempts "
            f"got corrupted HTTP response:\n" + "\n".join(failures)
        )
        print(
            f"✓ {successful_checks}/{ATTEMPTS} attempts checked: "
            f"HTTP responses clean after abrupt WS disconnect"
        )

    finally:
        if original_mode:
            api.set_port_mode(1, original_mode)
