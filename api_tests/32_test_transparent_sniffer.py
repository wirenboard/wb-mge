"""E2E tests for transparent TCP bridge and sniffer.

Coverage:
TR-01  Port 2 basic round-trip via transparent bridge.
TR-02  Server reconnect after RST disconnect.
TR-03  Large payload (1024 bytes) with embedded null bytes forwarded correctly.
TR-04  Client-mode reconnect after server closes connection.
TR-05  UART bytes are silently dropped when no TCP client is connected.
TR-06  tx_disabled=True on port 2 prevents TCP→UART2 forwarding.
TR-07  Port 2 last_writer routing.
SN-01  Sniffer on port 2 emits packets with port==2.
SN-02  Sniffer emits {type:"timeout"} when slave doesn't respond.
SN-03  Broadcast packet (slave_id=0x00) is classified as master.
SN-04  Fast Modbus packets are classified by subcmd (master vs slave).
SN-05  Orphan response (no preceding request) is emitted as sender=="slave".
SN-06  After WS disconnect without stop, firmware stays alive.
GM-15  After switching from sniffer to tcp_bridge, data path works correctly.
GM-16  Double mode switch tcp_bridge→sniffer→tcp_bridge works end-to-end.
"""

import json
import socket
import struct
import threading
import time

import pytest

from conftest import build_gateway_fixture, _poll_tcp_connect
from packet_injector import (
    PacketInjector,
    inject_bytes,
    open_uart_socket,
    modbus_crc16,
    build_fc03_request,
)
from sniffer_helpers import _ws_connect, _collect_packets


# ---------------------------------------------------------------------------
# Module-level constants
# ---------------------------------------------------------------------------

GATEWAY_HOST = "127.0.0.1"
UART1_TCP_PORT = 5561                  # QEMU UART1 chardev TCP
UART2_TCP_PORT = 5562                  # QEMU UART2 chardev TCP
TRANSPARENT_PORT2_HOST_PORT = 50503    # QEMU hostfwd: guest 503  → host 50503
TRANSPARENT_PORT1_HOST_PORT = 50504    # QEMU hostfwd: guest 50504 → host 50504
TCP_CLIENT_RECONN_DELAY_MS = 1000      # Must match firmware tcp_client.c constant
SNIFFER_RESP_TIMEOUT_MS = 200          # Must match firmware sniffer.c constant


# ---------------------------------------------------------------------------
# Module-level transparent bridge fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def transparent_bridge_p2(api):
    """Port 2 transparent bridge fixture (server mode).

    Explicitly forces tx_disabled=False to prevent state leak from TR-06
    which sets tx_disabled=True on port 2.
    """
    # Step 1: verify UART2 chardev is reachable
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(3.0)
    try:
        probe.connect(("127.0.0.1", UART2_TCP_PORT))
        probe.close()
    except (ConnectionRefusedError, OSError, socket.timeout):
        probe.close()
        pytest.skip(
            f"Cannot connect to UART2 chardev TCP port {UART2_TCP_PORT}. "
            "QEMU may not expose UART2 as TCP in this configuration."
        )

    # Step 2: save original settings
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
    original_settings = resp.json()
    rs485_key = "rs485_2"

    try:
        # Step 3: disable port first to release UART driver
        resp = api.set_port_mode(2, "disabled")
        assert resp.status_code == 200, f"Failed to disable port 2: {resp.status_code}"
        time.sleep(0.3)

        # Step 4: apply bridge settings with tx_disabled=False explicitly
        port_settings = dict(original_settings.get(rs485_key, {}))
        port_settings["tx_disabled"] = False  # prevent state leak from TR-06
        port_settings["bridge"] = {
            "mode": "server",
            "port": 503,
            "ip": "0.0.0.0",
            "modbus": False,
        }
        resp = api.update_settings({rs485_key: port_settings})
        assert resp.status_code == 200, f"POST /settings failed: {resp.status_code}"
        assert resp.json().get("success") is True, f"Settings update failed: {resp.json()}"
        time.sleep(0.3)

        # Step 5: switch to tcp_bridge mode and wait for the port to open
        resp = api.set_port_mode(2, "tcp_bridge")
        assert resp.status_code == 200, f"set_port_mode(2, tcp_bridge) failed: {resp.status_code}"
        ready = _poll_tcp_connect("127.0.0.1", TRANSPARENT_PORT2_HOST_PORT, timeout=5.0)
        assert ready, f"Port {TRANSPARENT_PORT2_HOST_PORT} not ready within 5 s"

        yield None

    finally:
        # Restore original state
        api.set_port_mode(2, "disabled")
        time.sleep(0.3)
        restore_resp = api.update_settings(original_settings)
        if restore_resp.status_code != 200:
            print(f"✗ Failed to restore port 2 settings: HTTP {restore_resp.status_code}")
        original_mode = original_settings.get(rs485_key, {}).get("port_mode", "disabled")
        api.set_port_mode(2, original_mode)
        time.sleep(0.3)

# Port 1 transparent bridge fixture (server mode) — same config as test 25
transparent_bridge_p1 = build_gateway_fixture(
    port_num=1,
    tcp_host_port=TRANSPARENT_PORT1_HOST_PORT,
    uart_tcp_port=UART1_TCP_PORT,
    bridge_port=50504,
    modbus=False,
)


# ---------------------------------------------------------------------------
# Module-level baseline fixture
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    """Set rs485_1 and rs485_2 to a known state before any test in this module."""
    resp = api.update_settings({
        "rs485_1": {
            "tx_disabled": False,
            "baudrate": 9600,
            "stopbits": "1",
            "parity": "none",
            "databits": "8",
        },
        "rs485_2": {
            "tx_disabled": False,
            "baudrate": 9600,
            "stopbits": "1",
            "parity": "none",
            "databits": "8",
        },
    })
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code}"


# ---------------------------------------------------------------------------
# Helper: try to establish a TCP connection, return socket or None
# ---------------------------------------------------------------------------

def _try_connect_tcp(host: str, port: int, timeout: float = 3.0):
    """Attempt a TCP connection; return the socket or None on failure."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((host, port))
        return sock
    except (ConnectionRefusedError, OSError, socket.timeout):
        sock.close()
        return None


# ---------------------------------------------------------------------------
# Helper thread: connects to a UART chardev and echoes all received bytes back
# ---------------------------------------------------------------------------

class _UartEchoThread(threading.Thread):
    """Connects to a QEMU UART chardev TCP port and echoes all received bytes back."""

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
# Helper server: TCP echo server that accepts one client and echoes data
# ---------------------------------------------------------------------------

class _TcpEchoServer(threading.Thread):
    """TCP echo server that listens on a random free port and echoes all bytes.

    Suitable for transparent bridge client mode tests: firmware connects outbound
    to this server, which reflects all received bytes back to the firmware.
    """

    def __init__(self, host: str = "0.0.0.0"):
        super().__init__(daemon=True)
        self.host = host
        self._stop_event = threading.Event()
        self._server_sock = None
        self.port = None               # assigned after bind
        self._ready_event = threading.Event()
        self._accepted_event = threading.Event()  # set when firmware connects

    def run(self) -> None:
        """Bind, listen, accept one client, echo all data until stop()."""
        self._server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_sock.bind((self.host, 0))  # port=0 → OS picks a free port
        self.port = self._server_sock.getsockname()[1]
        self._server_sock.listen(1)
        self._server_sock.settimeout(0.5)
        self._ready_event.set()

        client_sock = None
        try:
            while not self._stop_event.is_set():
                try:
                    client_sock, _ = self._server_sock.accept()
                    self._accepted_event.set()
                    break
                except socket.timeout:
                    continue
            if client_sock is None:
                return

            client_sock.settimeout(0.5)
            while not self._stop_event.is_set():
                try:
                    data = client_sock.recv(256)
                    if not data:
                        break
                    client_sock.sendall(data)
                except socket.timeout:
                    continue
                except OSError:
                    break
        finally:
            if client_sock:
                try:
                    client_sock.close()
                except OSError:
                    pass
            try:
                self._server_sock.close()
            except OSError:
                pass

    def wait_ready(self, timeout: float = 5.0) -> bool:
        """Block until server is bound and listening."""
        return self._ready_event.wait(timeout=timeout)

    def wait_accepted(self, timeout: float = 10.0) -> bool:
        """Block until firmware has connected to the echo server."""
        return self._accepted_event.wait(timeout=timeout)

    def stop(self) -> None:
        """Signal the server to stop."""
        self._stop_event.set()
        if self._server_sock:
            try:
                self._server_sock.close()
            except OSError:
                pass


# ---------------------------------------------------------------------------
# Helper server: TCP reconnect server — accepts N connections sequentially
# ---------------------------------------------------------------------------

class _TcpReconnectServer(threading.Thread):
    """TCP server that accepts connections sequentially and tracks connection count.

    Each accepted connection is echoed until the caller calls close_current(),
    then the server waits for the next connection.
    """

    def __init__(self, host: str = "0.0.0.0"):
        super().__init__(daemon=True)
        self.host = host
        self._server_sock = None
        self.port = None
        self._ready_event = threading.Event()
        self._stop_event = threading.Event()
        self._lock = threading.Lock()
        self._connection_events = []   # list[threading.Event] — one per accepted connection
        self._current_client = None

    def run(self) -> None:
        """Bind, listen, accept connections in a loop, echoing each one."""
        self._server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_sock.bind((self.host, 0))
        self.port = self._server_sock.getsockname()[1]
        self._server_sock.listen(5)
        self._server_sock.settimeout(0.5)
        self._ready_event.set()

        while not self._stop_event.is_set():
            try:
                client_sock, _ = self._server_sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break

            # Record this new connection
            evt = threading.Event()
            evt.set()
            with self._lock:
                self._connection_events.append(evt)
                self._current_client = client_sock

            client_sock.settimeout(0.5)
            # Echo loop for this client until told to stop or client disconnects
            while not self._stop_event.is_set():
                try:
                    data = client_sock.recv(256)
                    if not data:
                        break
                    client_sock.sendall(data)
                except socket.timeout:
                    continue
                except OSError:
                    break

            with self._lock:
                if self._current_client is client_sock:
                    self._current_client = None
            try:
                client_sock.close()
            except OSError:
                pass

        try:
            self._server_sock.close()
        except OSError:
            pass

    def wait_ready(self, timeout: float = 5.0) -> bool:
        """Block until server is bound and listening."""
        return self._ready_event.wait(timeout=timeout)

    def wait_connection(self, n: int, timeout: float) -> bool:
        """Block until at least n connections have been accepted."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                count = len(self._connection_events)
            if count >= n:
                return True
            time.sleep(0.05)
        return False

    def close_current(self) -> None:
        """Close the current client connection to trigger firmware reconnect."""
        with self._lock:
            sock = self._current_client
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass

    def stop(self) -> None:
        """Signal the server to stop."""
        self._stop_event.set()
        if self._server_sock:
            try:
                self._server_sock.close()
            except OSError:
                pass


# ---------------------------------------------------------------------------
# Helper: configure firmware in transparent TCP client mode
# ---------------------------------------------------------------------------

def _setup_client_mode_bridge(api, port_num: int, bridge_ip: str, bridge_port: int,
                               uart_tcp_port: int) -> tuple:
    """Configure firmware for transparent TCP client mode.

    Returns (original_settings, rs485_key) for teardown.
    Raises AssertionError on failure.
    """
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
    original_settings = resp.json()
    rs485_key = f"rs485_{port_num}"

    # Disable port first to release UART driver
    resp = api.set_port_mode(port_num, "disabled")
    assert resp.status_code == 200, f"Failed to disable port {port_num}: {resp.status_code}"
    time.sleep(0.3)

    # Apply bridge settings: client mode
    port_settings = dict(original_settings.get(rs485_key, {}))
    port_settings["bridge"] = {
        "mode": "client",
        "port": bridge_port,
        "ip": bridge_ip,
        "modbus": False,
    }
    resp = api.update_settings({rs485_key: port_settings})
    assert resp.status_code == 200, f"POST /settings failed: {resp.status_code}"
    result = resp.json()
    assert result.get("success") is True, f"Settings update not successful: {result}"
    time.sleep(0.3)

    # Activate tcp_bridge mode
    resp = api.set_port_mode(port_num, "tcp_bridge")
    assert resp.status_code == 200, \
        f"POST /ports/{port_num}/mode tcp_bridge failed: {resp.status_code}"

    return original_settings, rs485_key


def _teardown_client_mode_bridge(api, port_num: int, original_settings: dict,
                                  rs485_key: str) -> None:
    """Restore firmware to original state after client mode test."""
    api.set_port_mode(port_num, "disabled")
    time.sleep(0.3)
    restore_resp = api.update_settings(original_settings)
    if restore_resp.status_code != 200:
        print(f"✗ Failed to restore settings: HTTP {restore_resp.status_code}")
    original_mode = original_settings.get(rs485_key, {}).get("port_mode", "disabled")
    api.set_port_mode(port_num, original_mode)
    time.sleep(0.3)


# ---------------------------------------------------------------------------
# Helper: collect echo bytes from TCP socket with deadline
# ---------------------------------------------------------------------------

def _collect_echo(sock: socket.socket, expected_len: int, timeout: float) -> bytes:
    """Receive bytes from sock until expected_len bytes arrive or timeout expires."""
    received = b""
    deadline = time.monotonic() + timeout
    while len(received) < expected_len and time.monotonic() < deadline:
        try:
            chunk = sock.recv(1024)
            if not chunk:
                break
            received += chunk
        except socket.timeout:
            continue
    return received


# ===========================================================================
# TR-01: Port 2 basic round-trip via transparent bridge
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_transparent_port2_basic_roundtrip(transparent_bridge_p2):
    """Send 16 arbitrary bytes through the port 2 transparent bridge and receive echo.

    Setup:
    - Port 2 configured as transparent bridge on bridge_port=503 (host 50503).
    - _UartEchoThread connects to UART2 chardev (port 5562) and echoes bytes.
    - TCP client connects to host port 50503, sends 16 bytes, expects echo back.
    """
    probe = _try_connect_tcp(GATEWAY_HOST, UART2_TCP_PORT, timeout=3.0)
    if probe is None:
        pytest.skip(f"UART2 chardev TCP port {UART2_TCP_PORT} not reachable")
    probe.close()

    echo_thread = _UartEchoThread(GATEWAY_HOST, UART2_TCP_PORT)
    echo_thread.start()
    assert echo_thread.wait_connected(timeout=5.0), \
        "UART echo thread could not connect to UART2 chardev"

    test_data = bytes(range(16))  # 16 distinct bytes: 0x00..0x0F
    tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_sock.settimeout(5.0)
    try:
        tcp_sock.connect((GATEWAY_HOST, TRANSPARENT_PORT2_HOST_PORT))
        tcp_sock.sendall(test_data)

        received = _collect_echo(tcp_sock, len(test_data), timeout=5.0)

        assert received == test_data, (
            f"Port 2 round-trip mismatch: sent={test_data.hex()!r}, got={received.hex()!r}"
        )
        print(f"✓ Port 2 transparent bridge round-trip: {len(test_data)} bytes echoed correctly")
    finally:
        tcp_sock.close()
        echo_thread.stop()
        echo_thread.join(timeout=3.0)


# ===========================================================================
# TR-02: Server reconnect after RST disconnect
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(30)
def test_transparent_server_reconnect_after_rst(transparent_bridge_p1):
    """After abrupt RST disconnect, new client connects and receives echo.

    Steps:
    1. Echo thread on UART1.
    2. client_a connects (no data sent).
    3. client_b connects, sends 4 bytes → becomes last_client_sock.
    4. Receive and verify b's echo on client_b.
    5. RST-close client_b (SO_LINGER trick).
    6. Wait 200ms for firmware to process the RST.
    7. Connect client_c; send 4 different bytes.
    8. Collect echo on client_c; verify it matches sent data.
    """
    probe = _try_connect_tcp(GATEWAY_HOST, UART1_TCP_PORT, timeout=3.0)
    if probe is None:
        pytest.skip(f"UART1 chardev TCP port {UART1_TCP_PORT} not reachable")
    probe.close()

    echo_thread = _UartEchoThread(GATEWAY_HOST, UART1_TCP_PORT)
    echo_thread.start()
    assert echo_thread.wait_connected(timeout=5.0), "Echo thread failed to connect"

    client_a = None
    client_b = None
    client_c = None
    try:
        client_a = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_a.settimeout(3.0)
        client_a.connect((GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT))
        time.sleep(0.05)  # allow server to accept A

        client_b = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_b.settimeout(5.0)
        client_b.connect((GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT))
        time.sleep(0.05)  # allow server to accept B

        data_b = b'\x11\x22\x33\x44'
        client_b.sendall(data_b)  # B is now last_client_sock in firmware

        # Verify B receives its own echo (confirms firmware tracks B as last_client_sock)
        received_b = _collect_echo(client_b, len(data_b), timeout=3.0)
        assert received_b == data_b, (
            f"client_b did not receive its echo before RST: got {received_b.hex()!r}"
        )

        # RST-close client_b — sends RST instead of FIN to abruptly terminate
        client_b.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                            struct.pack('ii', 1, 0))
        client_b.close()
        client_b = None

        # Wait for firmware to process the RST
        time.sleep(0.2)

        # Connect client_c and verify it becomes new last_client_sock
        client_c = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_c.settimeout(5.0)
        client_c.connect((GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT))
        time.sleep(0.05)

        data_c = b'\xAA\xBB\xCC\xDD'
        client_c.sendall(data_c)

        received_c = _collect_echo(client_c, len(data_c), timeout=5.0)
        assert received_c == data_c, (
            f"client_c did not receive its echo after RST reconnect: "
            f"sent={data_c.hex()!r}, got={received_c.hex()!r}"
        )
        print(f"✓ RST reconnect: client_c received {len(received_c)} bytes correctly")
    finally:
        for sock in (client_a, client_c):
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass
        if client_b is not None:
            try:
                client_b.close()
            except OSError:
                pass
        echo_thread.stop()
        echo_thread.join(timeout=3.0)


# ===========================================================================
# TR-03: Large payload with embedded null bytes
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_transparent_large_payload_with_nulls(transparent_bridge_p1):
    """1024-byte payload with embedded null bytes is forwarded correctly.

    Uses payload = bytes([(i % 251) for i in range(1024)]) which cycles
    values 0..250, inserting 0x00 at indices 0, 251, 502, 753, 1004 (5 null bytes).
    Payload is sent in one shot; the firmware forwards UART data immediately
    without waiting for idle timeout, so burst load does not cause RX overflow.
    """
    probe = _try_connect_tcp(GATEWAY_HOST, UART1_TCP_PORT, timeout=3.0)
    if probe is None:
        pytest.skip(f"UART1 chardev TCP port {UART1_TCP_PORT} not reachable")
    probe.close()

    echo_thread = _UartEchoThread(GATEWAY_HOST, UART1_TCP_PORT)
    echo_thread.start()
    assert echo_thread.wait_connected(timeout=5.0), "Echo thread failed to connect"

    # Payload cycles 0..250; indices 0, 251, 502, 753, 1004 are 0x00 (5 embedded nulls)
    payload = bytes([i % 251 for i in range(1024)])
    assert payload.count(0) == 5, "Payload must contain exactly 5 null bytes at expected indices"

    tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_sock.settimeout(5.0)
    try:
        tcp_sock.connect((GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT))
        # Send entire payload at once; firmware now forwards UART_DATA events immediately
        # without buffering, so burst load no longer causes RX overflow.
        tcp_sock.sendall(payload)

        received = _collect_echo(tcp_sock, len(payload), timeout=10.0)

        assert len(received) == len(payload), (
            f"Large payload length mismatch: expected {len(payload)}, got {len(received)}"
        )
        assert received == payload, (
            f"Large payload content mismatch (first diff at byte "
            f"{next(i for i, (a, b) in enumerate(zip(received, payload)) if a != b)})"
        )
        print(f"✓ Large payload ({len(payload)} bytes, 5 nulls) forwarded correctly")
    finally:
        tcp_sock.close()
        echo_thread.stop()
        echo_thread.join(timeout=3.0)


# ===========================================================================
# TR-04: Client mode reconnect after server closes connection
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(40)
def test_transparent_client_mode_reconnect(api):
    """Firmware reconnects within TCP_CLIENT_RECONN_DELAY_MS after server closes connection.

    A _TcpReconnectServer accepts connections sequentially.  The firmware is
    configured in client mode; after the server closes the first connection the
    firmware should reconnect and data flow must be restored on the second
    connection.
    """
    probe = _try_connect_tcp(GATEWAY_HOST, UART1_TCP_PORT, timeout=3.0)
    if probe is None:
        pytest.skip(f"UART1 chardev TCP port {UART1_TCP_PORT} not reachable")
    probe.close()

    reconnect_server = _TcpReconnectServer(host="0.0.0.0")
    reconnect_server.start()
    assert reconnect_server.wait_ready(timeout=5.0), \
        "Reconnect server did not bind within 5 s"
    server_port = reconnect_server.port
    print(f"Reconnect server listening on 0.0.0.0:{server_port}")

    # Capture original settings before any firmware state changes so teardown
    # can always restore the port even if _setup_client_mode_bridge fails mid-way.
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
    original_settings = resp.json()
    rs485_key = "rs485_1"

    uart_sock = None
    try:
        # Configure firmware in client mode pointing to the reconnect server
        _setup_client_mode_bridge(
            api, port_num=1,
            bridge_ip="10.0.2.2",   # QEMU host IP
            bridge_port=server_port,
            uart_tcp_port=UART1_TCP_PORT,
        )

        # Wait for firmware's first connection to the server
        conn1_ok = reconnect_server.wait_connection(n=1, timeout=10.0)
        assert conn1_ok, "Firmware did not connect to reconnect server within 10 s"

        # Connect to UART1 chardev and test first data exchange
        uart_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        uart_sock.settimeout(5.0)
        uart_sock.connect((GATEWAY_HOST, UART1_TCP_PORT))

        data1 = b'\x01\x02\x03\x04'
        uart_sock.sendall(data1)

        received1 = _collect_echo(uart_sock, len(data1), timeout=5.0)
        assert received1 == data1, (
            f"First connection: echo mismatch: sent={data1.hex()!r}, got={received1.hex()!r}"
        )

        # Close the current server-side connection to trigger firmware reconnect
        reconnect_server.close_current()

        # Wait for firmware to reconnect (2× TCP_CLIENT_RECONN_DELAY_MS for safety)
        reconn_wait = (TCP_CLIENT_RECONN_DELAY_MS * 2) / 1000.0
        time.sleep(reconn_wait)

        # Wait for second connection
        conn2_ok = reconnect_server.wait_connection(n=2, timeout=5.0)
        assert conn2_ok, (
            f"Firmware did not reconnect within 5 s after server close "
            f"(waited {reconn_wait:.1f} s for reconnect delay)"
        )

        # Test second data exchange — confirms reconnect was successful
        data2 = b'\xAA\xBB\xCC\xDD'
        uart_sock.sendall(data2)

        received2 = _collect_echo(uart_sock, len(data2), timeout=5.0)
        assert received2 == data2, (
            f"Second connection: echo mismatch: sent={data2.hex()!r}, got={received2.hex()!r}"
        )
        print(f"✓ Client mode reconnect: both connections exchanged data correctly")

    finally:
        if uart_sock:
            uart_sock.close()
        _teardown_client_mode_bridge(api, 1, original_settings, rs485_key)
        reconnect_server.stop()
        reconnect_server.join(timeout=3.0)


# ===========================================================================
# TR-05: UART bytes dropped when no TCP client is connected
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_transparent_no_client_uart_bytes_dropped(transparent_bridge_p1, api):
    """UART bytes are silently dropped when no TCP client is connected; firmware stays alive.

    The transparent_bridge_p1 fixture sets up the bridge but nobody connects
    to the TCP port.  Raw bytes injected via UART1 chardev must not crash the
    firmware; the API must remain responsive afterwards.
    """
    # Inject 16 bytes directly into UART1 chardev — no TCP client is listening
    try:
        inject_bytes(port=1, data=bytes(range(16)))
    except OSError as exc:
        pytest.skip(f"UART1 chardev not reachable: {exc}")

    # Allow firmware to process the bytes and (silently) discard them
    time.sleep(0.2)

    # Verify API is still alive
    resp = api.get_info()
    assert resp.status_code == 200, (
        f"API became unresponsive after injecting bytes with no TCP client: "
        f"HTTP {resp.status_code}"
    )
    print("✓ Firmware survived UART bytes with no TCP client connected")


# ===========================================================================
# TR-06: tx_disabled=True on port 2 prevents TCP→UART2 forwarding
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_transparent_tx_disabled_port2(api):
    """tx_disabled=True on port 2 prevents firmware from forwarding TCP→UART2.

    Steps:
    1. Save original settings, apply bridge_port=503 (server, non-modbus), tx_disabled=True.
    2. Activate tcp_bridge mode on port 2.
    3. Start _UartEchoThread on UART2 — will echo anything it receives.
    4. TCP client connects to host port 50503, sends 8 bytes.
    5. Assert recv raises socket.timeout (no bytes came back): because tx_disabled=True
       → firmware did not forward TCP→UART2 → echo thread received nothing.
    6. Teardown: close all sockets, restore settings and port mode.
    """
    # Save original settings
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
    original_settings = resp.json()

    echo_thread = None
    tcp_sock = None
    try:
        # Disable port 2 first
        resp = api.set_port_mode(2, "disabled")
        assert resp.status_code == 200, f"Failed to disable port 2: {resp.status_code}"
        time.sleep(0.3)

        # Apply bridge settings with tx_disabled=True
        port2_settings = dict(original_settings.get("rs485_2", {}))
        port2_settings["tx_disabled"] = True
        port2_settings["bridge"] = {
            "mode": "server",
            "port": 503,
            "ip": "0.0.0.0",
            "modbus": False,
        }
        resp = api.update_settings({"rs485_2": port2_settings})
        assert resp.status_code == 200, f"POST /settings failed: {resp.status_code}"
        assert resp.json().get("success") is True
        time.sleep(0.3)

        # Activate tcp_bridge mode on port 2
        resp = api.set_port_mode(2, "tcp_bridge")
        assert resp.status_code == 200, f"Failed to activate tcp_bridge on port 2"

        ready = _poll_tcp_connect(GATEWAY_HOST, TRANSPARENT_PORT2_HOST_PORT, timeout=5.0)
        if not ready:
            pytest.skip(f"Port 50503 not ready after 5 s — UART2 chardev may be unavailable")

        # Start echo thread on UART2 chardev
        probe = _try_connect_tcp(GATEWAY_HOST, UART2_TCP_PORT, timeout=3.0)
        if probe is None:
            pytest.skip(f"UART2 chardev TCP port {UART2_TCP_PORT} not reachable")
        probe.close()

        echo_thread = _UartEchoThread(GATEWAY_HOST, UART2_TCP_PORT)
        echo_thread.start()
        assert echo_thread.wait_connected(timeout=5.0), \
            "Echo thread could not connect to UART2 chardev"

        # TCP client connects and sends 8 bytes
        tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp_sock.connect((GATEWAY_HOST, TRANSPARENT_PORT2_HOST_PORT))
        tcp_sock.sendall(b'\x01\x02\x03\x04\x05\x06\x07\x08')

        # With tx_disabled=True, no bytes reach UART2, so echo thread sends nothing back
        tcp_sock.settimeout(2.0)
        got_data = False
        try:
            data = tcp_sock.recv(64)
            if data:
                got_data = True
        except socket.timeout:
            pass  # expected — no echo received

        assert not got_data, (
            "Received data back when tx_disabled=True — firmware forwarded TCP→UART2 unexpectedly"
        )
        print("✓ tx_disabled=True on port 2: no bytes forwarded to UART2 as expected")

    finally:
        if tcp_sock:
            try:
                tcp_sock.close()
            except OSError:
                pass
        if echo_thread:
            echo_thread.stop()
            echo_thread.join(timeout=3.0)
        # Restore original state
        api.set_port_mode(2, "disabled")
        time.sleep(0.3)
        api.update_settings(original_settings)
        original_mode = original_settings.get("rs485_2", {}).get("port_mode", "disabled")
        api.set_port_mode(2, original_mode)
        time.sleep(0.3)


# ===========================================================================
# TR-07: Port 2 last_writer routing
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_transparent_port2_last_writer_routing(transparent_bridge_p2):
    """Port 2 last_writer routing: echo goes to the client that sent data.

    Sequential scenario:
    1. client_a connects (no data sent).
    2. client_b connects and sends 4 bytes — B becomes last_client_sock.
    3. Echo thread echoes B's 4 bytes back to the firmware.
    4. Firmware routes the echo to last sender (client B).
    5. client_b receives its own 4 bytes back; client_a receives nothing.
    """
    probe = _try_connect_tcp(GATEWAY_HOST, UART2_TCP_PORT, timeout=3.0)
    if probe is None:
        pytest.skip(f"UART2 chardev TCP port {UART2_TCP_PORT} not reachable")
    probe.close()

    echo_thread = _UartEchoThread(GATEWAY_HOST, UART2_TCP_PORT)
    echo_thread.start()
    assert echo_thread.wait_connected(timeout=5.0), "Echo thread failed to connect"

    sock_a = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_a.settimeout(3.0)
    sock_b = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_b.settimeout(5.0)
    try:
        sock_a.connect((GATEWAY_HOST, TRANSPARENT_PORT2_HOST_PORT))
        time.sleep(0.05)  # allow server to accept A before B connects
        sock_b.connect((GATEWAY_HOST, TRANSPARENT_PORT2_HOST_PORT))
        time.sleep(0.05)  # allow server to accept B before data is sent

        data_b = b'\xAA\xBB\xCC\xDD'
        sock_b.sendall(data_b)  # B is now last writer

        # B should receive its own echo
        received_b = _collect_echo(sock_b, len(data_b), timeout=3.0)
        assert received_b == data_b, (
            f"Client B did not receive its echo: got {received_b.hex()!r}"
        )

        # A should receive nothing (not the last writer)
        sock_a.settimeout(0.5)
        try:
            data_a = sock_a.recv(64)
            assert not data_a, (
                f"Client A must receive nothing (last-writer is B), got: {data_a.hex()!r}"
            )
        except socket.timeout:
            pass  # expected: A gets nothing

        print("✓ Port 2 last-writer routing: B received its echo, A got nothing")
    finally:
        sock_a.close()
        sock_b.close()
        echo_thread.stop()
        echo_thread.join(timeout=3.0)


# ===========================================================================
# SN-01: Sniffer on port 2 emits packets with port==2
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(30)
def test_sniffer_port2_packets_have_port2(api):
    """Sniffer started on port 2 must emit packets with port==2.

    Injects Modbus traffic into UART2 chardev, collects ≥5 packets from
    the WS sniffer and verifies that all of them have port==2.
    """
    resp = api.get_info()
    assert resp.status_code == 200
    original_mode = resp.json().get("rs485_2", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        resp = api.set_port_mode(2, "sniffer")
        assert resp.status_code == 200, f"Failed to set port 2 to sniffer: {resp.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=2, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 2)

            packets = _collect_packets(
                ws,
                min_count=5,
                timeout_sec=20,
                filter_fn=lambda p: p.get("type") == "packet",
            )

        assert len(packets) >= 5, (
            f"Expected >=5 packets from port 2 sniffer but got {len(packets)}"
        )
        assert all(p["port"] == 2 for p in packets), (
            f"Some packets have wrong port field: {[p['port'] for p in packets]}"
        )
        print(f"✓ All {len(packets)} sniffer packets have port==2")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 2}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        resp = api.set_port_mode(2, original_mode)
        assert resp.status_code == 200, f"Failed to restore port 2 mode: {resp.status_code}"


# ===========================================================================
# SN-02: Sniffer emits {type:"timeout"} when slave doesn't respond
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_sniffer_timeout_packet_when_slave_silent(api):
    """When slave doesn't respond, sniffer emits a {type:'timeout'} packet.

    Injects only an FC03 request with no response.  Sniffer buffers the
    request and starts SNIFFER_RESP_TIMEOUT_MS timer; after it fires the
    sniffer emits a timeout packet with slave_id and function matching the
    injected request.
    """
    resp = api.get_info()
    assert resp.status_code == 200
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    uart_sock = None
    try:
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Failed to set port 1 to sniffer: {resp.status_code}"
        time.sleep(0.5)

        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.2)  # allow WS to be fully ready

        # Inject only a request — no response follows, so sniffer must time out
        request_frame = build_fc03_request(slave=1, start_addr=0, reg_count=1)
        try:
            uart_sock = open_uart_socket(port=1)
            inject_bytes(port=1, data=request_frame, sock=uart_sock)
        except OSError as exc:
            pytest.skip(f"UART1 chardev not reachable: {exc}")

        # Collect timeout packets; give 3× the timeout for jitter
        timeout_packets = _collect_packets(
            ws,
            min_count=1,
            timeout_sec=3.0,
            filter_fn=lambda p: p.get("type") == "timeout",
        )

        assert len(timeout_packets) >= 1, (
            f"Expected >=1 timeout packet but got 0 within 3 s; "
            f"SNIFFER_RESP_TIMEOUT_MS={SNIFFER_RESP_TIMEOUT_MS}"
        )
        timeout_pkt = timeout_packets[0]
        assert timeout_pkt.get("slave_id") == 1, (
            f"Timeout packet slave_id mismatch: expected 1, got {timeout_pkt.get('slave_id')}"
        )
        assert timeout_pkt.get("function") == 3, (
            f"Timeout packet function mismatch: expected 3, got {timeout_pkt.get('function')}"
        )
        print(f"✓ Sniffer emitted timeout packet: slave_id={timeout_pkt['slave_id']}, "
              f"function={timeout_pkt['function']}")

    finally:
        if uart_sock:
            try:
                uart_sock.close()
            except OSError:
                pass
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
        resp = api.set_port_mode(1, original_mode)
        assert resp.status_code == 200, f"Failed to restore port 1 mode: {resp.status_code}"


# ===========================================================================
# SN-03: Broadcast packet (slave_id=0x00) classified as master
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_sniffer_broadcast_classified_as_master(api):
    """Broadcast packet (slave_id=0x00) is classified as sender=='master'.

    Injects a broadcast FC03 request (slave=0x00) via UART1 chardev and
    verifies the sniffer emits it as sender=='master'.
    """
    resp = api.get_info()
    assert resp.status_code == 200
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    uart_sock = None
    try:
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Failed to set port 1 to sniffer: {resp.status_code}"
        time.sleep(0.5)

        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.2)

        # Build broadcast FC03 request: slave=0x00
        broadcast_frame = build_fc03_request(slave=0, start_addr=0, reg_count=1)
        try:
            uart_sock = open_uart_socket(port=1)
            inject_bytes(port=1, data=broadcast_frame, sock=uart_sock)
        except OSError as exc:
            pytest.skip(f"UART1 chardev not reachable: {exc}")

        # Collect packets until one with slave_id==0 is found
        broadcast_pkts = _collect_packets(
            ws,
            min_count=1,
            timeout_sec=5.0,
            filter_fn=lambda p: (
                p.get("type") == "packet" and p.get("slave_id") == 0
            ),
        )

        assert len(broadcast_pkts) >= 1, (
            "Expected >=1 packet with slave_id==0 (broadcast) but none received"
        )
        pkt = broadcast_pkts[0]
        assert pkt.get("sender") == "master", (
            f"Broadcast packet must have sender=='master', got {pkt.get('sender')!r}"
        )
        print(f"✓ Broadcast packet (slave_id=0) classified as sender=='master'")

    finally:
        if uart_sock:
            try:
                uart_sock.close()
            except OSError:
                pass
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
        resp = api.set_port_mode(1, original_mode)
        assert resp.status_code == 200, f"Failed to restore port 1 mode: {resp.status_code}"


# ===========================================================================
# SN-04: Fast Modbus packets classified by subcmd (master vs slave)
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_sniffer_fast_modbus_classification(api):
    """Fast Modbus packets are classified correctly by subcmd field.

    subcmd=0x01 → fm_is_slave_subcmd(0x01) is False → sender=='master'
    subcmd=0x03 → fm_is_slave_subcmd(0x03) is True  → sender=='slave'

    FM packet wire format: 0xFF + [0xFD, FC, subcmd, ...] + CRC
    The 0xFF prefix is arbitration; CRC is computed on the stripped portion.
    """
    resp = api.get_info()
    assert resp.status_code == 200
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    uart_sock = None
    try:
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Failed to set port 1 to sniffer: {resp.status_code}"
        time.sleep(0.5)

        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.2)

        try:
            uart_sock = open_uart_socket(port=1)
        except OSError as exc:
            pytest.skip(f"UART1 chardev not reachable: {exc}")

        # Build FM master packet (subcmd=0x01, which is NOT a slave subcmd)
        stripped_pdu_master = bytes([0xFD, 0x46, 0x01, 0x00, 0x00, 0x00])
        crc_master = modbus_crc16(stripped_pdu_master).to_bytes(2, "little")
        fm_master_packet = bytes([0xFF]) + stripped_pdu_master + crc_master

        inject_bytes(port=1, data=fm_master_packet, sock=uart_sock)

        # Collect until we find function==0x46 with sender=='master'
        master_fm_pkts = _collect_packets(
            ws,
            min_count=1,
            timeout_sec=5.0,
            filter_fn=lambda p: (
                p.get("type") == "packet"
                and p.get("function") == 0x46
                and p.get("sender") == "master"
            ),
        )
        assert len(master_fm_pkts) >= 1, (
            "Expected >=1 FM packet with function==0x46 and sender=='master' "
            "(subcmd=0x01) but none received"
        )

        # Build FM slave response packet (subcmd=0x03, which IS a slave subcmd)
        stripped_pdu_slave = bytes([0xFD, 0x46, 0x03, 0x00, 0x00, 0x00])
        crc_slave = modbus_crc16(stripped_pdu_slave).to_bytes(2, "little")
        fm_slave_packet = bytes([0xFF]) + stripped_pdu_slave + crc_slave

        inject_bytes(port=1, data=fm_slave_packet, sock=uart_sock)

        # Collect until we find function==0x46 with sender=='slave'
        slave_fm_pkts = _collect_packets(
            ws,
            min_count=1,
            timeout_sec=5.0,
            filter_fn=lambda p: (
                p.get("type") == "packet"
                and p.get("function") == 0x46
                and p.get("sender") == "slave"
            ),
        )
        assert len(slave_fm_pkts) >= 1, (
            "Expected >=1 FM packet with function==0x46 and sender=='slave' "
            "(subcmd=0x03) but none received"
        )
        print(
            "✓ Fast Modbus classification: master subcmd=0x01 → sender==master, "
            "slave subcmd=0x03 → sender==slave"
        )

    finally:
        if uart_sock:
            try:
                uart_sock.close()
            except OSError:
                pass
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
        resp = api.set_port_mode(1, original_mode)
        assert resp.status_code == 200, f"Failed to restore port 1 mode (SN-04): {resp.status_code}"


# ===========================================================================
# SN-05: Orphan response (no preceding request) emitted as sender=='slave'
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_sniffer_orphan_response(api):
    """A slave response without preceding request (orphan) is emitted as sender=='slave'.

    When the sniffer is in SNIFF_IDLE state and receives a valid FC03 response
    (classified as DIRECTION_RESPONSE by classify_direction), the sniffer emits
    it as a slave packet without any state change (stays SNIFF_IDLE).

    FC03 response classification: len >= 5, len == 5 + data[2], data[2] even and > 0.
    For 1 register: data[2]=2, len=7 → DIRECTION_RESPONSE.
    """
    resp = api.get_info()
    assert resp.status_code == 200
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    uart_sock = None
    try:
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Failed to set port 1 to sniffer: {resp.status_code}"
        time.sleep(0.5)

        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.2)

        try:
            uart_sock = open_uart_socket(port=1)
        except OSError as exc:
            pytest.skip(f"UART1 chardev not reachable: {exc}")

        # Build orphan FC03 response: slave=1, FC=3, bytecount=2, reg value=0x1234
        # len=7: slave(1)+FC(1)+bytecount(1)+data(2)+CRC(2) = 7 → DIRECTION_RESPONSE
        pdu = bytes([0x01, 0x03, 0x02, 0x12, 0x34])
        orphan_response = pdu + modbus_crc16(pdu).to_bytes(2, "little")
        assert len(orphan_response) == 7

        inject_bytes(port=1, data=orphan_response, sock=uart_sock)

        # Collect until packet with slave_id==1, function==3, sender=='slave' found
        orphan_pkts = _collect_packets(
            ws,
            min_count=1,
            timeout_sec=5.0,
            filter_fn=lambda p: (
                p.get("type") == "packet"
                and p.get("slave_id") == 1
                and p.get("function") == 3
                and p.get("sender") == "slave"
            ),
        )
        assert len(orphan_pkts) >= 1, (
            "Expected >=1 orphan response packet with slave_id==1, function==3, "
            "sender=='slave' but none received"
        )
        pkt = orphan_pkts[0]
        assert pkt.get("crc_valid") is True, (
            f"Orphan response packet must have crc_valid==True, got {pkt.get('crc_valid')}"
        )
        print("✓ Orphan response: emitted as sender=='slave' with crc_valid=True")

    finally:
        if uart_sock:
            try:
                uart_sock.close()
            except OSError:
                pass
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
        resp = api.set_port_mode(1, original_mode)
        assert resp.status_code == 200, f"Failed to restore port 1 mode (SN-05): {resp.status_code}"


# ===========================================================================
# SN-06: WS disconnect without stop — firmware stays alive
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_sniffer_ws_disconnect_firmware_stays_alive(api):
    """After WS disconnect without stop command, firmware remains responsive.

    Confirms the server handles abrupt WS closure gracefully:
    the API must remain reachable and /sniffer/status must return 200.
    """
    resp = api.get_info()
    assert resp.status_code == 200
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Failed to set port 1 to sniffer: {resp.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)

            # Collect a few packets to confirm sniffer is active before disconnect
            packets = _collect_packets(ws, min_count=2, timeout_sec=10)
            assert len(packets) >= 2, (
                f"Expected >=2 packets before disconnect but got {len(packets)}"
            )

            # Stop the ping thread first to avoid concurrent recv/ping race
            if stop_ping is not None:
                stop_ping.set()

            # Abruptly close WS without sending stop command
            try:
                ws.sock.close()
            except Exception:
                try:
                    ws.close()
                except Exception:
                    pass
            ws = None
            stop_ping = None

        # Allow firmware to detect the closed connection
        time.sleep(0.5)

        # Verify API is still responsive
        resp = api.get_info()
        assert resp.status_code == 200, (
            f"API became unresponsive after abrupt WS close: HTTP {resp.status_code}"
        )

        # Verify /sniffer/status endpoint still works
        status_resp = api.get_sniffer_status()
        assert status_resp.status_code == 200, (
            f"/sniffer/status failed after abrupt WS close: HTTP {status_resp.status_code}"
        )
        print("✓ Firmware responsive after abrupt WS disconnect without stop command")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.close()
            except Exception:
                pass
        resp = api.set_port_mode(1, original_mode)
        assert resp.status_code == 200, f"Failed to restore port 1 mode (SN-06): {resp.status_code}"


# ===========================================================================
# GM-15: After switching from sniffer to tcp_bridge, data path works
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(40)
def test_sniffer_to_tcp_bridge_data_path_restored(api):
    """After switching from sniffer to tcp_bridge, data path works correctly.

    Steps:
    1. Save original port 1 settings.
    2. Apply bridge config (bridge_port=50504, mode=server, modbus=False).
    3. Switch to sniffer mode; confirm ≥3 packets received via WS.
    4. Switch port 1 to tcp_bridge mode.
    5. Start UART echo thread + TCP client; verify 8-byte round-trip.
    6. Teardown: close all, restore settings.
    """
    # Save original settings
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
    original_settings = resp.json()
    rs485_key = "rs485_1"

    ws = None
    stop_ping = None
    echo_thread = None
    tcp_sock = None
    try:
        # Disable port 1 first
        resp = api.set_port_mode(1, "disabled")
        assert resp.status_code == 200, f"Failed to disable port 1: {resp.status_code}"
        time.sleep(0.3)

        # Apply bridge config for transparent bridge on port 50504
        port1_settings = dict(original_settings.get(rs485_key, {}))
        port1_settings["bridge"] = {
            "mode": "server",
            "port": 50504,
            "ip": "0.0.0.0",
            "modbus": False,
        }
        resp = api.update_settings({rs485_key: port1_settings})
        assert resp.status_code == 200, f"POST /settings failed: {resp.status_code}"
        assert resp.json().get("success") is True
        time.sleep(0.3)

        # Phase 1: sniffer mode — inject traffic and verify packets arrive
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Failed to set sniffer mode: {resp.status_code}"
        time.sleep(0.5)

        probe = _try_connect_tcp(GATEWAY_HOST, UART1_TCP_PORT, timeout=3.0)
        if probe is None:
            pytest.skip(f"UART1 chardev TCP port {UART1_TCP_PORT} not reachable")
        probe.close()

        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)
            packets = _collect_packets(ws, min_count=3, timeout_sec=15)
            assert len(packets) >= 3, (
                f"Expected >=3 packets in sniffer mode but got {len(packets)}"
            )

        # Stop sniffer — send stop command and close WS
        stop_ping.set()
        try:
            ws.send(json.dumps({"cmd": "stop", "port": 1}))
        except Exception:
            pass
        try:
            ws.close()
        except Exception:
            pass
        ws = None
        stop_ping = None

        # Phase 2: switch to tcp_bridge mode
        resp = api.set_port_mode(1, "tcp_bridge")
        assert resp.status_code == 200, f"Failed to switch to tcp_bridge: {resp.status_code}"
        ready = _poll_tcp_connect(GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT, timeout=5.0)
        assert ready, "Port 50504 not ready after switching from sniffer to tcp_bridge"

        # Verify data path with echo round-trip
        echo_thread = _UartEchoThread(GATEWAY_HOST, UART1_TCP_PORT)
        echo_thread.start()
        assert echo_thread.wait_connected(timeout=5.0), \
            "Echo thread could not connect to UART1 chardev after mode switch"

        tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp_sock.settimeout(5.0)
        tcp_sock.connect((GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT))

        test_data = b'\xDE\xAD\xBE\xEF\x01\x02\x03\x04'
        tcp_sock.sendall(test_data)

        received = _collect_echo(tcp_sock, len(test_data), timeout=5.0)
        assert received == test_data, (
            f"GM-15 round-trip after sniffer→tcp_bridge: "
            f"sent={test_data.hex()!r}, got={received.hex()!r}"
        )
        print("✓ GM-15: sniffer→tcp_bridge mode switch: data path restored correctly")

    finally:
        if tcp_sock:
            try:
                tcp_sock.close()
            except OSError:
                pass
        if echo_thread:
            echo_thread.stop()
            echo_thread.join(timeout=3.0)
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
        # Restore original state
        api.set_port_mode(1, "disabled")
        time.sleep(0.3)
        restore_resp = api.update_settings(original_settings)
        if restore_resp.status_code != 200:
            print(f"✗ Failed to restore settings: HTTP {restore_resp.status_code}")
        original_mode = original_settings.get(rs485_key, {}).get("port_mode", "disabled")
        api.set_port_mode(1, original_mode)
        time.sleep(0.3)


# ===========================================================================
# GM-16: Double mode switch tcp_bridge→sniffer→tcp_bridge works end-to-end
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(60)
def test_tcp_bridge_sniffer_tcp_bridge_roundtrip(api):
    """Double mode switch tcp_bridge→sniffer→tcp_bridge works end-to-end.

    Phases:
    1. tcp_bridge: verify echo round-trip (8 bytes via UART1 + TCP client).
    2. sniffer: inject traffic, verify ≥3 packets received.
    3. tcp_bridge again: verify echo round-trip once more.
    """
    # Save original settings
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
    original_settings = resp.json()
    rs485_key = "rs485_1"

    ws = None
    stop_ping = None
    echo_thread = None
    tcp_sock = None
    try:
        # Disable port 1 first
        resp = api.set_port_mode(1, "disabled")
        assert resp.status_code == 200, f"Failed to disable port 1: {resp.status_code}"
        time.sleep(0.3)

        # Apply bridge config for transparent bridge on port 50504
        port1_settings = dict(original_settings.get(rs485_key, {}))
        port1_settings["bridge"] = {
            "mode": "server",
            "port": 50504,
            "ip": "0.0.0.0",
            "modbus": False,
        }
        resp = api.update_settings({rs485_key: port1_settings})
        assert resp.status_code == 200, f"POST /settings failed: {resp.status_code}"
        assert resp.json().get("success") is True
        time.sleep(0.3)

        # Check UART1 chardev is accessible
        probe = _try_connect_tcp(GATEWAY_HOST, UART1_TCP_PORT, timeout=3.0)
        if probe is None:
            pytest.skip(f"UART1 chardev TCP port {UART1_TCP_PORT} not reachable")
        probe.close()

        # ----------------------------------------------------------------
        # Phase 1: tcp_bridge — verify round-trip
        # ----------------------------------------------------------------
        resp = api.set_port_mode(1, "tcp_bridge")
        assert resp.status_code == 200, f"Phase 1: Failed to set tcp_bridge: {resp.status_code}"
        ready = _poll_tcp_connect(GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT, timeout=5.0)
        assert ready, "Phase 1: Port 50504 not ready"

        echo_thread = _UartEchoThread(GATEWAY_HOST, UART1_TCP_PORT)
        echo_thread.start()
        assert echo_thread.wait_connected(timeout=5.0), \
            "Phase 1: Echo thread could not connect to UART1 chardev"

        tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp_sock.settimeout(5.0)
        tcp_sock.connect((GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT))

        data_phase1 = b'\x11\x22\x33\x44\x55\x66\x77\x88'
        tcp_sock.sendall(data_phase1)
        received1 = _collect_echo(tcp_sock, len(data_phase1), timeout=5.0)
        assert received1 == data_phase1, (
            f"Phase 1 round-trip failed: sent={data_phase1.hex()!r}, got={received1.hex()!r}"
        )
        print(f"✓ Phase 1 (tcp_bridge): {len(received1)} bytes echoed correctly")

        # Close the TCP client and stop the echo thread before mode switch
        tcp_sock.close()
        tcp_sock = None
        echo_thread.stop()
        echo_thread.join(timeout=3.0)
        echo_thread = None

        # ----------------------------------------------------------------
        # Phase 2: sniffer — inject traffic and verify packets
        # ----------------------------------------------------------------
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Phase 2: Failed to set sniffer: {resp.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)
            packets = _collect_packets(ws, min_count=3, timeout_sec=15)
            assert len(packets) >= 3, (
                f"Phase 2: Expected >=3 sniffer packets but got {len(packets)}"
            )

        stop_ping.set()
        try:
            ws.send(json.dumps({"cmd": "stop", "port": 1}))
        except Exception:
            pass
        try:
            ws.close()
        except Exception:
            pass
        ws = None
        stop_ping = None
        print(f"✓ Phase 2 (sniffer): {len(packets)} packets received")

        # ----------------------------------------------------------------
        # Phase 3: tcp_bridge again — verify round-trip still works
        # ----------------------------------------------------------------
        resp = api.set_port_mode(1, "tcp_bridge")
        assert resp.status_code == 200, f"Phase 3: Failed to set tcp_bridge: {resp.status_code}"
        ready = _poll_tcp_connect(GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT, timeout=5.0)
        assert ready, "Phase 3: Port 50504 not ready after second tcp_bridge switch"

        echo_thread = _UartEchoThread(GATEWAY_HOST, UART1_TCP_PORT)
        echo_thread.start()
        assert echo_thread.wait_connected(timeout=5.0), \
            "Phase 3: Echo thread could not connect to UART1 chardev"

        tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp_sock.settimeout(5.0)
        tcp_sock.connect((GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT))

        data_phase3 = b'\xAA\xBB\xCC\xDD\x01\x02\x03\x04'
        tcp_sock.sendall(data_phase3)
        received3 = _collect_echo(tcp_sock, len(data_phase3), timeout=5.0)
        assert received3 == data_phase3, (
            f"Phase 3 round-trip failed: sent={data_phase3.hex()!r}, got={received3.hex()!r}"
        )
        print(f"✓ Phase 3 (tcp_bridge again): {len(received3)} bytes echoed correctly")
        print("✓ GM-16: tcp_bridge→sniffer→tcp_bridge double mode switch works end-to-end")

    finally:
        if tcp_sock:
            try:
                tcp_sock.close()
            except OSError:
                pass
        if echo_thread:
            echo_thread.stop()
            echo_thread.join(timeout=3.0)
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
        # Restore original state
        api.set_port_mode(1, "disabled")
        time.sleep(0.3)
        restore_resp = api.update_settings(original_settings)
        if restore_resp.status_code != 200:
            print(f"✗ Failed to restore settings: HTTP {restore_resp.status_code}")
        original_mode = original_settings.get(rs485_key, {}).get("port_mode", "disabled")
        api.set_port_mode(1, original_mode)
        time.sleep(0.3)
