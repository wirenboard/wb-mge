"""Regression test for Modbus-TCP gateway RTU fragmentation bug.

When a Modbus RTU response exceeds the ESP32 UART RXFIFO_FULL threshold
(120 bytes), the UART driver fires two UART_DATA events:
  1. UART_DATA without timeout_flag  — when the RXFIFO fills to 120 bytes
  2. UART_DATA with timeout_flag     — when remaining bytes arrive and bus goes idle

The regression: the UART_DATA handler was changed to call receive_handler()
immediately on EVERY UART_DATA event, instead of waiting for timeout_flag.
This caused process_data_from_serial() to be invoked on the first 120-byte
fragment, whose CRC check fails (incomplete frame), and the response was
silently dropped.  The TCP client received nothing.

FC03 reading LARGE_REG_COUNT (60) registers produces a 125-byte RTU response,
which is guaranteed to split across two UART_DATA events.

Existing tests only read 1–2 registers (9-byte response), so they never trigger
this code path.

Fix (applied in main/bridge/serial.c): receive_handler is now called only when
timeout_flag is set AND serial_desc->wait_for_idle is true (Modbus TCP gateway
mode). Transparent bridge mode (wait_for_idle=false) still forwards immediately
on every UART_DATA event.
"""

import qemu_ports
import socket
import struct
import threading
import time

import pytest

from conftest import require_uart_chardev
from modbus_helpers import make_mbap_request, recv_modbus_tcp_response


# ---------------------------------------------------------------------------
# Module-level constants
# ---------------------------------------------------------------------------

GATEWAY_HOST_PORT = qemu_ports.GATEWAY_HOST_PORT  # QEMU hostfwd: slot gateway host port -> guest 502
UART1_TCP_PORT = qemu_ports.UART1_TCP_PORT  # QEMU UART1 chardev TCP
UART_FIFO_FULL_THRESHOLD = 120      # ESP32 UART RXFIFO_FULL threshold in bytes
# FC03 response size = 1(slave_id) + 1(FC) + 1(byte_count) + count*2(data) + 2(CRC) = 5 + count*2
# For count=60: 5 + 120 = 125 bytes → exceeds FIFO threshold by 5 bytes
LARGE_REG_COUNT = 60
FAKE_VALUE = 0x1234


# ---------------------------------------------------------------------------
# Private helpers (redefined locally; originals are private in rtu_slave_helpers)
# ---------------------------------------------------------------------------

def _crc16(data: bytes) -> int:
    """Calculate Modbus CRC16."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else crc >> 1
    return crc


def _build_rtu_fc03_response(slave_id: int, count: int, value: int) -> bytes:
    """Build RTU FC03 response frame: slave_id + 0x03 + byte_count + data + CRC."""
    payload = struct.pack(f'>{count}H', *([value] * count))
    body = bytes([slave_id, 0x03, len(payload)]) + payload
    crc = _crc16(body)
    return body + bytes([crc & 0xFF, crc >> 8])


# ---------------------------------------------------------------------------
# Custom RTU slave that sends large responses in two chunks
# ---------------------------------------------------------------------------

class ModbusRtuSlaveChunkedThread(threading.Thread):
    """Custom RTU slave for UART1 TCP chardev.

    Reads fixed-length 8-byte RTU requests (slave_id + FC + addr(2) + count(2) + CRC(2)).
    For FC03 responses that exceed UART_FIFO_FULL_THRESHOLD bytes, sends the response
    in two socket.send() calls with a short 2 ms pause between them
    (needed to prevent QEMU loopback from coalescing the two TCP segments into one):
      - Chunk 1: response[:UART_FIFO_FULL_THRESHOLD]
      - Chunk 2: response[UART_FIFO_FULL_THRESHOLD:]

    This simulates the UART hardware behaviour when the response overflows the RXFIFO:
    the gateway receives two UART_DATA events (first without timeout_flag, second with it).

    For responses at or below the threshold, sends via a single socket.sendall().
    """

    # Fixed RTU request length: slave_id(1) + FC(1) + addr(2) + count(2) + CRC(2)
    RTU_REQUEST_LEN = 8

    def __init__(self, host: str = '127.0.0.1', port: int = UART1_TCP_PORT,
                 fake_value: int = FAKE_VALUE, connect_timeout: float = 5.0):
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.fake_value = fake_value
        self.connect_timeout = connect_timeout
        self.request_count = 0      # incremented for each valid request received
        self.connected = False      # True once TCP connection is established
        self._stop_event = threading.Event()
        self._sock = None

    def run(self) -> None:
        """Connect to QEMU UART chardev and serve RTU requests until stop()."""
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            # Disable Nagle to prevent the OS from coalescing the two chunk sends
            self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self._sock.settimeout(self.connect_timeout)
            self._sock.connect((self.host, self.port))
            self.connected = True
            self._sock.settimeout(0.5)  # short recv timeout to check _stop_event
        except OSError:
            return

        buf = b''
        while not self._stop_event.is_set():
            try:
                chunk = self._sock.recv(256)
                if not chunk:
                    break
                buf += chunk
                buf = self._process_buffer(buf)
            except socket.timeout:
                continue
            except OSError:
                break

        try:
            self._sock.close()
        except OSError:
            pass

    def _process_buffer(self, buf: bytes) -> bytes:
        """Parse all complete 8-byte RTU requests and send responses; return leftover bytes."""
        while len(buf) >= self.RTU_REQUEST_LEN:
            slave_id = buf[0]
            fc = buf[1]
            addr = (buf[2] << 8) | buf[3]
            count = (buf[4] << 8) | buf[5]
            crc_recv = (buf[7] << 8) | buf[6]   # RTU CRC is little-endian
            crc_calc = _crc16(buf[:6])

            if crc_recv != crc_calc:
                # CRC mismatch: discard one byte and re-sync
                buf = buf[1:]
                continue

            response = self._build_response(slave_id, fc, addr, count)
            if response and self._sock:
                self._send_response(response)

            self.request_count += 1
            buf = buf[self.RTU_REQUEST_LEN:]

        return buf

    def _build_response(self, slave_id: int, fc: int, addr: int, count: int):
        """Build an RTU response for FC03; return exception 0x01 for other FCs."""
        if fc == 0x03:
            return _build_rtu_fc03_response(slave_id, count, self.fake_value)
        # Unsupported FC: return exception 0x01
        body = bytes([slave_id, fc | 0x80, 0x01])
        crc = _crc16(body)
        return body + bytes([crc & 0xFF, crc >> 8])

    def _send_response(self, response: bytes) -> None:
        """Send RTU response, splitting at UART_FIFO_FULL_THRESHOLD if it exceeds threshold.

        When total length > UART_FIFO_FULL_THRESHOLD:
          - First send() delivers exactly UART_FIFO_FULL_THRESHOLD bytes
          - Second send() delivers the remainder immediately (no sleep)
        This replicates the UART hardware RXFIFO_FULL + RXFIFO_TOUT event sequence.

        When total length <= UART_FIFO_FULL_THRESHOLD: single sendall().
        """
        try:
            if len(response) > UART_FIFO_FULL_THRESHOLD:
                # Send first chunk, then yield to the OS so QEMU's event loop
                # processes the first TCP segment before the second arrives.
                # Without this pause, both sends may be coalesced into a single
                # epoll/select call inside QEMU on the loopback interface, meaning
                # all 125 bytes arrive as one delivery and no FIFO split occurs.
                self._sock.send(response[:UART_FIFO_FULL_THRESHOLD])
                time.sleep(0.002)   # 2 ms: enough for QEMU event loop, well below UART idle timeout
                self._sock.send(response[UART_FIFO_FULL_THRESHOLD:])
            else:
                self._sock.sendall(response)
        except OSError:
            pass

    # ------------------------------------------------------------------
    # Control API
    # ------------------------------------------------------------------

    def stop(self) -> None:
        """Signal the thread to stop and close the socket."""
        self._stop_event.set()
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass

    def wait_connected(self, timeout: float = 5.0) -> bool:
        """Block until connected (or timeout). Returns True if connected."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.connected:
                return True
            time.sleep(0.05)
        return False


# ---------------------------------------------------------------------------
# Module-level baseline fixture
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    resp = api.update_settings({
        "rs485_1": {
            "tx_disabled": False,   # gateway must forward bytes to UART
            "baudrate": 9600,
            "stopbits": "1",
            "parity": "none",
            "databits": "8",
        }
    })
    assert resp.status_code == 200, (
        f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    )


# ---------------------------------------------------------------------------
# Gateway fixture (no built-in slave; each test starts its own chunked slave)
# ---------------------------------------------------------------------------

@pytest.fixture
def gateway_port(api, is_qemu):
    """Set up Modbus TCP gateway on RS-485 port 1, without starting an RTU slave.

    Configures port 1 in tcp_bridge mode with modbus=True (Modbus TCP gateway),
    waits for the gateway TCP port to bind, then yields. The caller is responsible
    for starting its own ModbusRtuSlaveChunkedThread.

    Teardown: disables port 1 and restores original settings.
    """
    # Verify UART1 chardev is reachable before proceeding. Only a reachability
    # question, so the probe socket is closed immediately.
    require_uart_chardev(UART1_TCP_PORT, is_qemu).close()

    # Save original settings for teardown
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
    original_settings = resp.json()

    try:
        # Disable port first to release the UART driver
        resp = api.set_port_mode(1, "disabled")
        assert resp.status_code == 200, f"Failed to disable port 1: {resp.status_code}"
        time.sleep(0.3)

        # Apply RS-485 config with Modbus TCP gateway settings
        port_settings = dict(original_settings.get("rs485_1", {}))
        port_settings["bridge"] = {
            "mode": "server",
            "port": 502,
            "ip": "0.0.0.0",
            "modbus": True,   # Modbus TCP gateway mode (not transparent bridge)
        }
        resp = api.update_settings({"rs485_1": port_settings})
        assert resp.status_code == 200, f"POST /settings failed: {resp.status_code}"
        result = resp.json()
        assert result.get("success") is True, f"Settings update not successful: {result}"
        time.sleep(0.3)

        # Switch to tcp_bridge mode and wait for the gateway TCP port to bind
        resp = api.set_port_mode(1, "tcp_bridge")
        assert resp.status_code == 200, f"POST /ports/1/mode tcp_bridge failed: {resp.status_code}"

        # Poll until gateway TCP port is reachable (instead of fixed sleep)
        deadline = time.monotonic() + 5.0
        ready = False
        while time.monotonic() < deadline:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.5)
            try:
                s.connect(("127.0.0.1", GATEWAY_HOST_PORT))
                s.close()
                ready = True
                break
            except (ConnectionRefusedError, OSError, socket.timeout):
                s.close()
                time.sleep(0.1)
        assert ready, f"Gateway did not start listening on host port {GATEWAY_HOST_PORT} within 5 s"

        yield  # test runs here; tests manage their own slave

    finally:
        # Restore original mode and settings
        api.set_port_mode(1, "disabled")
        time.sleep(0.3)
        restore_resp = api.update_settings(original_settings)
        if restore_resp.status_code != 200:
            print(f"✗ Failed to restore settings: HTTP {restore_resp.status_code}")
        original_mode = original_settings.get("rs485_1", {}).get("port_mode", "disabled")
        api.set_port_mode(1, original_mode)
        time.sleep(0.3)


# ---------------------------------------------------------------------------
# Test 1: Baseline — small response (always passes regardless of the bug)
# ---------------------------------------------------------------------------

@pytest.mark.qemu
# 55 s, not 30 s: an item's pytest-timeout budget covers SETUP as well as the call, and
# this is the first item of the module, so it pays the module-scoped _baseline above
# (:222, one POST /settings). When this file is run on its own it additionally pays
# conftest's once-per-session rs485 snapshot (one bounded GET /settings, 20.1 s, see
# _RS485_HTTP_TIMEOUT) — in a full-suite run that lands on the very first item of the
# session (00_test_heap_session.py) instead. 30 s body + 20.1 s snapshot + slack.
@pytest.mark.timeout(55)
def test_gateway_small_response_baseline(gateway_port):
    """FC03 read 2 registers → 9-byte response: well below FIFO threshold.

    Passes regardless of the RTU fragmentation bug because the entire
    response fits within one UART_DATA event (no RXFIFO_FULL overflow).
    Used as a sanity check that the gateway and RTU slave infrastructure works.

    Verifies:
    - TCP response received within 5 seconds.
    - Transaction ID in response matches the sent TID.
    - Function code is 0x03 (no exception).
    - Byte count = count * 2 (2 registers → 4 bytes of data).
    - Both register values equal FAKE_VALUE.
    """
    count = 2  # 9-byte RTU response: well below UART_FIFO_FULL_THRESHOLD
    txid = 0x0001

    slave = ModbusRtuSlaveChunkedThread(
        host="127.0.0.1",
        port=UART1_TCP_PORT,
        fake_value=FAKE_VALUE,
    )
    slave.start()
    try:
        assert slave.wait_connected(timeout=5.0), (
            "Chunked RTU slave failed to connect to UART1 chardev within 5 s"
        )

        request = make_mbap_request(txid, 1, 0x03, 0x0000, count)

        gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        gw_sock.settimeout(5.0)
        try:
            gw_sock.connect(("127.0.0.1", GATEWAY_HOST_PORT))
            gw_sock.sendall(request)

            response = recv_modbus_tcp_response(gw_sock, time.monotonic() + 5.0)

            resp_txid, resp_proto, resp_length = struct.unpack('>HHH', response[:6])
            assert resp_txid == txid, (
                f"TID mismatch: sent {txid:#06x}, got {resp_txid:#06x}"
            )
            assert resp_proto == 0, f"Protocol ID must be 0, got {resp_proto}"

            pdu = response[6:]
            assert len(pdu) >= 3, f"PDU too short: {pdu.hex()!r}"
            assert pdu[1] == 0x03, f"FC mismatch: expected 0x03, got {pdu[1]:#04x}"
            byte_count = pdu[2]
            assert byte_count == count * 2, (
                f"Byte count mismatch: expected {count * 2}, got {byte_count}"
            )

            registers = struct.unpack(f'>{count}H', pdu[3:3 + byte_count])
            for i, val in enumerate(registers):
                assert val == FAKE_VALUE, (
                    f"Register[{i}] value mismatch: expected {FAKE_VALUE:#06x}, got {val:#06x}"
                )

            print(
                f"✓ Baseline small response: txid={txid:#06x} count={count} "
                f"regs={[hex(v) for v in registers]} slave_requests={slave.request_count}"
            )
        finally:
            gw_sock.close()
    finally:
        slave.stop()
        slave.join(timeout=3.0)


# ---------------------------------------------------------------------------
# Test 2: Large response — regression test (FAILS with bug, PASSES after fix)
# ---------------------------------------------------------------------------

@pytest.mark.qemu
# 75 s, not 30 s: an item's pytest-timeout budget covers setup + call + TEARDOWN, and
# module-scoped fixtures are torn down inside the LAST item of the module. This is that
# item, so it also pays conftest's _restore_rs485_settings teardown — up to two bounded
# POST /settings plus a settle window (2 x 20.1 s + 1 s = 41.2 s, see _RS485_HTTP_TIMEOUT).
# 30 s body + 45 s teardown allowance.
@pytest.mark.timeout(75)
def test_gateway_large_response_fifo_split(gateway_port):
    """Regression test for RTU fragmentation bug.

    FC03 reads LARGE_REG_COUNT (60) holding registers → 125-byte RTU response.
    The custom slave sends this response in two chunks split at UART_FIFO_FULL_THRESHOLD
    (120 bytes): two separate socket.send() calls without any delay between them.

    This simulates the UART RXFIFO_FULL event followed by RXFIFO_TOUT:
    - Chunk 1 (120 bytes, no timeout): fires UART_DATA without timeout_flag
    - Chunk 2 (5 bytes, idle bus): fires UART_DATA with timeout_flag

    With the regression (serial.c calls receive_handler on every UART_DATA):
    - Chunk 1 triggers process_data_from_serial(120 bytes) → CRC fails → silent drop
    - Chunk 2 triggers process_data_from_serial(5 bytes) → CRC fails → silent drop
    → TCP client receives NO response → test FAILS with TimeoutError

    With the fix (serial.c only calls receive_handler when timeout_flag is set):
    - Both chunks accumulate in buffer_ctx
    - timeout_flag on chunk 2 triggers process_data_from_serial(125 bytes) → CRC passes
    → TCP client receives correct response → test PASSES

    Verifies (on success):
    - TCP response received within 5 seconds.
    - Transaction ID in response matches the sent TID.
    - Function code is 0x03 (no exception).
    - Byte count = LARGE_REG_COUNT * 2 (120 bytes of register data).
    - All LARGE_REG_COUNT register values equal FAKE_VALUE.
    """
    txid = 0x0039
    expected_rtu_size = 5 + LARGE_REG_COUNT * 2   # 125 bytes

    slave = ModbusRtuSlaveChunkedThread(
        host="127.0.0.1",
        port=UART1_TCP_PORT,
        fake_value=FAKE_VALUE,
    )
    slave.start()
    try:
        assert slave.wait_connected(timeout=5.0), (
            "Chunked RTU slave failed to connect to UART1 chardev within 5 s"
        )

        request = make_mbap_request(txid, 1, 0x03, 0x0000, LARGE_REG_COUNT)

        gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        gw_sock.settimeout(5.0)
        try:
            gw_sock.connect(("127.0.0.1", GATEWAY_HOST_PORT))
            gw_sock.sendall(request)

            try:
                response = recv_modbus_tcp_response(gw_sock, time.monotonic() + 5.0)
            except TimeoutError:
                pytest.fail(
                    f"Gateway did not respond to FC03({LARGE_REG_COUNT} regs) within timeout.\n"
                    f"RTU response is {expected_rtu_size} bytes, split into two UART_DATA events\n"
                    f"at FIFO threshold ({UART_FIFO_FULL_THRESHOLD} bytes).\n"
                    f"REGRESSION: serial.c calls receive_handler on partial data instead of "
                    f"waiting for timeout_flag."
                )

            resp_txid, resp_proto, resp_length = struct.unpack('>HHH', response[:6])
            assert resp_txid == txid, (
                f"TID mismatch: sent {txid:#06x}, got {resp_txid:#06x}"
            )
            assert resp_proto == 0, f"Protocol ID must be 0, got {resp_proto}"

            pdu = response[6:]
            assert len(pdu) >= 3, f"PDU too short: {pdu.hex()!r}"
            assert pdu[1] == 0x03, (
                f"FC mismatch: expected 0x03, got {pdu[1]:#04x} "
                f"(0x{pdu[1]:02X} suggests Modbus exception)"
            )
            byte_count = pdu[2]
            assert byte_count == LARGE_REG_COUNT * 2, (
                f"Byte count mismatch: expected {LARGE_REG_COUNT * 2}, got {byte_count}"
            )

            registers = struct.unpack(
                f'>{LARGE_REG_COUNT}H', pdu[3:3 + LARGE_REG_COUNT * 2]
            )
            for i, val in enumerate(registers):
                assert val == FAKE_VALUE, (
                    f"Register[{i}] value mismatch: expected {FAKE_VALUE:#06x}, got {val:#06x}"
                )

            print(
                f"✓ Large response ({expected_rtu_size} bytes, split at "
                f"{UART_FIFO_FULL_THRESHOLD}): txid={txid:#06x} "
                f"count={LARGE_REG_COUNT} all regs={FAKE_VALUE:#06x} "
                f"slave_requests={slave.request_count}"
            )
        finally:
            gw_sock.close()
    finally:
        slave.stop()
        slave.join(timeout=3.0)
