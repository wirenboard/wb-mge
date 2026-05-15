"""
Sniffer WebSocket integration test.

Connects to the device WebSocket sniffer endpoint, collects Modbus packets
from RS-485 port 1, and verifies that both valid and bad-CRC packets arrive
and that the sniffer remains functional after a bad-CRC packet.
"""

import json
import threading
import time

import pytest
import websocket
from urllib.parse import urlparse


@pytest.mark.order(26)
def test_sniffer_websocket(api):
    """Sniffer WebSocket: collect packets, verify CRC fields, check resilience after bad-CRC"""
    parsed = urlparse(api.base_url)
    host = parsed.hostname
    port = parsed.port or 80

    original_port_mode = None
    ws = None
    stop_ping = threading.Event()

    try:
        info = api.get_info()
        assert info.status_code == 200
        info_data = info.json()
        original_port_mode = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
        print(f"  Port 1 original mode: {original_port_mode}")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)
        print("✓ Port 1 switched to sniffer mode")

        ws_url = f"ws://{host}:{port}/sniffer/ws"
        cookies = "; ".join([f"{k}={v}" for k, v in api.session.cookies.items()])
        ws = websocket.WebSocket()
        ws.settimeout(15)
        ws.connect(ws_url, cookie=cookies)
        print(f"✓ WebSocket connected to {ws_url}")

        ws.send(json.dumps({"cmd": "start", "port": 1}))

        def _ping_thread():
            while not stop_ping.is_set():
                try:
                    ws.ping()
                except Exception:
                    break
                time.sleep(0.5)

        ping_thread = threading.Thread(target=_ping_thread, daemon=True)
        ping_thread.start()

        # Phase 1: collect up to 20 packets within 15 s
        packets = []
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline and len(packets) < 20:
            try:
                msg = ws.recv()
                if not msg:
                    continue
                pkt = json.loads(msg)
                packets.append(pkt)
                crc = pkt.get("crc_valid", "N/A")
                print(f"  Packet #{len(packets)}: type={pkt.get('type')} crc_valid={crc}")
            except websocket.WebSocketTimeoutException:
                pass
            except json.JSONDecodeError:
                pass

        print(f"\n  Collected {len(packets)} packet(s) in phase 1")
        assert len(packets) > 0, "No packets received — check that QEMU mock is running"

        valid_pkts = []
        invalid_pkts = []
        for i, pkt in enumerate(packets):
            for field in ("type", "id", "port", "timestamp_us"):
                assert field in pkt, f"Packet[{i}] missing '{field}': {pkt}"
            if pkt["type"] == "packet":
                for field in ("sender", "slave_id", "function", "crc_valid", "raw", "size"):
                    assert field in pkt, f"Packet[{i}] missing '{field}': {pkt}"
                assert pkt["sender"] in ("master", "slave"), f"Packet[{i}] bad sender: {pkt['sender']}"
                assert isinstance(pkt["crc_valid"], bool), f"Packet[{i}] crc_valid not bool"
                if pkt["crc_valid"]:
                    valid_pkts.append(pkt)
                else:
                    invalid_pkts.append(pkt)

        assert len(valid_pkts) >= 1, f"Expected >= 1 valid packet, got {len(valid_pkts)}"
        print(f"✓ {len(valid_pkts)} valid CRC packet(s) received")

        # Phase 2: if no bad-CRC packets, extend up to 30 s
        if len(invalid_pkts) == 0:
            print("  [INFO] No bad-CRC packets yet — extending window (up to 30 s)...")
            ws.settimeout(5)
            extra_deadline = time.monotonic() + 30
            while time.monotonic() < extra_deadline:
                try:
                    msg = ws.recv()
                    if not msg:
                        continue
                    pkt = json.loads(msg)
                    packets.append(pkt)
                    if pkt.get("type") == "packet" and pkt.get("crc_valid") is False:
                        invalid_pkts.append(pkt)
                        print(f"  Bad-CRC packet: {pkt}")
                        break
                except websocket.WebSocketTimeoutException:
                    continue
                except json.JSONDecodeError:
                    continue

        assert len(invalid_pkts) >= 1, (
            "Expected >= 1 bad-CRC packet (mock injects one every 5×500ms cycles). "
            f"Received {len(packets)} total packets, none with crc_valid=false."
        )
        print(f"✓ {len(invalid_pkts)} bad-CRC packet(s) received")

        # Phase 3: verify valid packets still arrive AFTER bad-CRC
        ws.settimeout(5)
        post_bad_crc = []
        post_deadline = time.monotonic() + 5
        while time.monotonic() < post_deadline:
            try:
                msg = ws.recv()
                if not msg:
                    continue
                pkt = json.loads(msg)
                if pkt.get("type") == "packet" and pkt.get("crc_valid") is True:
                    post_bad_crc.append(pkt)
                    break
            except websocket.WebSocketTimeoutException:
                continue
            except json.JSONDecodeError:
                continue

        assert len(post_bad_crc) >= 1, (
            "No valid packets received AFTER bad-CRC — sniffer may have stalled. "
            f"Total packets: {len(packets)}"
        )
        print("✓ Valid packets arrive after bad-CRC — sniffer remains functional")

    finally:
        stop_ping.set()
        if ws is not None:
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            try:
                api.set_port_mode(1, original_port_mode)
                print(f"✓ Port 1 mode restored to {original_port_mode}")
            except Exception as e:
                raise AssertionError(f"Failed to restore port mode: {e}")
