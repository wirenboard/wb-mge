"""E2E tests for the transparent TCP bridge — Wave 3.

Requires QEMU with UART1 exposed as TCP port 5561 and guest port 503 forwarded
to host port 50503.  The transparent bridge mode forwards raw bytes between the
TCP client and the serial interface without any Modbus framing.

Coverage:
12. Basic round-trip — 16 arbitrary bytes TCP → serial → TCP (echo via UART chardev).
14. Zero-byte edge case — null byte + real data; connection stays open throughout.
15. Client mode — firmware connects outbound to a Python TCP echo server on the host.
16. Single-client cap (C7) — a second client is rejected (block-new): A keeps being
    served while B is disconnected (EOF).
17. Server-sends-first (B3) — serial → TCP reaches a client that never transmitted.

Test #13 ("multiple clients — last-writer routing") was removed: it predates the
single-client cap and asserted that two clients could be admitted at once, which
block-new (max_connections == 1) makes impossible — test #16 asserts the correct
behaviour instead.
"""

import socket
import threading
import time

import pytest

from conftest import build_gateway_fixture, _connect_ready_bridge


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


class _TcpEchoServer(threading.Thread):
    """TCP echo server that listens on a random free port and echoes all received bytes.

    Suitable for transparent bridge client mode tests: firmware connects outbound
    to this server, which reflects all received bytes back to the firmware.
    """

    def __init__(self, host: str = "0.0.0.0"):
        super().__init__(daemon=True)
        self.host = host
        self._stop_event = threading.Event()
        self._server_sock = None
        self.port = None              # assigned after bind
        self._ready_event = threading.Event()
        self._accepted_event = threading.Event()  # set when firmware connects


    def run(self) -> None:
        """Bind, listen, accept one client, echo all data until stop()."""
        self._server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_sock.bind((self.host, 0))   # port=0 → OS picks a free port
        self.port = self._server_sock.getsockname()[1]
        self._server_sock.listen(1)
        self._server_sock.settimeout(0.5)
        self._ready_event.set()

        client_sock = None
        try:
            # Accept one client connection (firmware)
            while not self._stop_event.is_set():
                try:
                    client_sock, _ = self._server_sock.accept()
                    self._accepted_event.set()   # firmware connected
                    break
                except socket.timeout:
                    continue
            if client_sock is None:
                return

            client_sock.settimeout(0.5)
            # Echo loop
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
# Test #12: Basic round-trip — arbitrary bytes TCP → serial → TCP
# ---------------------------------------------------------------------------

@pytest.mark.qemu
# 120s: marker covers function-scoped setup+teardown whose retrying 30s HTTP calls are slow under QEMU load
@pytest.mark.timeout(120)
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
    # _connect_ready_bridge() can raise if the guest never admits a connection; it
    # MUST be inside the try so the finally still stops the echo thread. A leaked
    # daemon echo thread keeps the single-client UART chardev socket occupied and
    # turns every later UART connect into a spurious skip.
    tcp_sock = None
    try:
        tcp_sock = _connect_ready_bridge(GATEWAY_HOST, TRANSPARENT_HOST_PORT, timeout=15.0)
        tcp_sock.settimeout(5.0)
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
        # Stop the echo thread FIRST — that is the invariant this whole rework
        # protects (a leaked daemon thread wedges the single-client UART chardev).
        # Only then close the socket, wrapped so a close error cannot skip the stop.
        echo_thread.stop()
        echo_thread.join(timeout=3.0)
        if tcp_sock is not None:
            try:
                tcp_sock.close()
            except OSError:
                pass


# ---------------------------------------------------------------------------
# Test #14: Zero bytes edge case
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(120)
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

    # _connect_ready_bridge() inside the try so a raise still stops the echo thread
    # (see test_transparent_basic_roundtrip): a leaked echo thread wedges the UART
    # chardev and cascades into spurious skips.
    tcp_sock = None
    try:
        tcp_sock = _connect_ready_bridge(GATEWAY_HOST, TRANSPARENT_HOST_PORT, timeout=15.0)
        tcp_sock.settimeout(5.0)
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
        # Stop the echo thread FIRST — that is the invariant this whole rework
        # protects (a leaked daemon thread wedges the single-client UART chardev).
        # Only then close the socket, wrapped so a close error cannot skip the stop.
        echo_thread.stop()
        echo_thread.join(timeout=3.0)
        if tcp_sock is not None:
            try:
                tcp_sock.close()
            except OSError:
                pass


# ---------------------------------------------------------------------------
# Test #15: Client mode — firmware connects outbound to a TCP echo server
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_transparent_client_mode(api):
    """Transparent bridge client mode: firmware connects outbound to a TCP echo server.

    Setup:
    - Start a Python TCP echo server on the host at a random free port.
    - Configure firmware: bridge.mode=client, bridge.ip=10.0.2.2, bridge.port=<echo_port>.
    - Activate tcp_bridge mode — firmware connects outbound to the echo server.
    - Connect to UART1 chardev (port 5561), send bytes, expect echo back.

    Data flow:
      UART1_chardev → firmware_UART1 → firmware_tcp_client → echo_server
                   ← firmware_UART1 ← firmware_tcp_client ←

    10.0.2.2 is the QEMU user-network host IP (standard QEMU convention).
    """
    # Step 1: verify UART1 chardev is reachable
    probe = _try_connect_tcp(GATEWAY_HOST, UART1_TCP_PORT, timeout=3.0)
    if probe is None:
        pytest.skip(f"UART1 chardev TCP port {UART1_TCP_PORT} not reachable")
    probe.close()

    # Step 2: start echo server on host
    echo_server = _TcpEchoServer(host="0.0.0.0")
    echo_server.start()
    assert echo_server.wait_ready(timeout=5.0), "Echo server did not bind within 5 s"
    echo_port = echo_server.port
    print(f"Echo server listening on 0.0.0.0:{echo_port}")

    original_settings = None
    rs485_key = None
    uart_sock = None
    try:
        # Step 3: configure firmware for client mode
        original_settings, rs485_key = _setup_client_mode_bridge(
            api, port_num=1,
            bridge_ip="10.0.2.2",   # QEMU host IP
            bridge_port=echo_port,
            uart_tcp_port=UART1_TCP_PORT,
        )

        # Step 4: wait for firmware to connect to echo server (outbound connection).
        # The firmware tcp_client_task tries to connect every ~1 s after tcp_bridge starts.
        # We poll echo_server.wait_accepted() which signals when firmware accepted.
        firmware_connected = echo_server.wait_accepted(timeout=10.0)
        assert firmware_connected, (
            "Firmware did not connect to the echo server within 10 s. "
            "Check that 10.0.2.2 is reachable from QEMU (QEMU user-network host IP)."
        )

        # The firmware logs "Successfully connected" inside connect_socket() *before*
        # bumping active_connections — so for a brief window tcp_client_connected()
        # still returns ESP_FAIL and any UART RX in that window is silently dropped.
        # echo_server.wait_accepted unblocks at the kernel-level accept, which can
        # land us right in that window. A 200 ms breather is enough to let the
        # firmware's tcp_client_task finish setting active_connections=1.
        time.sleep(0.2)

        # Step 5: connect to UART1 chardev and test data flow
        uart_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        uart_sock.settimeout(5.0)
        uart_sock.connect((GATEWAY_HOST, UART1_TCP_PORT))

        test_data = b'\xCA\xFE\xBA\xBE'
        uart_sock.sendall(test_data)

        received = b''
        deadline = time.monotonic() + 5.0
        while len(received) < len(test_data) and time.monotonic() < deadline:
            try:
                chunk = uart_sock.recv(64)
                if not chunk:
                    break
                received += chunk
            except socket.timeout:
                continue

        assert received == test_data, (
            f"Client mode round-trip failed: sent={test_data.hex()!r}, got={received.hex()!r}"
        )
        print(f"✓ Transparent bridge client mode: {len(test_data)} bytes echoed via 10.0.2.2:{echo_port}")

    finally:
        # Stopping echo_server MUST NOT depend on the earlier steps: a leaked daemon
        # echo thread wedges the single-client chardev and cascades skips downstream.
        # uart_sock.close() and _teardown_client_mode_bridge (bare api.set_port_mode/
        # update_settings, each a 30 s-read-timeout /settings request that can ReadTimeout
        # under QEMU load) could otherwise throw and skip the stop(). Guard + nest.
        if uart_sock is not None:
            try:
                uart_sock.close()
            except OSError:
                pass
        try:
            if original_settings is not None:
                _teardown_client_mode_bridge(api, 1, original_settings, rs485_key)
        finally:
            echo_server.stop()
            echo_server.join(timeout=3.0)


# ---------------------------------------------------------------------------
# Test #16: Single-client cap (C7) — a new client is rejected, the old one stays
# ---------------------------------------------------------------------------
#
# NOTE: like every test in this module this needs QEMU (UART1 chardev on port
# 5561 + the transparent-bridge hostfwd). It was NOT executed in the C7 change
# environment — added and py_compile-checked only; run under the QEMU harness.

@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_transparent_single_client_cap_block_new(transparent_bridge):
    """A second client is rejected: transparent server keeps the client it already serves.

    C7 policy (block-new): the transparent bridge routes to a single socket, so the
    acceptor caps connections at 1 and rejects any newcomer while a client is served.

    Sequential scenario:
    1. Client A connects and is the sole (served) client.
    2. Client B connects — the firmware rejects B (close) and keeps A.
    3. B observes the rejection: recv() returns b'' (clean EOF) or ConnectionReset.
    4. A stays fully functional: its bytes round-trip TCP -> serial -> TCP via the echo.
    """
    probe = _try_connect_tcp(GATEWAY_HOST, UART1_TCP_PORT, timeout=3.0)
    if probe is None:
        pytest.skip(f"UART1 chardev TCP port {UART1_TCP_PORT} not reachable")
    probe.close()

    echo_thread = _UartEchoThread(GATEWAY_HOST, UART1_TCP_PORT)
    echo_thread.start()
    assert echo_thread.wait_connected(timeout=5.0), "Echo thread failed to connect"

    # _connect_ready_bridge() for A goes inside the try so a raise still stops the
    # echo thread (a leaked echo thread wedges the single-client UART chardev). B
    # stays a BARE socket — the test asserts the single-slot cap REJECTS it, so it
    # must not be retried to admission.
    sock_a = None
    sock_b = None
    try:
        sock_a = _connect_ready_bridge(GATEWAY_HOST, TRANSPARENT_HOST_PORT, timeout=15.0)
        sock_a.settimeout(5.0)
        sock_b = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock_b.settimeout(5.0)
        # 1. Client A connects and becomes the served socket (on accept).
        time.sleep(0.2)   # let the firmware accept A and spawn its receiver

        # 2. Client B connects — the firmware must reject B and keep A.
        sock_b.connect((GATEWAY_HOST, TRANSPARENT_HOST_PORT))
        time.sleep(0.3)   # let the firmware reject B

        # 3. B must observe the rejection: EOF (b'') or a reset. A timeout here
        #    would mean B was admitted → the single-client cap failed.
        sock_b.settimeout(3.0)
        b_rejected = False
        try:
            data_b = sock_b.recv(64)
            b_rejected = (data_b == b'')   # clean EOF from the acceptor's close()
        except ConnectionResetError:
            b_rejected = True              # RST is also an acceptable rejection signal
        except socket.timeout:
            b_rejected = False
        assert b_rejected, "Client B must be rejected while client A is still served (block-new)"

        # 4. A must stay fully served: its bytes round-trip through the bridge.
        data_a = b'\x51\x52\x53\x54'
        sock_a.sendall(data_a)

        received_a = b''
        deadline = time.monotonic() + 3.0
        while len(received_a) < len(data_a) and time.monotonic() < deadline:
            try:
                chunk = sock_a.recv(64)
                if not chunk:
                    break
                received_a += chunk
            except socket.timeout:
                continue

        assert received_a == data_a, (
            f"Client A (the retained client) did not round-trip its data: got {received_a.hex()!r}"
        )
        print("✓ Single-client cap (C7): A retained and served, B rejected")
    finally:
        # Stop the echo thread FIRST (the invariant), then close the sockets, wrapped.
        echo_thread.stop()
        echo_thread.join(timeout=3.0)
        for _s in (sock_a, sock_b):
            if _s is not None:
                try:
                    _s.close()
                except OSError:
                    pass


# ---------------------------------------------------------------------------
# Test #17: B3 — server-sends-first: serial -> TCP to a client that never sent
# ---------------------------------------------------------------------------
#
# NOTE: like every test in this module this needs QEMU (UART1 chardev on port
# 5561 + the transparent-bridge hostfwd). It was NOT executed in the B3 change
# environment — added and py_compile-checked only; run under the QEMU harness.
# It needs no new fixture: it reuses `transparent_bridge` and writes into the
# UART1 chardev directly (the same socket the echo thread uses), instead of
# echoing, so that the serial side is the one that speaks first.

@pytest.mark.qemu
# 165 s, not 120 s: an item's pytest-timeout budget covers setup + call + TEARDOWN, and
# module-scoped fixtures are torn down inside the LAST item of the module. This is that
# item, so it also pays conftest's _restore_rs485_settings teardown — up to two bounded
# POST /settings plus a settle window (2 x 20.1 s + 1 s = 41.2 s, see _RS485_HTTP_TIMEOUT).
# This module's own _baseline (:46) is setup-only and transparent_bridge is function-scoped
# (built by conftest.build_gateway_fixture), so the conftest restore is the whole module
# teardown. 120 s body + 45 s teardown allowance.
@pytest.mark.timeout(165)
def test_transparent_serial_to_tcp_client_never_sent(transparent_bridge):
    """Serial data reaches a client that connected but never transmitted (B3).

    Regression for the reported bug: with the bridge in server mode, bytes arriving
    on RS-485 were dropped with "No client connected" / "Failed to send data to TCP
    from serial" until the TCP client happened to send something first. Swapping who
    spoke first made it work, because the client socket was only registered on receive.

    Sequential scenario:
    1. A TCP client connects to the transparent server and sends NOTHING.
    2. The host injects bytes into the UART1 chardev (the serial side speaks first).
    3. Those bytes must arrive on the silent client's TCP socket.
    """
    probe = _try_connect_tcp(GATEWAY_HOST, UART1_TCP_PORT, timeout=3.0)
    if probe is None:
        pytest.skip(f"UART1 chardev TCP port {UART1_TCP_PORT} not reachable")
    probe.close()

    # Drive the UART chardev by hand: no echo thread, this test needs the serial
    # side to originate traffic rather than reflect it. Both sockets are created
    # inside the try (and _connect_ready_bridge, which can raise, with them) so the
    # finally always runs and never depends on statement order.
    uart_sock = None
    tcp_sock = None
    try:
        tcp_sock = _connect_ready_bridge(GATEWAY_HOST, TRANSPARENT_HOST_PORT, timeout=15.0)
        tcp_sock.settimeout(5.0)
        uart_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        uart_sock.settimeout(5.0)
        uart_sock.connect((GATEWAY_HOST, UART1_TCP_PORT))

        # 1. Client connects and stays completely silent.
        time.sleep(0.3)   # let the firmware accept it and spawn its receiver

        # 2. The serial side speaks first.
        test_data = b'\x5A\xA5\x01\x02\x03\x04'
        uart_sock.sendall(test_data)

        # 3. The silent client must receive the serial bytes.
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
            "serial->TCP must reach a client that never sent data "
            f"(B3): injected={test_data.hex()!r}, got={received.hex()!r}"
        )
        print(f"✓ B3: serial->TCP delivered {len(test_data)} bytes to a silent client")
    finally:
        for _s in (tcp_sock, uart_sock):
            if _s is not None:
                try:
                    _s.close()
                except OSError:
                    pass
