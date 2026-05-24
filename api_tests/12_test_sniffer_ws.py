"""
Sniffer WebSocket integration tests.

Connects to the device WebSocket sniffer endpoint, collects Modbus packets
from RS-485 port 1, and verifies that both valid and bad-CRC packets arrive
and that the sniffer remains functional after a bad-CRC packet.

Extended coverage includes:
  - JSON packet field validation (port, id, timestamp_us, raw, sender)
  - Stream splitter verification (separate master/slave packets)
  - Stop command behavior
  - Malformed JSON resilience
  - Port mode restore after teardown
"""

import json
import re
import threading
import time

import pytest
import websocket
from urllib.parse import urlparse

from sniffer_helpers import _ws_connect, _collect_packets
from packet_injector import PacketInjector


@pytest.mark.timeout(120)
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

        with PacketInjector(port=1):
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
                except websocket.WebSocketPayloadException:
                    pass
                except websocket.WebSocketProtocolException:
                    pass
                except json.JSONDecodeError:
                    pass

            print(f"\n  Collected {len(packets)} packet(s) in phase 1")
            assert len(packets) > 0, "No packets received — check that PacketInjector is running"

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
                    except websocket.WebSocketPayloadException:
                        continue
                    except websocket.WebSocketProtocolException:
                        continue
                    except json.JSONDecodeError:
                        continue

            assert len(invalid_pkts) >= 1, (
                "Expected >= 1 bad-CRC packet (PacketInjector emits one every "
                "bad_crc_period cycles). "
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
                except websocket.WebSocketPayloadException:
                    continue
                except websocket.WebSocketProtocolException:
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


def test_sniffer_ws_no_auth(api):
    """Sniffer WebSocket: unauthenticated upgrade must be closed immediately by server"""
    parsed = urlparse(api.base_url)
    host = parsed.hostname
    port = parsed.port or 80

    ws_url = f"ws://{host}:{port}/sniffer/ws"
    ws = websocket.WebSocket()
    ws.settimeout(10)

    connection_closed_by_server = False
    try:
        # Connect without cookies — IDF limitation: 101 is always sent, but server
        # will immediately close the connection via httpd_sess_trigger_close()
        ws.connect(ws_url)
        ws.settimeout(5)
        try:
            # Server should have scheduled close; recv must fail
            ws.recv()
            # If recv returned something, connection was NOT immediately closed
        except (websocket.WebSocketConnectionClosedException, ConnectionResetError, OSError):
            connection_closed_by_server = True
        except websocket.WebSocketTimeoutException:
            pass  # timeout means server did not close fast enough — flag stays False
    except (websocket.WebSocketConnectionClosedException, ConnectionResetError, OSError):
        # Connection might be closed even before or during connect
        connection_closed_by_server = True
    except websocket.WebSocketBadStatusException:
        # Future IDF versions might actually send non-101; treat as rejection too
        connection_closed_by_server = True
    finally:
        try:
            ws.close()
        except Exception:
            pass

    assert connection_closed_by_server, (
        "Expected server to close unauthenticated WS connection immediately, "
        "but connection remained open and recv succeeded"
    )
    print("✓ Unauthenticated WS connection closed by server immediately after upgrade")


def test_sniffer_ws_invalid_cookie(api):
    """Sniffer WebSocket: connection with invalid cookie must be closed immediately by server"""
    parsed = urlparse(api.base_url)
    host = parsed.hostname
    port = parsed.port or 80

    ws_url = f"ws://{host}:{port}/sniffer/ws"
    ws = websocket.WebSocket()
    ws.settimeout(10)

    connection_closed_by_server = False
    try:
        # Connect with a fake session_id — server will close immediately after upgrade
        ws.connect(ws_url, cookie="session_id=9999999999")
        ws.settimeout(5)
        try:
            ws.recv()
        except (websocket.WebSocketConnectionClosedException, ConnectionResetError, OSError):
            connection_closed_by_server = True
        except websocket.WebSocketTimeoutException:
            pass  # timeout means server did not close fast enough — flag stays False
    except (websocket.WebSocketConnectionClosedException, ConnectionResetError, OSError):
        connection_closed_by_server = True
    except websocket.WebSocketBadStatusException:
        connection_closed_by_server = True
    finally:
        try:
            ws.close()
        except Exception:
            pass

    assert connection_closed_by_server, (
        "Expected server to close invalid-cookie WS connection immediately, "
        "but connection remained open and recv succeeded"
    )
    print("✓ Invalid-cookie WS connection closed by server immediately after upgrade")


# ---------------------------------------------------------------------------
# Group 1: WS protocol and JSON packet fields
# ---------------------------------------------------------------------------

def test_sniffer_ws_packet_port_field_matches_started_port(api):
    """All received packets must report port==1 when sniffer started for port 1."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        # Save original port 1 mode
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=1):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(
                ws,
                min_count=5,
                timeout_sec=15,
                filter_fn=lambda p: p.get("type") == "packet",
            )

            assert len(packets) >= 5, (
                f"Expected >=5 packets but got {len(packets)}; check PacketInjector is running"
            )
            assert all(p["port"] == 1 for p in packets), (
                f"Some packets have wrong port field: {[p['port'] for p in packets]}"
            )
            print(f"✓ All {len(packets)} packets have port==1")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            r = api.set_port_mode(1, original_port_mode)
            assert r.status_code == 200, f"Failed to restore port mode: {r.status_code}"


def test_sniffer_ws_id_monotonically_increasing(api):
    """Packet id field must be strictly increasing across all received packets."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=1):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(ws, min_count=10, timeout_sec=15)

            assert len(packets) >= 10, (
                f"Expected >=10 packets but got {len(packets)}; check PacketInjector is running"
            )
            ids = [p["id"] for p in packets]
            violations = [
                (i, ids[i], ids[i + 1])
                for i in range(len(ids) - 1)
                if ids[i] >= ids[i + 1]
            ]
            assert len(violations) == 0, (
                f"packet id is not strictly increasing at positions: {violations}"
            )
            print(f"✓ id strictly increasing across {len(packets)} packets")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            r = api.set_port_mode(1, original_port_mode)
            assert r.status_code == 200, f"Failed to restore port mode: {r.status_code}"


def test_sniffer_ws_timestamp_positive_and_plausible(api):
    """timestamp_us must be a positive integer and the sequence must be non-decreasing."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=1):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(ws, min_count=5, timeout_sec=15)

            assert len(packets) >= 5, (
                f"Expected >=5 packets but got {len(packets)}; check PacketInjector is running"
            )

            for i, p in enumerate(packets):
                ts = p.get("timestamp_us")
                assert isinstance(ts, int), (
                    f"Packet[{i}] timestamp_us is not an int: {type(ts).__name__} = {ts!r}"
                )
                assert ts > 0, f"Packet[{i}] timestamp_us is not positive: {ts}"

            timestamps = [p["timestamp_us"] for p in packets]
            for i in range(len(timestamps) - 1):
                assert timestamps[i] <= timestamps[i + 1], (
                    f"Timestamp sequence is not non-decreasing at index {i}: "
                    f"{timestamps[i]} > {timestamps[i + 1]}"
                )
            print(f"✓ All {len(packets)} timestamps are positive and non-decreasing")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            r = api.set_port_mode(1, original_port_mode)
            assert r.status_code == 200, f"Failed to restore port mode: {r.status_code}"


def test_sniffer_ws_raw_field_is_hex_and_matches_size(api):
    """raw field must be uppercase hex and its length must equal size*2."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=1):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(
                ws,
                min_count=5,
                timeout_sec=15,
                filter_fn=lambda p: p.get("type") == "packet",
            )

            assert len(packets) >= 5, (
                f"Expected >=5 type==packet packets but got {len(packets)}; "
                "check PacketInjector is running"
            )

            for i, p in enumerate(packets):
                raw = p.get("raw", "")
                size = p.get("size", -1)
                assert len(raw) == size * 2, (
                    f"Packet[{i}] raw length {len(raw)} != size*2 {size * 2}; raw={raw!r}"
                )
                # Implementation uses %02X so output is uppercase hex
                assert re.fullmatch(r"[0-9A-F]+", raw), (
                    f"Packet[{i}] raw is not uppercase hex: {raw!r}"
                )
            print(f"✓ raw field validated for {len(packets)} type==packet packets")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            r = api.set_port_mode(1, original_port_mode)
            assert r.status_code == 200, f"Failed to restore port mode: {r.status_code}"


def test_sniffer_ws_stream_splitter_produces_separate_request_and_response(api):
    """Stream splitter must emit separate master (FC03) and slave packets."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=1):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(
                ws,
                min_count=10,
                timeout_sec=15,
                filter_fn=lambda p: p.get("type") == "packet" and p.get("crc_valid") is True,
            )

            assert len(packets) >= 10, (
                f"Expected >=10 valid packets but got {len(packets)}; "
                "check PacketInjector is running"
            )

            master_fc3 = [p for p in packets if p.get("sender") == "master" and p.get("function") == 3]
            slave_pkts = [p for p in packets if p.get("sender") == "slave"]

            assert len(master_fc3) > 0, "No master FC03 packets seen"
            assert len(slave_pkts) > 0, "No slave packets seen"

            # Find a matched consecutive master→slave pair to verify timestamp ordering.
            # Avoid comparing packets from different transactions (e.g. orphan responses
            # emitted when the sniffer starts mid-exchange may precede the first master).
            matched_pair = None
            for i in range(len(packets) - 1):
                m = packets[i]
                s = packets[i + 1]
                if (
                    m.get("sender") == "master"
                    and s.get("sender") == "slave"
                    and m.get("slave_id") == s.get("slave_id")
                    and m.get("function") == s.get("function")
                ):
                    matched_pair = (m, s)
                    break

            if matched_pair is not None:
                m, s = matched_pair
                assert s["timestamp_us"] >= m["timestamp_us"], (
                    f"Slave timestamp {s['timestamp_us']} < master timestamp {m['timestamp_us']} "
                    f"in matched pair"
                )
            print(
                f"✓ stream_splitter: {len(master_fc3)} master FC03, {len(slave_pkts)} slave packets"
            )

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            r = api.set_port_mode(1, original_port_mode)
            assert r.status_code == 200, f"Failed to restore port mode: {r.status_code}"


def test_sniffer_ws_sender_alternates_master_slave(api):
    """At least 2 consecutive (master → slave) pairs with matching slave_id and function."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=1):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(
                ws,
                min_count=10,
                timeout_sec=15,
                filter_fn=lambda p: p.get("type") == "packet" and p.get("crc_valid") is True,
            )

            assert len(packets) >= 10, (
                f"Expected >=10 valid packets but got {len(packets)}; "
                "check PacketInjector is running"
            )

            pairs_found = 0
            for i in range(len(packets) - 1):
                m = packets[i]
                s = packets[i + 1]
                if (
                    m.get("sender") == "master"
                    and s.get("sender") == "slave"
                    and m.get("slave_id") == s.get("slave_id")
                    and m.get("function") == s.get("function")
                ):
                    pairs_found += 1

            assert pairs_found >= 2, (
                f"Expected >=2 master→slave pairs with matching slave_id/function, "
                f"found {pairs_found}"
            )
            print(f"✓ Found {pairs_found} matching master→slave pairs")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            r = api.set_port_mode(1, original_port_mode)
            assert r.status_code == 200, f"Failed to restore port mode: {r.status_code}"


# ---------------------------------------------------------------------------
# Group 2: Stop command and verification
# ---------------------------------------------------------------------------

@pytest.mark.timeout(60)
def test_sniffer_ws_stop_command_stops_stream(api):
    """After sending stop, no more packets must arrive and /sniffer/status must reflect false."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        injector = PacketInjector(port=1)
        injector.start()
        try:
            ws, stop_ping, _ = _ws_connect(api, 1)

            # Confirm sniffer is producing packets before stopping
            pre_stop = _collect_packets(ws, min_count=1, timeout_sec=10)
            assert len(pre_stop) >= 1, "No packets received before stop — check PacketInjector"
        finally:
            # Stop injection before checking silence so any post-stop packets
            # come solely from drained queue, not from continued injection
            # racing the sniffer's disable.
            injector.stop()

        # Send stop and kill the ping thread before verifying silence —
        # concurrent ws.ping() and ws.recv() can deadlock in websocket-client.
        stop_ping.set()
        ws.send(json.dumps({"cmd": "stop", "port": 1}))

        # Phase A: drain in-flight packets — the server may have already
        # queued a packet before processing the stop command.  Wait until
        # 2 consecutive seconds pass with no data.
        ws.settimeout(2)
        consecutive_silent = 0
        drain_deadline = time.monotonic() + 10
        while time.monotonic() < drain_deadline:
            try:
                msg = ws.recv()
                if msg:
                    consecutive_silent = 0
                    continue
            except websocket.WebSocketTimeoutException:
                consecutive_silent += 1
                if consecutive_silent >= 1:
                    break
            except (websocket.WebSocketPayloadException,
                    websocket.WebSocketProtocolException,
                    json.JSONDecodeError):
                pass

        # Phase B: verify silence — no packets for 3 full seconds.
        ws.settimeout(1)
        post_stop_packets = []
        post_deadline = time.monotonic() + 3
        while time.monotonic() < post_deadline:
            try:
                msg = ws.recv()
                if msg:
                    post_stop_packets.append(json.loads(msg))
            except websocket.WebSocketTimeoutException:
                pass
            except (websocket.WebSocketPayloadException,
                    websocket.WebSocketProtocolException,
                    json.JSONDecodeError):
                pass

        assert len(post_stop_packets) == 0, (
            f"Expected 0 packets after stop, got {len(post_stop_packets)}"
        )

        # Verify /sniffer/status reflects the stopped state
        status_resp = api.get_sniffer_status()
        assert status_resp.status_code == 200, f"Unexpected status code: {status_resp.status_code}"
        body = status_resp.json()
        assert body.get("port_1") is False, (
            f"Expected port_1==False after stop, got {body.get('port_1')}"
        )
        print("✓ stop command stops stream and /sniffer/status reflects false")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            # Stop already sent above; just close
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            r = api.set_port_mode(1, original_port_mode)
            assert r.status_code == 200, f"Failed to restore port mode: {r.status_code}"


def test_sniffer_ws_malformed_json_does_not_crash(api):
    """Server must survive malformed WS messages and still serve packets afterwards."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=1):
            parsed = urlparse(api.base_url)
            host = parsed.hostname
            http_port = parsed.port or 80
            ws_url = f"ws://{host}:{http_port}/sniffer/ws"
            cookies = "; ".join([f"{k}={v}" for k, v in api.session.cookies.items()])

            ws = websocket.WebSocket()
            ws.settimeout(15)
            ws.connect(ws_url, cookie=cookies)

            stop_ping = threading.Event()

            def _ping():
                while not stop_ping.is_set():
                    try:
                        ws.ping()
                    except Exception:
                        break
                    time.sleep(0.5)

            ping_thread = threading.Thread(target=_ping, daemon=True)
            ping_thread.start()

            malformed_messages = [
                "not json",
                "{}",
                '{"cmd":"start"}',           # missing port field
                '{"cmd":"start","port":"x"}', # port is not a number
            ]

            for bad_msg in malformed_messages:
                ws.send(bad_msg)
                # Try to receive something briefly; a closed connection would raise
                ws.settimeout(0.5)
                try:
                    ws.recv()
                except websocket.WebSocketTimeoutException:
                    pass  # expected — no immediate reply to malformed messages
                except websocket.WebSocketConnectionClosedException:
                    pytest.fail(f"Server closed connection after malformed message: {bad_msg!r}")
                except websocket.WebSocketPayloadException:
                    pass
                except websocket.WebSocketProtocolException:
                    pass
                except json.JSONDecodeError:
                    pass

            # Reconnect with a fresh WebSocket so residual state doesn't interfere
            stop_ping.set()
            try:
                ws.close()
            except Exception:
                pass
            time.sleep(1)

            ws = websocket.WebSocket()
            ws.settimeout(15)
            ws.connect(ws_url, cookie=cookies)

            stop_ping = threading.Event()

            def _ping2():
                while not stop_ping.is_set():
                    try:
                        ws.ping()
                    except Exception:
                        break
                    time.sleep(0.5)

            ping_thread = threading.Thread(target=_ping2, daemon=True)
            ping_thread.start()

            # Now send a valid start and expect packets
            ws.send(json.dumps({"cmd": "start", "port": 1}))
            ws.settimeout(5)
            packets = _collect_packets(ws, min_count=3, timeout_sec=10)

            assert len(packets) >= 3, (
                f"Expected >=3 packets after malformed messages but got {len(packets)}; "
                "server may have crashed"
            )
            print(f"✓ Server survived {len(malformed_messages)} malformed messages; "
                  f"{len(packets)} packets received afterwards")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            r = api.set_port_mode(1, original_port_mode)
            assert r.status_code == 200, f"Failed to restore port mode: {r.status_code}"


def test_sniffer_ws_stop_before_start_does_not_crash(api):
    """Sending stop before start must not crash the server; start must work afterwards."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=1):
            parsed = urlparse(api.base_url)
            host = parsed.hostname
            http_port = parsed.port or 80
            ws_url = f"ws://{host}:{http_port}/sniffer/ws"
            cookies = "; ".join([f"{k}={v}" for k, v in api.session.cookies.items()])

            ws = websocket.WebSocket()
            ws.settimeout(15)
            ws.connect(ws_url, cookie=cookies)

            stop_ping = threading.Event()

            def _ping():
                while not stop_ping.is_set():
                    try:
                        ws.ping()
                    except Exception:
                        break
                    time.sleep(0.5)

            ping_thread = threading.Thread(target=_ping, daemon=True)
            ping_thread.start()

            # Send stop immediately — before any start
            ws.send(json.dumps({"cmd": "stop", "port": 1}))
            time.sleep(0.3)

            # Verify /sniffer/status is still 200 and port_1 is false
            status_resp = api.get_sniffer_status()
            assert status_resp.status_code == 200, (
                f"Expected 200 from /sniffer/status, got {status_resp.status_code}"
            )
            body = status_resp.json()
            assert body.get("port_1") is False, (
                f"Expected port_1==False after premature stop, got {body.get('port_1')}"
            )

            # Reconnect with a fresh WebSocket so residual state doesn't interfere
            stop_ping.set()
            try:
                ws.close()
            except Exception:
                pass
            time.sleep(1)

            ws = websocket.WebSocket()
            ws.settimeout(15)
            ws.connect(ws_url, cookie=cookies)

            stop_ping = threading.Event()

            def _ping2():
                while not stop_ping.is_set():
                    try:
                        ws.ping()
                    except Exception:
                        break
                    time.sleep(0.5)

            ping_thread = threading.Thread(target=_ping2, daemon=True)
            ping_thread.start()

            # Now start normally and expect packets
            ws.send(json.dumps({"cmd": "start", "port": 1}))
            packets = _collect_packets(ws, min_count=3, timeout_sec=10)

            assert len(packets) >= 3, (
                f"Expected >=3 packets after stop-before-start but got {len(packets)}; "
                "server may have crashed"
            )
            print(f"✓ stop-before-start did not crash; {len(packets)} packets received")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            r = api.set_port_mode(1, original_port_mode)
            assert r.status_code == 200, f"Failed to restore port mode: {r.status_code}"


# ---------------------------------------------------------------------------
# Group 4: WS lifecycle
# ---------------------------------------------------------------------------

def test_sniffer_ws_restore_port_mode_on_teardown(api):
    """Port mode must be correctly restored to its original value after sniffer usage."""
    api.reconnect()

    original_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")
        print(f"  Original port 1 mode: {original_mode}")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=1):
            ws, stop_ping, _ = _ws_connect(api, 1)

            # Collect at least 1 packet to confirm sniffer is active
            packets = _collect_packets(ws, min_count=1, timeout_sec=10)
            assert len(packets) >= 1, "No packets received — check PacketInjector"

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        if original_mode is not None:
            r = api.set_port_mode(1, original_mode)
            assert r.status_code == 200, f"Failed to restore port mode: {r.status_code}"

    # Verify restoration result (read-only check; runs only when the test body succeeds)
    info_after = api.get_info()
    assert info_after.status_code == 200
    restored_mode = info_after.json().get("rs485_1", {}).get("port_mode")
    assert restored_mode == original_mode, (
        f"Port mode not restored: expected {original_mode!r}, got {restored_mode!r}"
    )
    print(f"✓ Port 1 mode correctly restored to {original_mode!r}")
