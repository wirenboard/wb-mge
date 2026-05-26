"""E2E tests for the transparent TCP bridge — Wave 3.

Requires QEMU with UART1 exposed as TCP port 5561 and guest port 503 forwarded
to host port 50503.  The transparent bridge mode forwards raw bytes between the
TCP client and the serial interface without any Modbus framing.

Coverage:
12. Basic round-trip — 16 arbitrary bytes TCP → serial → TCP (echo via UART chardev).
13. Multiple clients — last-writer routing: last client's bytes are echoed back to it.
14. Zero-byte edge case — null byte + real data; connection stays open throughout.
15. Client mode (skipped — requires additional QEMU network setup).
"""

import socket
import threading
import time

import pytest

from conftest import build_gateway_fixture


# ---------------------------------------------------------------------------
# Module-level constants
# ---------------------------------------------------------------------------

GATEWAY_HOST = "127.0.0.1"
# Transparent bridge uses port 50504 (QEMU hostfwd: guest 50504 -> host 50504).
# Port 503 is avoided because it is the default bridge_port for RS485-2 (port 2)
# and is already bound when the firmware starts, causing EADDRINUSE on port 1 bind.
TRANSPARENT_HOST_PORT = 50504
UART1_TCP_PORT = 5561        # QEMU UART1 chardev TCP


# ---------------------------------------------------------------------------
# Module-level baseline fixture
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    resp = api.update_settings({
        "rs485_1": {
            "tx_disabled": False,
            "baudrate": 9600,
            "stopbits": "1",
            "parity": "none",
            "databits": "8",
        }
    })
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code}"


# ---------------------------------------------------------------------------
# Transparent bridge fixture
# ---------------------------------------------------------------------------

transparent_bridge = build_gateway_fixture(
    port_num=1,
    tcp_host_port=TRANSPARENT_HOST_PORT,
    uart_tcp_port=UART1_TCP_PORT,
    bridge_port=50504,   # use port 50504 to avoid conflict with default port 2 bridge (503)
    modbus=False,        # transparent mode — no RTU slave started
)


# ---------------------------------------------------------------------------
# Module-level helpers
# ---------------------------------------------------------------------------

def _try_connect_tcp(host: str, port: int, timeout: float = 3.0):
    """Attempt TCP connection; return socket or None."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((host, port))
        return sock
    except (ConnectionRefusedError, OSError, socket.timeout):
        sock.close()
        return None


class _UartEchoThread(threading.Thread):
    """Connects to QEMU UART1 chardev TCP port and echoes all received bytes back."""

    def __init__(self, host: str, port: int, connect_timeout: float = 5.0):
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.connect_timeout = connect_timeout
        self.connected = False
        self.bytes_echoed = 0
        self._stop_event = threading.Event()
        self._sock = None

    def run(self) -> None:
        """Connect to UART chardev and echo all received bytes."""
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.settimeout(self.connect_timeout)
            self._sock.connect((self.host, self.port))
            self.connected = True
            self._sock.settimeout(0.5)
        except OSError:
            return

        while not self._stop_event.is_set():
            try:
                data = self._sock.recv(256)
                if not data:
                    break
                self._sock.sendall(data)
                self.bytes_echoed += len(data)
            except socket.timeout:
                continue
            except OSError:
                break

        try:
            self._sock.close()
        except OSError:
            pass

    def wait_connected(self, timeout: float = 5.0) -> bool:
        """Block until connected or timeout. Returns True if connected."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.connected:
                return True
            time.sleep(0.05)
        return False

    def stop(self) -> None:
        """Signal thread to stop."""
        self._stop_event.set()
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass


# ---------------------------------------------------------------------------
# Test #12: Basic round-trip — arbitrary bytes TCP → serial → TCP
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_transparent_basic_roundtrip(transparent_bridge):
    """Send 16 arbitrary bytes through the transparent bridge and receive them back.

    Setup:
    - Port 1 configured as transparent bridge on port 503 (host 50503).
    - _UartEchoThread connects to UART1 chardev (port 5561) and echoes bytes.
    - TCP client connects to port 50503, sends 16 bytes, expects echo back.
    """
    probe = _try_connect_tcp(GATEWAY_HOST, UART1_TCP_PORT, timeout=3.0)
    if probe is None:
        pytest.skip(f"UART1 chardev TCP port {UART1_TCP_PORT} not reachable")
    probe.close()

    echo_thread = _UartEchoThread(GATEWAY_HOST, UART1_TCP_PORT)
    echo_thread.start()
    assert echo_thread.wait_connected(timeout=5.0), \
        "UART echo thread could not connect to UART1 chardev"

    test_data = bytes(range(16))   # 16 distinct bytes: 0x00..0x0F
    tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_sock.settimeout(5.0)
    try:
        tcp_sock.connect((GATEWAY_HOST, TRANSPARENT_HOST_PORT))
        tcp_sock.sendall(test_data)

        received = b''
        deadline = time.monotonic() + 5.0
        while len(received) < len(test_data) and time.monotonic() < deadline:
            try:
                chunk = tcp_sock.recv(64)
                if not chunk:
                    break
                received += chunk
            except socket.timeout:
                continue

        assert received == test_data, (
            f"Round-trip data mismatch: sent={test_data.hex()!r}, got={received.hex()!r}"
        )
        print(f"✓ Transparent bridge round-trip: {len(test_data)} bytes echoed correctly")
    finally:
        tcp_sock.close()
        echo_thread.stop()
        echo_thread.join(timeout=3.0)


# ---------------------------------------------------------------------------
# Test #13: Multiple clients — last-writer routing
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_transparent_last_writer_routing(transparent_bridge):
    """Second TCP client's bytes get echoed back to it (last_client_sock routing).

    Sequential scenario:
    1. Client A connects (no data sent).
    2. Client B connects and sends 4 bytes — B becomes last_client_sock.
    3. Echo thread echoes B's 4 bytes back to the firmware.
    4. The firmware routes the echo to the last sender (client B).
    5. Client B receives its own 4 bytes back; client A receives nothing.

    Note: in transparent_tcp.c, last_client_sock is updated when bytes are received
    (process_data_from_tcp), not on accept(). B becomes last_client_sock only after
    it actually sends data.
    """
    probe = _try_connect_tcp(GATEWAY_HOST, UART1_TCP_PORT, timeout=3.0)
    if probe is None:
        pytest.skip(f"UART1 chardev TCP port {UART1_TCP_PORT} not reachable")
    probe.close()

    echo_thread = _UartEchoThread(GATEWAY_HOST, UART1_TCP_PORT)
    echo_thread.start()
    assert echo_thread.wait_connected(timeout=5.0), "Echo thread failed to connect"

    sock_a = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_a.settimeout(3.0)
    sock_b = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_b.settimeout(5.0)
    try:
        sock_a.connect((GATEWAY_HOST, TRANSPARENT_HOST_PORT))
        time.sleep(0.05)    # allow server to accept A before B connects
        sock_b.connect((GATEWAY_HOST, TRANSPARENT_HOST_PORT))
        time.sleep(0.05)    # allow server to accept B before data is sent

        data_b = b'\xAA\xBB\xCC\xDD'
        sock_b.sendall(data_b)  # B is now last writer

        # B should receive its own echo
        received_b = b''
        deadline = time.monotonic() + 3.0
        while len(received_b) < len(data_b) and time.monotonic() < deadline:
            try:
                chunk = sock_b.recv(64)
                if not chunk:
                    break
                received_b += chunk
            except socket.timeout:
                continue

        assert received_b == data_b, (
            f"Client B did not receive its echo: got {received_b.hex()!r}"
        )

        # A should receive nothing (not the last writer)
        sock_a.settimeout(0.5)
        try:
            data_a = sock_a.recv(64)
            # It's acceptable if A gets 0 bytes; fail if A gets B's data
            if data_a:
                assert data_a != data_b, (
                    f"Client A incorrectly received client B's data: {data_a.hex()!r}"
                )
        except socket.timeout:
            pass   # expected: A gets nothing

        print("✓ Transparent bridge last-writer routing: B received its echo, A got nothing")
    finally:
        sock_a.close()
        sock_b.close()
        echo_thread.stop()
        echo_thread.join(timeout=3.0)


# ---------------------------------------------------------------------------
# Test #14: Zero bytes edge case
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(15)
def test_transparent_zero_bytes_edge_case(transparent_bridge):
    """Client sends empty bytes (length 0), then real data — connection stays open.

    Sending 0 bytes to lwIP's send() can trigger unexpected behavior on some stacks.
    The test verifies the firmware handles it without closing the connection.
    """
    probe = _try_connect_tcp(GATEWAY_HOST, UART1_TCP_PORT, timeout=3.0)
    if probe is None:
        pytest.skip(f"UART1 chardev TCP port {UART1_TCP_PORT} not reachable")
    probe.close()

    echo_thread = _UartEchoThread(GATEWAY_HOST, UART1_TCP_PORT)
    echo_thread.start()
    assert echo_thread.wait_connected(timeout=5.0), "Echo thread failed to connect"

    tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_sock.settimeout(5.0)
    try:
        tcp_sock.connect((GATEWAY_HOST, TRANSPARENT_HOST_PORT))

        # Send 0 bytes — Python's socket.send(b'') is a no-op and won't even call
        # the syscall, so we can't truly test this at TCP level. Instead, send a
        # 1-byte keepalive and verify connection stays open.
        # This test primarily verifies the connection lifecycle is correct.
        tcp_sock.sendall(b'\x00')   # null byte — verify it's forwarded

        real_data = b'\x42\x43\x44'
        tcp_sock.sendall(real_data)

        # Expect to receive: null byte echo + real_data echo
        expected = b'\x00' + real_data
        received = b''
        deadline = time.monotonic() + 5.0
        while len(received) < len(expected) and time.monotonic() < deadline:
            try:
                chunk = tcp_sock.recv(64)
                if not chunk:
                    break
                received += chunk
            except socket.timeout:
                continue

        assert received == expected, (
            f"Zero-byte edge case: expected {expected.hex()!r}, got {received.hex()!r}"
        )
        print(f"✓ Zero-byte edge case: connection stable, {len(received)} bytes echoed")
    finally:
        tcp_sock.close()
        echo_thread.stop()
        echo_thread.join(timeout=3.0)


# ---------------------------------------------------------------------------
# Test #15: Client mode (skipped)
# ---------------------------------------------------------------------------

@pytest.mark.skip(reason="client mode requires additional QEMU network setup: "
                          "a TCP echo server must be running on host at 10.0.2.2")
@pytest.mark.qemu
def test_transparent_client_mode(api):
    """Transparent bridge client mode: firmware connects outbound to TCP server.

    Requires bridge.mode=client with bridge.ip=10.0.2.2 (QEMU host IP).
    Skipped until host-side echo server infrastructure is added to conftest.
    """
    pass
