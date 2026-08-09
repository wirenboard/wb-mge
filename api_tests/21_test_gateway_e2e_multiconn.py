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

import qemu_ports
import socket
import struct
import threading
import time

import pytest

from conftest import build_gateway_fixture
from modbus_helpers import make_mbap_request, recv_modbus_tcp_response


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
GATEWAY_HOST = qemu_ports.GATEWAY_HOST
GATEWAY_HOST_PORT = qemu_ports.GATEWAY_HOST_PORT

# QEMU UART1 chardev exposed as a TCP socket.
UART1_TCP_PORT = qemu_ports.UART1_TCP_PORT

# Register value returned by the RTU slave for any register read.
FAKE_VALUE = 0x1234

# Maximum simultaneous reassembly slots in modbus_tcp.c (MBTCP_REASM_MAX_CONNS).
MBTCP_MAX_CONNS = 8

# Timeout for opening TCP connections to the gateway.
# 15s rides out a SYN drop in QEMU's emulated OpenETH (Linux retransmits at ~1s/~3s/~7s).
# A 5s timeout used to flake when several rapid connects coincided with an RX buffer
# overflow in the emulator.
GATEWAY_CONNECT_TIMEOUT = 15.0

# Per-attempt receive budget for the data phase, and the socket timeout that must be armed
# alongside it. recv_modbus_tcp_response() checks its deadline only BEFORE each recv(), so a
# socket timeout LARGER than the deadline makes the deadline unenforceable: with the 15 s
# connect timeout still armed, the first recv() blocks for 15 s before the 10 s deadline is
# ever looked at. Every socket must therefore be re-armed to DATA_TIMEOUT after connect().
DATA_TIMEOUT = 10.0


# ---------------------------------------------------------------------------
# Module-level helpers
# ---------------------------------------------------------------------------

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

    One Modbus retry is tolerated (see MAX_ATTEMPTS below) but BUDGETED: the total
    number of attempts across all clients is asserted at the end, so a firmware
    regression that costs every client its first attempt cannot hide behind it.
    """
    NUM_CLIENTS = 2
    # The gateway validates each reply against a DESCRIPTOR-WIDE generation counter
    # (conn_generation), not a per-fd one — see the explanatory comment in
    # tcp_server_send_to_captured_client() in main/bridge/tcp_server.c. By that
    # conscious firmware compromise, if any *other* client drops during our RTU
    # turnaround the generation moves and our otherwise-valid reply is discarded; the
    # accepted by-design cost is exactly ONE Modbus retry for at most ONE client.
    # Modbus is a timeout/retry protocol, so we mirror the contract: one initial
    # attempt plus one retry on the same (still-open) socket. A second timeout is a
    # genuine failure, not the documented compromise, and is re-raised.
    MAX_ATTEMPTS = 2
    results = {}
    results_lock = threading.Lock()

    def gw_split_worker(idx: int):
        base_txid = 200 + idx
        txid = base_txid
        attempts = 0
        # Initialize sock to None so the finally block is safe even if socket() raises.
        sock = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(GATEWAY_CONNECT_TIMEOUT)
            sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            # Re-arm for the data phase — see the DATA_TIMEOUT comment above for why the
            # socket timeout must not stay at the larger connect value.
            sock.settimeout(DATA_TIMEOUT)
            response = None
            for attempt in range(MAX_ATTEMPTS):
                # A FRESH TID per attempt. Reusing it would let a merely LATE reply to the
                # first request satisfy the retry: the assertions would pass while the
                # retried request went unserved (its orphaned reply then RST'd by close()).
                # The documented compromise DROPS a reply, it does not delay it, so with a
                # fresh TID a stale reply fails the TID check instead of masking the bug.
                txid = base_txid + 1000 * attempt
                req = make_mbap_request(txid, 1, 0x03, 0, 1)
                attempts = attempt + 1
                # Send MBAP header only, pause, then send the PDU — exercises reassembly
                sock.sendall(req[:6])
                time.sleep(0.02)
                sock.sendall(req[6:])
                deadline = time.monotonic() + DATA_TIMEOUT
                try:
                    response = recv_modbus_tcp_response(sock, deadline)
                    break
                except TimeoutError:
                    if attempt == MAX_ATTEMPTS - 1:
                        raise
            with results_lock:
                results[idx] = {"raw": response, "error": None, "txid": txid,
                                "attempts": attempts}
        except Exception as exc:
            with results_lock:
                results[idx] = {"raw": b"", "error": str(exc), "txid": txid,
                                "attempts": attempts}
        finally:
            if sock is not None:
                sock.close()

    threads = [
        threading.Thread(target=gw_split_worker, args=(i,), daemon=True)
        for i in range(NUM_CLIENTS)
    ]
    # Stagger by ~50 ms so the two SYNs don't hit the emulated OpenETH in the same tick.
    for t in threads:
        t.start()
        time.sleep(0.05)
    for t in threads:
        t.join(timeout=60)

    assert not any(t.is_alive() for t in threads), \
        "A client thread did not finish (deadlock?)"

    for i in range(NUM_CLIENTS):
        # Explicit membership check first. Every later read of results[i] — r["raw"] here
        # and results[i]["attempts"] in the retry budget below — indexes directly, so a
        # worker that registered no result at all would surface as a bare KeyError with
        # no indication of which client vanished.
        assert i in results, f"Client {i} produced no result"
        r = results[i]
        assert r.get("error") is None, f"Client {i} error: {r.get('error')}"
        raw = r["raw"]
        assert len(raw) >= 8, f"Client {i}: response too short: {raw.hex()!r}"
        resp_txid, _proto, _length = struct.unpack('>HHH', raw[:6])
        pdu = raw[6:]
        assert resp_txid == r["txid"], (
            f"Client {i}: TID mismatch: expected {r['txid']}, got {resp_txid}"
            + (" — this is the stale reply to the timed-out first attempt arriving "
               "late, so that reply was DELAYED, not dropped; the retry accommodates "
               "a dropped reply only" if r["attempts"] > 1 else "")
        )
        assert not (pdu[1] & 0x80), \
            f"Client {i}: Modbus exception FC=0x{pdu[1]:02X}"
        print(f"✓ Concurrent split [client {i}]: TID={resp_txid} FC=0x{pdu[1]:02X} "
              f"({r['attempts']} attempt(s))")

    # Budget the retries. The generation counter only moves when a client socket is
    # CLOSED (retire_client_conn() is its sole bumper), and in this test the only
    # closes are the workers' own sock.close() calls on the way out. So at most one
    # client — whichever is still mid-turnaround when the first one finishes — can
    # lose a reply to the descriptor-wide check, which is exactly the one-retry cost
    # tcp_server_send_to_captured_client() documents. Hence NUM_CLIENTS attempts plus
    # at most one extra, in total, across all clients.
    total_attempts = sum(results[i]["attempts"] for i in range(NUM_CLIENTS))
    assert total_attempts <= NUM_CLIENTS + 1, (
        f"{total_attempts} attempts for {NUM_CLIENTS} clients — more than one client "
        f"needed a retry, which is outside the documented per-descriptor compromise "
        f"(see tcp_server_send_to_captured_client() in main/bridge/tcp_server.c). "
        f"Suspect a regression that loses the FIRST attempt for everyone: "
        f"conn_generation bumping on every request, or an off-by-one in the "
        f"reassembly slot dropping the first split frame."
    )


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
    req_a = make_mbap_request(txid_a, 1, 0x03, 0, 1)
    req_b = make_mbap_request(txid_b, 1, 0x03, 0, 1)

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
        # Re-arm for the data phase — see the DATA_TIMEOUT comment at the top of the
        # module. Without this, sock_b keeps the 15 s connect timeout and its 10 s
        # deadline is never enforced: the real bound would be 15 s.
        sock_b.settimeout(DATA_TIMEOUT)
        deadline_b = time.monotonic() + DATA_TIMEOUT
        resp_b = recv_modbus_tcp_response(sock_b, deadline_b)
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
        sock_a.settimeout(DATA_TIMEOUT)
        deadline_a = time.monotonic() + DATA_TIMEOUT
        resp_a = recv_modbus_tcp_response(sock_a, deadline_a)
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
# 165 s, not 120 s: an item's pytest-timeout budget covers setup + call + TEARDOWN, and
# module-scoped fixtures are torn down inside the LAST item of the module. This is that
# item, so it also pays conftest's _restore_rs485_settings teardown — up to two bounded
# POST /settings plus a settle window (2 x 20.1 s + 1 s = 41.2 s, see _RS485_HTTP_TIMEOUT).
# This module's own _baseline (:25) is setup-only and gateway_slave is function-scoped
# (built by conftest.build_gateway_fixture), so the conftest restore is the whole module
# teardown. 120 s body + 45 s teardown allowance.
@pytest.mark.timeout(165)
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
        # Open all connections before sending any requests.
        # Stagger by ~50 ms each so 9 SYNs don't hit the emulated OpenETH in the same
        # tick — without this the RX buffer occasionally overflows and a SYN gets dropped.
        for i in range(NUM_SOCKETS):
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(GATEWAY_CONNECT_TIMEOUT)
            s.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
            sockets.append(s)
            time.sleep(0.05)

        # Send and receive sequentially to avoid overwhelming the RTU bus
        for i, s in enumerate(sockets):
            txid = 400 + i
            req = make_mbap_request(txid, 1, 0x03, 0, 1)
            # Re-arm for the data phase — see the DATA_TIMEOUT comment at the top of
            # the module.
            s.settimeout(DATA_TIMEOUT)
            s.sendall(req)
            deadline = time.monotonic() + DATA_TIMEOUT
            try:
                resp = recv_modbus_tcp_response(s, deadline)
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
