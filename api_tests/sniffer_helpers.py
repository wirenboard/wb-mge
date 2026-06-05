"""
Shared helper functions for sniffer-related tests.

Used by both test_sniffer_ws.py and test_ports.py.
"""

import json
import threading
import time

import websocket
from urllib.parse import urlparse


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

    ws.send(json.dumps({"cmd": "start", "port": port}))

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
