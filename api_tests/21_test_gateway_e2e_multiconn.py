"""End-to-end multi-connection tests for the Modbus TCP gateway.

Requires QEMU with UART1 exposed as TCP port 5561 and guest port 502 forwarded
to host port 50502.  Uses a Python RTU slave (ModbusRtuSlaveThread) connected
to UART1 to respond to FC01-FC04 requests.

Coverage:
1. Two concurrent TCP clients each sending split frames — both get responses.
2. Socket A's partial frame in the reassembly buffer does not corrupt socket B.
3. 9th TCP client (beyond MBTCP_REASM_MAX_CONNS=8) falls back to single-pass
   mode and still receives a valid response (no crash).
"""

import socket
import struct
import threading
import time

import pytest

from conftest import build_gateway_fixture


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
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    # bridge.* and port_mode are set by the `gateway_slave` fixture


# ---------------------------------------------------------------------------
# Module-level constants
# ---------------------------------------------------------------------------

# QEMU host-forwarded port for the Modbus TCP gateway (guest port 502).
GATEWAY_HOST = "127.0.0.1"
GATEWAY_HOST_PORT = 50502

# QEMU UART1 chardev exposed as a TCP socket.
UART1_TCP_PORT = 5561

# Register value returned by the RTU slave for any register read.
FAKE_VALUE = 0x1234

# Maximum simultaneous reassembly slots in modbus_tcp.c (MBTCP_REASM_MAX_CONNS).
MBTCP_MAX_CONNS = 8

# Timeout for opening TCP connections to the gateway.
GATEWAY_CONNECT_TIMEOUT = 5.0


# ---------------------------------------------------------------------------
# Module-level helpers
# ---------------------------------------------------------------------------

def _build_modbus_tcp_request(txid: int, unit_id: int, fc: int, addr: int, count: int) -> bytes:
    """Build a complete Modbus TCP frame (MBAP + PDU)."""
    pdu = struct.pack('>HH', addr, count)
    mbap_length = 1 + 1 + len(pdu)   # unit_id + FC + PDU
    mbap = struct.pack('>HHH', txid, 0, mbap_length)
    return mbap + bytes([unit_id, fc]) + pdu


def _recv_gateway_response(sock: socket.socket, deadline: float) -> bytes:
    """Receive one complete Modbus TCP response.

    Reads until at least 6 bytes (MBAP header) are available, then reads the
    full PDU indicated by the MBAP length field.  Raises on connection close or
    deadline exceeded.  Uses deadline rather than the socket's recv timeout so
    that socket.timeout exceptions are caught and treated as a deadline check.
    """
    response = b''
    while len(response) < 6:
        if time.monotonic() >= deadline:
            raise TimeoutError(f"Deadline exceeded reading MBAP header: got {response.hex()!r}")
        try:
            chunk = sock.recv(256)
        except socket.timeout:
            continue   # let the deadline check above handle timeout
        if not chunk:
            raise ConnectionError("Gateway closed connection unexpectedly")
        response += chunk
    _txid, _proto, resp_length = struct.unpack('>HHH', response[:6])
    total_expected = 6 + resp_length
    while len(response) < total_expected:
        if time.monotonic() >= deadline:
            raise TimeoutError(
                f"Deadline exceeded reading PDU: got {len(response)}/{total_expected} bytes "
                f"({response.hex()!r})"
            )
        try:
            chunk = sock.recv(256)
        except socket.timeout:
            continue   # let the deadline check above handle timeout
        if not chunk:
            break
        response += chunk
    return response


# ---------------------------------------------------------------------------
# Fixture
# ---------------------------------------------------------------------------

# Use shared gateway fixture from conftest (R5)
gateway_slave = build_gateway_fixture(
    port_num=1,
    tcp_host_port=GATEWAY_HOST_PORT,
    uart_tcp_port=UART1_TCP_PORT,
    bridge_port=502,
    modbus=True,
    fake_value=FAKE_VALUE,
)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_gateway_multiconn_concurrent_split_frames(gateway_slave):
    """Two concurrent TCP clients each send split frames; both get correct responses.

    Each client sends the MBAP header (6 bytes) first, pauses 20 ms, then sends
    the PDU remainder.  The gateway reassembly layer must keep per-connection
    state so that the partial data from one socket does not affect the other.
    Because the RTU bus serialises requests, one client may wait for the other;
    the per-test timeout (120 s) is generous enough to cover both round-trips.
    """
    NUM_CLIENTS = 2
    results = {}
    results_lock = threading.Lock()

    def gw_split_worker(idx: int):
        txid = 200 + idx
        # Initialize sock to None so the finally block is safe even if socket() raises.
        sock = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(GATEWAY_CONNECT_TIMEOUT)
            sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            req = _build_modbus_tcp_request(txid, 1, 0x03, 0, 1)
            # Send MBAP header only, pause, then send the PDU — exercises reassembly
            sock.sendall(req[:6])
            time.sleep(0.02)
            sock.sendall(req[6:])
            deadline = time.monotonic() + 10.0
            response = _recv_gateway_response(sock, deadline)
            with results_lock:
                results[idx] = {"raw": response, "error": None, "txid": txid}
        except Exception as exc:
            with results_lock:
                results[idx] = {"raw": b"", "error": str(exc), "txid": txid}
        finally:
            if sock is not None:
                sock.close()

    threads = [
        threading.Thread(target=gw_split_worker, args=(i,), daemon=True)
        for i in range(NUM_CLIENTS)
    ]
    for t in threads:
        t.start()
    for t in threads:
        t.join(timeout=60)

    assert not any(t.is_alive() for t in threads), \
        "A client thread did not finish (deadlock?)"

    for i in range(NUM_CLIENTS):
        r = results.get(i, {})
        assert r.get("error") is None, f"Client {i} error: {r.get('error')}"
        raw = r["raw"]
        assert len(raw) >= 8, f"Client {i}: response too short: {raw.hex()!r}"
        resp_txid, _proto, _length = struct.unpack('>HHH', raw[:6])
        pdu = raw[6:]
        assert resp_txid == r["txid"], \
            f"Client {i}: TID mismatch: expected {r['txid']}, got {resp_txid}"
        assert not (pdu[1] & 0x80), \
            f"Client {i}: Modbus exception FC=0x{pdu[1]:02X}"
        print(f"✓ Concurrent split [client {i}]: TID={resp_txid} FC=0x{pdu[1]:02X}")


@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_gateway_multiconn_independent_buffers(gateway_slave):
    """Socket A's partial frame in reassembly buffer does not corrupt Socket B's responses.

    Sequential (not threaded) scenario:
      1. Socket A sends only the MBAP header (6 bytes) — partial, buffered by server.
      2. Socket B sends a complete request and receives a valid response.
      3. Socket A sends the remaining PDU and receives its own valid response.

    This proves that each connection has an independent reassembly slot and that
    A's partial state does not bleed into B's slot or its response.
    """
    txid_a = 300
    txid_b = 301
    req_a = _build_modbus_tcp_request(txid_a, 1, 0x03, 0, 1)
    req_b = _build_modbus_tcp_request(txid_b, 1, 0x03, 0, 1)

    sock_a = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_a.settimeout(GATEWAY_CONNECT_TIMEOUT)
    sock_b = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_b.settimeout(GATEWAY_CONNECT_TIMEOUT)
    try:
        sock_a.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
        sock_a.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        # Send only the MBAP header for req_a — server buffers 6 bytes, waits for PDU
        sock_a.sendall(req_a[:6])
        time.sleep(0.02)  # ensure the partial data is received by the server

        # While socket A is stalled, socket B sends and receives a complete exchange
        sock_b.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
        sock_b.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock_b.sendall(req_b)
        deadline_b = time.monotonic() + 10.0
        resp_b = _recv_gateway_response(sock_b, deadline_b)
        assert len(resp_b) >= 8, f"Socket B: response too short: {resp_b.hex()!r}"
        resp_txid_b, _, _ = struct.unpack('>HHH', resp_b[:6])
        assert resp_txid_b == txid_b, \
            f"Socket B: TID mismatch: expected {txid_b}, got {resp_txid_b}"
        assert not (resp_b[7] & 0x80), f"Socket B: Modbus exception 0x{resp_b[7]:02X}"
        print(
            f"✓ Independent buffers: socket B (TID={resp_txid_b}) responded correctly "
            f"while A was partial"
        )

        # Complete socket A's request and verify it gets its own correct response
        sock_a.sendall(req_a[6:])
        sock_a.settimeout(10.0)
        deadline_a = time.monotonic() + 10.0
        resp_a = _recv_gateway_response(sock_a, deadline_a)
        assert len(resp_a) >= 8, f"Socket A: response too short: {resp_a.hex()!r}"
        resp_txid_a, _, _ = struct.unpack('>HHH', resp_a[:6])
        assert resp_txid_a == txid_a, \
            f"Socket A: TID mismatch: expected {txid_a}, got {resp_txid_a}"
        assert not (resp_a[7] & 0x80), f"Socket A: Modbus exception 0x{resp_a[7]:02X}"
        print(
            f"✓ Independent buffers: socket A (TID={resp_txid_a}) also got its response "
            f"after B completed"
        )

    finally:
        sock_a.close()
        sock_b.close()


@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_gateway_multiconn_table_exhaustion(gateway_slave):
    """9th TCP client (beyond table limit of 8) falls back to single-pass mode; no crash.

    Open MBTCP_MAX_CONNS+1 (= 9) simultaneous connections.  Send one complete
    (unfragmented) request from each sequentially so the RTU bus is not
    saturated.  The 9th connection has no reassembly slot (table full) so the
    gateway falls back to separate_and_push_one_pass(), which handles complete
    frames correctly.  All 9 clients must receive valid responses.
    """
    NUM_SOCKETS = MBTCP_MAX_CONNS + 1   # 9 sockets — one beyond table limit
    sockets = []
    responses = {}
    try:
        # Open all connections before sending any requests
        for i in range(NUM_SOCKETS):
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(GATEWAY_CONNECT_TIMEOUT)
            s.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
            sockets.append(s)

        # Send and receive sequentially to avoid overwhelming the RTU bus
        for i, s in enumerate(sockets):
            txid = 400 + i
            req = _build_modbus_tcp_request(txid, 1, 0x03, 0, 1)
            s.settimeout(10.0)
            s.sendall(req)
            deadline = time.monotonic() + 10.0
            try:
                resp = _recv_gateway_response(s, deadline)
                responses[i] = {"raw": resp, "txid": txid, "error": None}
            except Exception as exc:
                responses[i] = {"raw": b"", "txid": txid, "error": str(exc)}

        # Verify all responses are valid
        for i in range(NUM_SOCKETS):
            r = responses[i]
            assert r["error"] is None, f"Socket {i} error: {r['error']}"
            raw = r["raw"]
            assert len(raw) >= 8, f"Socket {i}: response too short: {raw.hex()!r}"
            resp_txid, _, _ = struct.unpack('>HHH', raw[:6])
            assert resp_txid == r["txid"], \
                f"Socket {i}: TID mismatch: expected {r['txid']}, got {resp_txid}"
            assert not (raw[7] & 0x80), \
                f"Socket {i}: Modbus exception FC=0x{raw[7]:02X}"
        print(
            f"✓ Table exhaustion: all {NUM_SOCKETS} clients "
            f"(including the 9th fallback) responded correctly"
        )

    finally:
        for s in sockets:
            try:
                s.close()
            except Exception:
                pass
