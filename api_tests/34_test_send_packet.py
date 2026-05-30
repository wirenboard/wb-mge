"""E2E tests for POST /ports/{N}/send endpoint.

Tests that:
1. A valid RTU hex frame is transmitted on the RS-485 bus and appears in the sniffer log.
2. With tx_disabled=True the API returns success but the packet does NOT appear in the log.
3. An odd-length hex string produces an error response.
4. An empty hex string results in sent=0 with 200 OK.
"""

import queue
import threading
import json
import pytest
import time

import websocket as _ws_module

from sniffer_helpers import _ws_connect, _collect_packets


# Known FC03 request: slave=0x01, FC=03, addr=0x0000, count=10
# CRC of [01 03 00 00 00 0A] = C5 CD (verified)
FC03_HEX = "01030000000AC5CD"


@pytest.fixture(autouse=True)
def sniffer_mode(api):
    """Open port 1 serial (passive transport) before each test so the WS sniffer
    overlay can observe sent packets; restore the original transport after."""
    original_mode = None
    try:
        info = api.get_info()
        if info.status_code == 200:
            original_mode = info.json().get("rs485_1", {}).get("port_mode", "passive")

        resp = api.set_port_mode(1, "passive")
        assert resp.status_code == 200, f"Failed to set port 1 to passive: {resp.text}"
        time.sleep(0.5)
        yield
    finally:
        if original_mode and original_mode != "passive":
            api.set_port_mode(1, original_mode)


@pytest.mark.qemu
def test_send_packet_appears_in_sniffer_log(api):
    """POST /ports/1/send → packet appears in sniffer WS within 3s."""
    ws, stop_event, ping_thread = _ws_connect(api, port=1)
    try:
        resp = api.send_packet(1, FC03_HEX)
        assert resp.status_code == 200, f"POST /ports/1/send failed: {resp.text}"
        result = resp.json()
        assert result.get("sent") == 8, f"Expected sent=8, got {result}"

        def is_our_packet(pkt):
            # Accept both packet and timeout events for our FC03 frame:
            # - type=packet: slave responded (raw matches)
            # - type=timeout: no response within 200ms (no raw field, but function/slave match)
            return (pkt.get("function") == 3 and
                    pkt.get("slave_id") == 1 and
                    pkt.get("port") == 1)

        packets = _collect_packets(ws, min_count=1, timeout_sec=5, filter_fn=is_our_packet)
        assert len(packets) >= 1, (
            f"Expected to see FC03 packet/timeout in sniffer within 5s, got none. "
            f"Hex sent: {FC03_HEX}"
        )
        pkt = packets[0]
        assert pkt.get("port") == 1
        assert pkt.get("function") == 3   # FC03
        assert pkt.get("slave_id") == 1
    finally:
        stop_event.set()
        ws.close()


def _collect_ws_packets_threaded(api_base_url, cookies, port_num, collect_sec, filter_fn=None):
    """Collect sniffer WS packets using a daemon thread to avoid blocking recv() hangs.

    Opens a fresh WebSocket connection, sends start command, collects for collect_sec
    seconds using a background thread with a queue, then closes.
    Returns a list of matching packet dicts.
    """
    from urllib.parse import urlparse
    parsed = urlparse(api_base_url)
    host = parsed.hostname
    http_port = parsed.port or 80
    ws_url = f"ws://{host}:{http_port}/sniffer/ws"
    cookie_str = "; ".join([f"{k}={v}" for k, v in cookies.items()])

    received: "queue.Queue[dict]" = queue.Queue()

    def on_message(sock, message):
        try:
            pkt = json.loads(message)
            if filter_fn is None or filter_fn(pkt):
                received.put(pkt)
        except (json.JSONDecodeError, TypeError):
            pass

    sock = _ws_module.WebSocketApp(
        ws_url,
        cookie=cookie_str,
        on_message=on_message,
    )
    thread = threading.Thread(target=sock.run_forever, daemon=True)
    thread.start()
    # Give the connection time to establish and send the start command
    time.sleep(0.3)
    try:
        sock.send(json.dumps({"cmd": "start", "port": port_num}))
    except Exception:
        pass
    # Collect for the specified window
    time.sleep(collect_sec)
    sock.close()
    thread.join(timeout=3.0)

    packets = []
    while not received.empty():
        packets.append(received.get_nowait())
    return packets


@pytest.mark.qemu
def test_send_packet_tx_disabled_no_sniffer_entry(api):
    """POST /ports/1/send with tx_disabled=True → API 200 but packet NOT in sniffer."""
    original_tx = None
    try:
        settings = api.get_settings()
        assert settings.status_code == 200
        original_tx = settings.json().get("rs485_1", {}).get("tx_disabled", False)

        resp = api.update_settings({"rs485_1": {"tx_disabled": True}})
        assert resp.status_code == 200
        time.sleep(0.3)

        def is_our_packet(pkt):
            return (pkt.get("function") == 3 and
                    pkt.get("slave_id") == 1 and
                    pkt.get("port") == 1)

        # Start collecting before sending so we don't miss anything
        collect_thread_result = []

        def collect_in_background():
            # Collect for 1.5s — long enough for the 200ms sniffer timeout to fire
            pkts = _collect_ws_packets_threaded(
                api.base_url, api.session.cookies, 1, 1.5, filter_fn=is_our_packet
            )
            collect_thread_result.extend(pkts)

        bg = threading.Thread(target=collect_in_background, daemon=True)
        bg.start()
        time.sleep(0.3)  # Allow WS to connect before sending

        resp = api.send_packet(1, FC03_HEX)
        assert resp.status_code == 200, f"Expected 200, got {resp.status_code}: {resp.text}"

        bg.join(timeout=5.0)  # Wait for collection to finish

        assert len(collect_thread_result) == 0, (
            f"Expected NO packets in sniffer when tx_disabled=True, "
            f"but got {len(collect_thread_result)} packets"
        )

    finally:
        if original_tx is not None:
            api.update_settings({"rs485_1": {"tx_disabled": original_tx}})


def test_send_packet_invalid_hex(api):
    """POST /ports/1/send with odd-length hex → error response."""
    resp = api.send_packet(1, "010")  # odd length
    data = resp.json()
    assert "error" in data or resp.status_code != 200, (
        f"Expected error for odd-length hex, got: {data}"
    )


def test_send_packet_empty_hex(api):
    """POST /ports/1/send with empty hex → valid response (0 bytes sent)."""
    resp = api.send_packet(1, "")
    # Empty string: hex_len=0, 0%2==0, 0/2=0 <= out_max → 0 bytes, sent=0
    assert resp.status_code == 200
    data = resp.json()
    assert data.get("sent") == 0
