"""E2E tests for the transparent RS-485 repeater (Port 1 <-> Port 2 passthrough).

Requires QEMU with UART1 exposed as TCP 5561 and UART2 as TCP 5562. The repeater
forwards raw bytes between the two serial ports in both directions.

Coverage:
- E2E-1: live bidirectional forwarding when BOTH ports are in repeater mode.
- E2E-2: negative — only one port in repeater → bytes are dropped, none forwarded,
         and the link reports inactive.
"""

import socket
import time

import pytest

GATEWAY_HOST = "127.0.0.1"
UART1_TCP_PORT = 5561        # QEMU UART1 chardev TCP (RS-485 port 1)
UART2_TCP_PORT = 5562        # QEMU UART2 chardev TCP (RS-485 port 2)


class _UartByteProbe:
    """Raw socket to a QEMU UART chardev: send() raw bytes and recv() up to n bytes.

    Unlike _UartEchoThread (25_test_transparent_tcp_e2e.py) this does NOT echo —
    the repeater must move bytes between the two ports on its own; echoing would
    pollute the stream.
    """

    def __init__(self, host: str, port: int, connect_timeout: float = 5.0):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.settimeout(connect_timeout)
        self._sock.connect((host, port))
        self._sock.settimeout(0.5)

    def send(self, data: bytes) -> None:
        self._sock.sendall(data)

    def recv(self, n: int, timeout: float = 3.0) -> bytes:
        buf = b""
        deadline = time.monotonic() + timeout
        while len(buf) < n and time.monotonic() < deadline:
            try:
                chunk = self._sock.recv(n - len(buf))
                if not chunk:
                    break
                buf += chunk
            except socket.timeout:
                continue
        return buf

    def drain(self, timeout: float = 0.3) -> None:
        """Discard any pending bytes so a counter baseline is clean."""
        self._sock.settimeout(timeout)
        try:
            while True:
                if not self._sock.recv(256):
                    break
        except socket.timeout:
            pass
        finally:
            self._sock.settimeout(0.5)

    def close(self) -> None:
        try:
            self._sock.close()
        except OSError:
            pass


def _read_repeater_stats(api) -> dict:
    """GET /info → the 'repeater' sub-object (empty dict if absent)."""
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    return resp.json().get("repeater", {})


def _skip_if_uart_unreachable(port: int) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(3.0)
    try:
        sock.connect((GATEWAY_HOST, port))
    except (ConnectionRefusedError, OSError, socket.timeout):
        pytest.skip(f"UART chardev TCP port {port} not reachable")
    finally:
        sock.close()


@pytest.mark.qemu
# 65 s, not 40 s: an item's pytest-timeout budget covers SETUP as well as the call, and
# this is the first item of the module. The module defines no fixtures of its own, but
# when this file is run on its own the item still pays conftest's once-per-session rs485
# snapshot (one bounded GET /settings, 20.1 s, see _RS485_HTTP_TIMEOUT) — in a full-suite
# run that lands on the very first item of the session (00_test_heap_session.py) instead.
# 40 s body + 20.1 s snapshot + slack.
@pytest.mark.timeout(65)
def test_repeater_bidirectional_forwarding(api):
    """E2E-1: with both ports in repeater, bytes flow 1->2 and 2->1, counters advance.

    User journey: the operator turns the repeater on for both ports and the gateway
    transparently bridges traffic in both directions.
    """
    _skip_if_uart_unreachable(UART1_TCP_PORT)
    _skip_if_uart_unreachable(UART2_TCP_PORT)

    info = api.get_info().json()
    orig1 = info.get("rs485_1", {}).get("port_mode", "disabled")
    orig2 = info.get("rs485_2", {}).get("port_mode", "disabled")

    probe1 = probe2 = None
    try:
        assert api.set_port_mode(1, "repeater").status_code == 200, "set port 1 repeater failed"
        assert api.set_port_mode(2, "repeater").status_code == 200, "set port 2 repeater failed"
        time.sleep(0.5)

        probe1 = _UartByteProbe(GATEWAY_HOST, UART1_TCP_PORT)
        probe2 = _UartByteProbe(GATEWAY_HOST, UART2_TCP_PORT)
        probe1.drain()
        probe2.drain()

        rep = _read_repeater_stats(api)
        assert rep.get("active") is True, f"both ports in repeater → active expected True, got {rep}"
        base = _read_repeater_stats(api)

        # Port 1 -> Port 2
        payload_fwd = bytes(range(16))
        probe1.send(payload_fwd)
        got2 = probe2.recv(len(payload_fwd), timeout=5.0)
        assert got2 == payload_fwd, f"forward mismatch: sent={payload_fwd.hex()}, got={got2.hex()}"

        # Port 2 -> Port 1
        payload_rev = bytes([0xA0 + i for i in range(8)])
        probe2.send(payload_rev)
        got1 = probe1.recv(len(payload_rev), timeout=5.0)
        assert got1 == payload_rev, f"reverse mismatch: sent={payload_rev.hex()}, got={got1.hex()}"

        # Counters advanced in the correct direction (poll for async settle).
        rep = base
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            rep = _read_repeater_stats(api)
            if (rep.get("bytes_1to2", 0) >= base.get("bytes_1to2", 0) + len(payload_fwd)
                    and rep.get("bytes_2to1", 0) >= base.get("bytes_2to1", 0) + len(payload_rev)):
                break
            time.sleep(0.2)
        assert rep.get("bytes_1to2", 0) >= base.get("bytes_1to2", 0) + len(payload_fwd), \
            f"bytes_1to2 did not advance by >= {len(payload_fwd)}: base={base}, now={rep}"
        assert rep.get("bytes_2to1", 0) >= base.get("bytes_2to1", 0) + len(payload_rev), \
            f"bytes_2to1 did not advance by >= {len(payload_rev)}: base={base}, now={rep}"
        print("✓ repeater bridges both directions; counters advanced correctly")
    finally:
        if probe1:
            probe1.close()
        if probe2:
            probe2.close()
        api.set_port_mode(1, orig1)
        time.sleep(0.2)
        api.set_port_mode(2, orig2)
        time.sleep(0.2)


@pytest.mark.qemu
# 85 s, not 40 s: an item's pytest-timeout budget covers setup + call + TEARDOWN, and
# module-scoped fixtures are torn down inside the LAST item of the module. This is that
# item, so it also pays conftest's _restore_rs485_settings teardown — up to two bounded
# POST /settings plus a settle window (2 x 20.1 s + 1 s = 41.2 s, see _RS485_HTTP_TIMEOUT).
# 40 s body + 45 s teardown allowance.
@pytest.mark.timeout(85)
def test_repeater_single_port_drops_and_inactive(api):
    """E2E-2: with only one port in repeater, bytes are dropped and the link is inactive.

    User journey: the operator enabled the repeater on a single port — the gateway
    must not send into a dead peer and must report the link as inactive.
    """
    _skip_if_uart_unreachable(UART1_TCP_PORT)

    info = api.get_info().json()
    orig1 = info.get("rs485_1", {}).get("port_mode", "disabled")
    orig2 = info.get("rs485_2", {}).get("port_mode", "disabled")

    probe1 = None
    try:
        assert api.set_port_mode(1, "repeater").status_code == 200, "set port 1 repeater failed"
        assert api.set_port_mode(2, "disabled").status_code == 200, "set port 2 disabled failed"
        time.sleep(0.5)

        rep = _read_repeater_stats(api)
        assert rep.get("active") is False, f"one port only → active expected False, got {rep}"
        base = _read_repeater_stats(api)

        probe1 = _UartByteProbe(GATEWAY_HOST, UART1_TCP_PORT)
        probe1.drain()

        payload = bytes([0x55] * 12)
        probe1.send(payload)

        # No peer in repeater → bytes dropped on port 1, none forwarded.
        rep = base
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            rep = _read_repeater_stats(api)
            if rep.get("dropped_1", 0) >= base.get("dropped_1", 0) + len(payload):
                break
            time.sleep(0.2)
        assert rep.get("dropped_1", 0) >= base.get("dropped_1", 0) + len(payload), \
            f"dropped_1 did not grow by >= {len(payload)}: base={base}, now={rep}"
        assert rep.get("bytes_1to2", 0) == base.get("bytes_1to2", 0), \
            f"bytes_1to2 must NOT advance when the peer is down: base={base}, now={rep}"
        print("✓ single-port repeater: bytes dropped, none forwarded, link inactive")
    finally:
        if probe1:
            probe1.close()
        api.set_port_mode(1, orig1)
        time.sleep(0.2)
        api.set_port_mode(2, orig2)
        time.sleep(0.2)
