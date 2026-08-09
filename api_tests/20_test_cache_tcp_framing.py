"""TCP framing tests for the cache Modbus TCP server.

Bug 07 fix verification: the cache server must correctly handle:
  1. Whole frame in a single send()          — baseline sanity check.
  2. Frame split across two TCP writes       — reassembly buffer path.
  3. Two frames coalesced in one send()      — multi-frame dispatch path.

Before the fix, cases 2 and 3 both failed because recv() was assumed to
deliver exactly one complete Modbus TCP frame per call.
"""

import qemu_ports
import socket
import struct
import threading
import time
from urllib.parse import urlparse

import pytest

from api_client import WBMGEAPI
from modbus_helpers import make_mbap_request, recv_exactly, send_and_receive, MIN_RESPONSE_SIZE
from packet_injector import PacketInjector


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    resp = api.update_settings({
        "cache_modbus_server_enabled": True,
        "cache_value_timeout_s": 60,
        "rs485_1": {
            "tx_disabled": True,      # PacketInjector drives traffic via UART chardev in QEMU; cache overlay records it
        }
    })
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    # cache_modbus_port, passive transport and the cache overlay are set by the module's own cache_tcp_server fixture


# ---------------------------------------------------------------------------
# Module-level constants
# ---------------------------------------------------------------------------

# QEMU host-forwarded port for the cache Modbus TCP server (guest port 50504).
QEMU_CACHE_MODBUS_PORT = qemu_ports.QEMU_CACHE_MODBUS_PORT

# Timeout for opening TCP connections to the cache server.
# 15s is generous enough to ride out a SYN drop in QEMU's OpenETH (Linux retransmits
# SYN at ~1s/~3s/~7s). A 5s timeout used to flake when one of several parallel SYNs
# happened to land while the emulated NIC's RX buffer was momentarily full.
CACHE_MODBUS_TCP_CONNECT_TIMEOUT = 15.0

# Modbus function code: Read Holding Registers.
FC_READ_HOLDING = 0x03

# Slave ID injected by PacketInjector (default slave=1).
CACHE_SLAVE_ID = 1

# First holding register address — always present in the cache after the injector runs.
CACHE_REG_ADDR = 0


# ---------------------------------------------------------------------------
# Module-level helper
# ---------------------------------------------------------------------------

def _recv_one_response(sock: socket.socket) -> tuple:
    """Receive one complete Modbus TCP response from *sock*.

    *sock* must already have a timeout set so the test does not block forever.

    Returns (tid, fc, payload) where:
      tid     — transaction identifier echoed by the server (int)
      fc      — function code (int, 0x80 | original_fc on exception)
      payload — bytes after the MBAP+FC header (may be empty)
    """
    # Read the first MIN_RESPONSE_SIZE bytes: tid(2) + proto(2) + len(2) + uid(1) + fc(1) = 8
    header = recv_exactly(sock, MIN_RESPONSE_SIZE)
    tid, _proto, length, _uid, fc = struct.unpack(">HHHBB", header)

    # The MBAP length field counts uid + fc + data; uid and fc are already consumed.
    remaining = length - 2
    payload = recv_exactly(sock, remaining) if remaining > 0 else b""
    return tid, fc, payload


# ---------------------------------------------------------------------------
# Module-scoped fixture
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def cache_tcp_server(api: WBMGEAPI):
    """Set up the cache Modbus TCP server on port 50504 and populate the cache.

    Steps:
      1. Save current rs485_1 port_mode and cache_modbus_port.
      2. Switch cache_modbus_port to QEMU_CACHE_MODBUS_PORT.
      3. Set port 1 to passive transport and enable the cache overlay.
      4. Start PacketInjector to drive live traffic into the cache.
      5. Wait up to 30 s for at least one cache entry to appear.
      6. Yield (host, QEMU_CACHE_MODBUS_PORT).
      7. Restore original settings in finally.

    Skipped when the QEMU cache port is not reachable (e.g. real-device run
    that does not expose this port).
    """
    # Skip guard: verify the QEMU port is accessible before we spend 30 s waiting.
    host = urlparse(api.base_url).hostname or "localhost"
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(CACHE_MODBUS_TCP_CONNECT_TIMEOUT)
    try:
        result = probe.connect_ex((host, QEMU_CACHE_MODBUS_PORT))
    finally:
        probe.close()
    if result != 0:
        pytest.skip(
            f"Cache Modbus TCP port {QEMU_CACHE_MODBUS_PORT} not reachable on {host} "
            f"(connect_ex returned {result}) — skipping TCP framing tests"
        )

    # Save original settings so teardown can restore them exactly.
    info_resp = api.get_info()
    assert info_resp.status_code == 200, \
        f"GET /info failed before fixture setup: HTTP {info_resp.status_code}"
    info = info_resp.json()
    original_port_mode = info.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    original_modbus_port = info.get("cache_modbus_port", 504)

    inj = PacketInjector(port=1)
    try:
        # Switch the cache server to the QEMU-forwarded GUEST port (fixed 50504) — that is
        # what the hostfwd rule forwards to; the host connects to QEMU_CACHE_MODBUS_PORT
        # (the dynamic host side) which reaches this guest port through the forward.
        resp = api.update_settings({"cache_modbus_port": qemu_ports.CACHE_MODBUS_GUEST_PORT})
        assert resp.status_code == 200, \
            f"Failed to set cache_modbus_port={qemu_ports.CACHE_MODBUS_GUEST_PORT}: HTTP {resp.status_code}"
        # Give the server time to rebind on the new port.
        time.sleep(1)

        # Open serial (passive) and activate the cache overlay on port 1.
        resp = api.set_port_mode(1, "passive")
        assert resp.status_code == 200, \
            f"Failed to set port 1 to passive: HTTP {resp.status_code}"
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, \
            f"Failed to enable cache overlay on port 1: HTTP {resp.status_code}"

        # Start injecting Modbus RTU traffic so the cache has data to serve.
        inj.__enter__()

        # Poll until the cache has at least one entry (up to 30 s).
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            time.sleep(1)
            st = api.get_cache_status()
            if st.status_code == 200 and st.json().get("entries", 0) > 0:
                break
        else:
            pytest.fail("Cache did not populate within 30 s — no entries visible via /cache/status")

        yield (host, QEMU_CACHE_MODBUS_PORT)

    finally:
        inj.__exit__(None, None, None)

        restore_errors = []

        try:
            api.update_settings({"cache_modbus_port": original_modbus_port})
        except Exception as exc:
            restore_errors.append(f"Failed to restore cache_modbus_port: {exc}")

        try:
            api.set_port_cache(1, False)
        except Exception as exc:
            restore_errors.append(f"Failed to disable cache overlay on port 1: {exc}")

        try:
            api.set_port_mode(1, original_port_mode)
        except Exception as exc:
            restore_errors.append(f"Failed to restore port 1 mode: {exc}")

        if restore_errors:
            raise AssertionError("; ".join(restore_errors))


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

@pytest.mark.timeout(60)
def test_cache_tcp_whole_frame(cache_tcp_server):
    """Whole Modbus TCP frame in one send() → one valid response.

    This is the baseline case: a single sendall() of a complete 12-byte
    FC03 request must produce a well-formed FC03 response with the same TID.
    """
    host, port = cache_tcp_server

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(CACHE_MODBUS_TCP_CONNECT_TIMEOUT)
    try:
        sock.connect((host, port))

        req = make_mbap_request(1, CACHE_SLAVE_ID, FC_READ_HOLDING, CACHE_REG_ADDR, 1)

        # send_and_receive does sendall + recv internally — one complete round-trip.
        resp_tid, resp_uid, resp_fc, payload = send_and_receive(sock, req)

        assert resp_tid == 1, \
            f"TID mismatch: expected 1, got {resp_tid}"
        # Check for exception first; then verify specific FC so both errors are visible.
        assert not (resp_fc & 0x80), \
            f"Server returned a Modbus exception: FC=0x{resp_fc:02X}, payload={payload.hex()}"
        assert resp_fc == FC_READ_HOLDING, \
            f"FC mismatch: expected 0x{FC_READ_HOLDING:02X}, got 0x{resp_fc:02X} " \
            f"(payload hex: {payload.hex()})"

        print(
            f"✓ Whole-frame: TID={resp_tid}, FC=0x{resp_fc:02X}, "
            f"payload={payload.hex()}"
        )
    finally:
        sock.close()


@pytest.mark.timeout(60)
def test_cache_tcp_split_frame(cache_tcp_server):
    """Frame split into two TCP writes → reassembly buffer produces one correct response.

    The request is split at byte offset 4 — just before the MBAP length field
    (bytes 4-5).  After the first write the server has only 4 bytes and cannot
    yet determine the total frame length; it must buffer and wait.  The second
    write delivers the rest.  A correct implementation returns exactly one
    response with the echoed TID.

    This test would fail before the Bug 07 fix (the server discarded the
    partial frame and returned no response, or returned a malformed one).
    """
    host, port = cache_tcp_server
    test_tid = 2

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(CACHE_MODBUS_TCP_CONNECT_TIMEOUT)
    try:
        sock.connect((host, port))
        # Disable Nagle's algorithm so that the first send() is flushed immediately
        # as a separate TCP segment.  Without TCP_NODELAY the OS may coalesce the
        # two writes into one segment, defeating the purpose of this test.
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        req = make_mbap_request(test_tid, CACHE_SLAVE_ID, FC_READ_HOLDING, CACHE_REG_ADDR, 1)
        assert len(req) == 12, f"Unexpected request length: {len(req)} (expected 12)"

        # Send first 4 bytes: transaction_id(2) + protocol_id(2).
        # The MBAP length field is at bytes 4-5; the server cannot determine
        # frame size until it has at least 6 bytes.
        sock.send(req[:4])
        time.sleep(0.05)   # allow the first partial segment to arrive separately
        sock.sendall(req[4:])

        # Receive the response manually so we can verify it without relying on
        # send_and_receive (which also sends).
        sock.settimeout(5.0)
        resp_tid, resp_fc, payload = _recv_one_response(sock)

        assert resp_tid == test_tid, \
            f"TID mismatch: expected {test_tid}, got {resp_tid}"
        # Check for exception first; then verify specific FC so both errors are visible.
        assert not (resp_fc & 0x80), \
            f"Server returned a Modbus exception on split frame: " \
            f"FC=0x{resp_fc:02X}, payload={payload.hex()}"
        assert resp_fc == FC_READ_HOLDING, \
            f"FC mismatch: expected 0x{FC_READ_HOLDING:02X}, got 0x{resp_fc:02X} " \
            f"(payload hex: {payload.hex()})"

        print(
            f"✓ Split-frame: TID={resp_tid}, FC=0x{resp_fc:02X}, "
            f"payload={payload.hex()}"
        )
    finally:
        sock.close()


@pytest.mark.timeout(60)
def test_cache_tcp_coalesced_frames(cache_tcp_server):
    """Two frames concatenated in one send() → reassembly dispatches both correctly.

    Both FC03 requests are concatenated and delivered in a single sendall()
    call.  A correct implementation must detect the frame boundary inside the
    receive buffer and dispatch two independent responses.

    This test would fail before the Bug 07 fix (the server processed only the
    first frame and ignored the remainder of the buffer, so the second response
    was never sent and recv() would block until timeout).
    """
    host, port = cache_tcp_server
    tid1 = 10
    tid2 = 11

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(CACHE_MODBUS_TCP_CONNECT_TIMEOUT)
    try:
        sock.connect((host, port))
        sock.settimeout(5.0)

        req1 = make_mbap_request(tid1, CACHE_SLAVE_ID, FC_READ_HOLDING, 0, 1)
        req2 = make_mbap_request(tid2, CACHE_SLAVE_ID, FC_READ_HOLDING, 1, 1)

        # Send both requests in a single syscall so the server receives them as
        # one TCP segment — the most likely scenario to expose the framing bug.
        sock.sendall(req1 + req2)

        # First response.
        resp_tid1, resp_fc1, payload1 = _recv_one_response(sock)
        assert resp_tid1 == tid1, \
            f"First response TID mismatch: expected {tid1}, got {resp_tid1}"
        # Check for exception first; then verify specific FC so both errors are visible.
        assert not (resp_fc1 & 0x80), \
            f"Server returned a Modbus exception for first coalesced frame: " \
            f"FC=0x{resp_fc1:02X}, payload={payload1.hex()}"
        assert resp_fc1 == FC_READ_HOLDING, \
            f"First response FC mismatch: expected 0x{FC_READ_HOLDING:02X}, " \
            f"got 0x{resp_fc1:02X} (payload hex: {payload1.hex()})"

        # Second response.
        resp_tid2, resp_fc2, payload2 = _recv_one_response(sock)
        assert resp_tid2 == tid2, \
            f"Second response TID mismatch: expected {tid2}, got {resp_tid2}"
        # Check for exception first; then verify specific FC so both errors are visible.
        assert not (resp_fc2 & 0x80), \
            f"Server returned a Modbus exception for second coalesced frame: " \
            f"FC=0x{resp_fc2:02X}, payload={payload2.hex()}"
        assert resp_fc2 == FC_READ_HOLDING, \
            f"Second response FC mismatch: expected 0x{FC_READ_HOLDING:02X}, " \
            f"got 0x{resp_fc2:02X} (payload hex: {payload2.hex()})"

        print(
            f"✓ Coalesced frames: "
            f"resp1 TID={resp_tid1} FC=0x{resp_fc1:02X} payload={payload1.hex()}, "
            f"resp2 TID={resp_tid2} FC=0x{resp_fc2:02X} payload={payload2.hex()}"
        )
    finally:
        sock.close()


@pytest.mark.timeout(60)
def test_cache_tcp_concurrent_connections(cache_tcp_server):
    """Multiple simultaneous connections each reassemble their split frames independently.

    Three threads each open a separate TCP connection, split a request across
    two TCP writes (with a 50 ms gap), and receive their own response.  The
    server must keep reassembly state per-connection so that partial data from
    one socket does not bleed into another socket's buffer.
    """
    host, port = cache_tcp_server

    NUM_CONNS = 3
    results = {}
    lock = threading.Lock()

    def conn_worker(idx, tid):
        # Initialize sock to None so the finally block is safe even if socket() raises.
        sock = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(CACHE_MODBUS_TCP_CONNECT_TIMEOUT)
            sock.connect((host, port))
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            req = make_mbap_request(tid, CACHE_SLAVE_ID, FC_READ_HOLDING, CACHE_REG_ADDR, 1)
            # Send the first 4 bytes (TID + protocol ID), pause, then send the rest.
            # This forces the server to buffer and wait for the remaining bytes.
            sock.send(req[:4])
            time.sleep(0.05)
            sock.sendall(req[4:])
            sock.settimeout(5.0)
            resp_tid, resp_fc, payload = _recv_one_response(sock)
            with lock:
                results[idx] = {"tid": resp_tid, "fc": resp_fc, "payload": payload, "error": None}
        except Exception as exc:
            with lock:
                results[idx] = {"tid": None, "fc": None, "payload": b"", "error": str(exc)}
        finally:
            if sock is not None:
                try:
                    sock.close()
                except Exception:
                    pass

    tids = [100 + i for i in range(NUM_CONNS)]
    threads = [
        threading.Thread(target=conn_worker, args=(i, tids[i]), daemon=True)
        for i in range(NUM_CONNS)
    ]
    # Stagger by ~50 ms so three SYNs don't hit OpenETH in the same emulator tick;
    # the test still exercises concurrent open sockets, just not simultaneous SYNs.
    for t in threads:
        t.start()
        time.sleep(0.05)
    for t in threads:
        t.join(timeout=30)

    still_alive = [t for t in threads if t.is_alive()]
    assert not still_alive, f"{len(still_alive)} connection thread(s) did not finish (deadlock?)"

    for i in range(NUM_CONNS):
        r = results.get(i, {})
        assert r.get("error") is None, f"Connection {i} raised: {r.get('error')}"
        assert r["tid"] == tids[i], \
            f"Conn {i}: TID mismatch: expected {tids[i]}, got {r['tid']}"
        assert not (r["fc"] & 0x80), \
            f"Conn {i}: Modbus exception FC=0x{r['fc']:02X}"
        assert r["fc"] == FC_READ_HOLDING, \
            f"Conn {i}: FC mismatch: got 0x{r['fc']:02X}"

    print(f"✓ Concurrent connections: all {NUM_CONNS} threads got valid split-frame responses")


@pytest.mark.timeout(60)
def test_cache_tcp_large_split(cache_tcp_server):
    """Frame delivered across 4 separate TCP writes (3+ recv() calls) → one correct response.

    A 12-byte FC03 request is cut into four 3-byte chunks each separated by a
    20 ms gap.  The server must accumulate all four recv() calls before it has
    enough data to parse the full MBAP header and PDU and produce a response.
    """
    host, port = cache_tcp_server
    test_tid = 3
    CHUNK_SIZE = 3

    # Let the server settle after the previous test's 3 concurrent connections —
    # otherwise lingering close events can delay this test's response and push
    # us past the recv() timeout under host CPU contention.
    time.sleep(0.3)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(CACHE_MODBUS_TCP_CONNECT_TIMEOUT)
    try:
        sock.connect((host, port))
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        req = make_mbap_request(test_tid, CACHE_SLAVE_ID, FC_READ_HOLDING, CACHE_REG_ADDR, 1)
        assert len(req) == 12, f"Unexpected request length: {len(req)} (expected 12)"
        # Send in 4 chunks of 3 bytes with inter-chunk gaps so that each chunk
        # arrives as a separate TCP segment.
        for offset in range(0, len(req), CHUNK_SIZE):
            sock.send(req[offset:offset + CHUNK_SIZE])
            time.sleep(0.02)
        sock.settimeout(10.0)
        resp_tid, resp_fc, payload = _recv_one_response(sock)
        assert resp_tid == test_tid, \
            f"TID mismatch: expected {test_tid}, got {resp_tid}"
        assert not (resp_fc & 0x80), \
            f"Modbus exception: FC=0x{resp_fc:02X}, payload={payload.hex()}"
        assert resp_fc == FC_READ_HOLDING, \
            f"FC mismatch: 0x{resp_fc:02X}"
        print(
            f"✓ Large-split (4 chunks): TID={resp_tid} FC=0x{resp_fc:02X} payload={payload.hex()}"
        )
    finally:
        sock.close()


# 105 s, not 60 s: an item's pytest-timeout budget covers setup + call + TEARDOWN, and
# module-scoped fixtures are torn down inside the LAST item of the module. This is that
# item, so it also pays conftest's _restore_rs485_settings teardown — up to two bounded
# POST /settings plus a settle window (2 x 20.1 s + 1 s = 41.2 s, see _RS485_HTTP_TIMEOUT)
# — on top of this module's own cache_tcp_server teardown (:89: stop the injector, restore
# cache_modbus_port, disable the cache overlay, restore the port mode) and _baseline (:25).
# 60 s body + 45 s teardown allowance.
@pytest.mark.timeout(105)
def test_cache_tcp_close_mid_frame(cache_tcp_server):
    """Connection closed mid-frame → reassembly slot freed; server handles next connection normally.

    Socket A sends only 4 bytes of a 12-byte request and then closes the
    connection.  The server must release the partially-filled reassembly slot.
    Socket B then sends a complete request and must receive a valid response,
    proving the slot was freed and the server did not crash.
    """
    host, port = cache_tcp_server

    # Socket A: partial frame then immediate close — triggers close_handler in tcp_server.c
    sock_a = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_a.settimeout(CACHE_MODBUS_TCP_CONNECT_TIMEOUT)
    try:
        sock_a.connect((host, port))
        req_partial = make_mbap_request(99, CACHE_SLAVE_ID, FC_READ_HOLDING, CACHE_REG_ADDR, 1)
        # Send only 4 bytes — an incomplete MBAP header; server cannot determine frame length yet.
        sock_a.send(req_partial[:4])
        time.sleep(0.05)  # give server time to buffer the partial data
    finally:
        sock_a.close()  # close mid-frame — server must free the reassembly slot

    time.sleep(0.1)  # allow the server to process the close event

    # Socket B: complete request — must succeed after slot is freed
    test_tid = 4
    sock_b = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_b.settimeout(CACHE_MODBUS_TCP_CONNECT_TIMEOUT)
    try:
        sock_b.connect((host, port))
        sock_b.settimeout(5.0)
        req = make_mbap_request(test_tid, CACHE_SLAVE_ID, FC_READ_HOLDING, CACHE_REG_ADDR, 1)
        sock_b.sendall(req)
        resp_tid, resp_fc, payload = _recv_one_response(sock_b)
        assert resp_tid == test_tid, \
            f"TID mismatch after mid-frame close: expected {test_tid}, got {resp_tid}"
        assert not (resp_fc & 0x80), \
            f"Modbus exception after mid-frame close: FC=0x{resp_fc:02X}"
        assert resp_fc == FC_READ_HOLDING, \
            f"FC mismatch: 0x{resp_fc:02X}"
        print(
            f"✓ Close-mid-frame: server survived and returned TID={resp_tid} FC=0x{resp_fc:02X}"
        )
    finally:
        sock_b.close()
