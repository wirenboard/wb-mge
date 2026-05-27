"""E2E tests for POST /ports/{N}/send endpoint.

Tests that:
1. A valid RTU hex frame is transmitted on the RS-485 bus and appears in the sniffer log.
2. With tx_disabled=True the API returns success but the packet does NOT appear in the log.
3. An odd-length hex string produces an error response.
4. An empty hex string results in sent=0 with 200 OK.
"""

import pytest
import time

from sniffer_helpers import _ws_connect, _collect_packets


# Known FC03 request: slave=0x01, FC=03, addr=0x0000, count=10
# CRC of [01 03 00 00 00 0A] = C5 CD (verified)
FC03_HEX = "01030000000AC5CD"


@pytest.fixture(autouse=True)
def sniffer_mode(api):
    """Set port 1 to sniffer mode before each test, restore after."""
    original_mode = None
    try:
        info = api.get_info()
        if info.status_code == 200:
            original_mode = info.json().get("rs485_1", {}).get("port_mode", "sniffer")

        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Failed to set port 1 to sniffer: {resp.text}"
        time.sleep(0.5)
        yield
    finally:
        if original_mode and original_mode != "sniffer":
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

        ws, stop_event, ping_thread = _ws_connect(api, port=1)
        try:
            resp = api.send_packet(1, FC03_HEX)
            # API should still return 200 (serial_send is called but silently dropped)
            assert resp.status_code == 200, f"Expected 200, got {resp.status_code}: {resp.text}"

            def is_our_packet(pkt):
                # Match by function/slave/port (works for both packet and timeout events)
                return (pkt.get("function") == 3 and
                        pkt.get("slave_id") == 1 and
                        pkt.get("port") == 1)

            packets = _collect_packets(ws, min_count=1, timeout_sec=2, filter_fn=is_our_packet)
            assert len(packets) == 0, (
                f"Expected NO packets in sniffer when tx_disabled=True, "
                f"but got {len(packets)} packets"
            )
        finally:
            stop_event.set()
            ws.close()

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
