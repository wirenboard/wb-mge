"""Advanced Modbus TCP gateway E2E tests — Wave 3.

Requires QEMU with the UART1 chardev exposed on TCP and guest port 502 forwarded to a
host port; both host ports follow WB_MGE_PORT_SLOT (api_tests/qemu_ports.py). Uses a
Python RTU slave (ModbusRtuSlaveThread) connected to UART1 to respond to FC01-FC04
requests.

Coverage:
7.  RTU slave does not respond → gateway must not hang; TCP connection stays open.
8.  RTU slave returns exception → gateway proxies exception frame to TCP client.
9.  FC06 Write Single Register forwarded through gateway.
10. Five back-to-back requests on same TCP connection; each TID matches.
11. TCP client disconnects while gateway waits for RTU response → no crash.
"""

import qemu_ports
import socket
import struct
import time

import pytest

from conftest import build_gateway_fixture
from modbus_helpers import make_mbap_request, recv_modbus_tcp_response


# ---------------------------------------------------------------------------
# Module-level constants
# ---------------------------------------------------------------------------

GATEWAY_HOST = qemu_ports.GATEWAY_HOST
GATEWAY_HOST_PORT = qemu_ports.GATEWAY_HOST_PORT  # QEMU hostfwd: slot gateway host port -> guest 502
UART1_TCP_PORT = qemu_ports.UART1_TCP_PORT  # QEMU UART1 chardev TCP
FAKE_VALUE = 0x1234


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
# Gateway fixture
# ---------------------------------------------------------------------------

gateway_slave = build_gateway_fixture(
    port_num=1,
    uart_tcp_port=UART1_TCP_PORT,
    bridge_port=qemu_ports.GATEWAY_GUEST_PORT,      # guest 502
    modbus=True,
    fake_value=FAKE_VALUE,
)


# ---------------------------------------------------------------------------
# Module-level helpers
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Test #7: RTU timeout visible at TCP level
# ---------------------------------------------------------------------------

@pytest.mark.qemu
# 120s: marker covers function-scoped setup+teardown whose retrying 30s HTTP calls are slow under QEMU load
@pytest.mark.timeout(120)
def test_gateway_rtu_timeout_no_response(gateway_slave):
    """RTU slave does not respond: gateway must not crash, TCP conn stays open.

    Uses drop_count=1 so the slave silently ignores the first request.
    The gateway should wait for the RTU timeout (~336 ms at 9600 baud) and then
    either return a Modbus exception or close the TCP connection gracefully —
    the key requirement is that it does NOT hang forever and the next request
    (after slave is re-enabled) works correctly.
    """
    gateway_slave.drop_count = 1

    gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock.settimeout(5.0)
    try:
        gw_sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))

        txid = 0x0700
        request = make_mbap_request(txid, 1, 0x03, 0, 1)
        gw_sock.sendall(request)

        # Wait up to 3 seconds for ANY response (timeout response or connection close).
        # The gateway must NOT hang indefinitely.
        deadline = time.monotonic() + 3.0
        got_data = b''
        try:
            while time.monotonic() < deadline:
                try:
                    chunk = gw_sock.recv(256)
                    if not chunk:
                        break   # connection closed cleanly — acceptable behaviour
                    got_data += chunk
                    break       # received some data — also acceptable
                except socket.timeout:
                    continue
        except (ConnectionResetError, OSError):
            pass    # connection reset by gateway — acceptable

        # Assert that drop_count was decremented (the slave saw and dropped the request)
        assert gateway_slave.drop_count == 0, (
            "RTU slave did not receive the request (gateway did not forward to UART)"
        )

        # Verify gateway is still operational: second request (slave now responds) must succeed.
        # After RTU timeout the gateway stays connected — per modbus_tcp_server_task() loop
        # it records the timeout via rs485_stats_update() and returns to waiting for next request.
        # A new request on the same connection must produce a valid response.
        gateway_slave.drop_count = 0
        txid2 = 0x0701
        request2 = make_mbap_request(txid2, 1, 0x03, 0, 1)
        gw_sock.sendall(request2)
        resp2 = recv_modbus_tcp_response(gw_sock, time.monotonic() + 5.0)
        resp_txid2 = struct.unpack('>H', resp2[:2])[0]
        assert resp_txid2 == txid2, (
            f"Second request TID mismatch: expected {txid2:#06x}, got {resp_txid2:#06x}. "
            "Gateway may have crashed or hung after RTU timeout."
        )
        print(f"✓ Gateway survived RTU timeout; second request TID={resp_txid2:#06x} OK")
    finally:
        gw_sock.close()


# ---------------------------------------------------------------------------
# Test #8: Modbus exception forwarded to TCP client
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_gateway_exception_forwarded_to_tcp(gateway_slave):
    """RTU slave responds with FC03 exception 0x02; TCP client must receive FC=0x83 exception.

    Uses exception_fc={0x03: 0x02} so the slave returns an exception response.
    The gateway must proxy the exception back to the TCP client unchanged.
    """
    gateway_slave.exception_fc = {0x03: 0x02}
    try:
        gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        gw_sock.settimeout(5.0)
        try:
            gw_sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))

            txid = 0x0800
            unit_id = 0x01
            request = make_mbap_request(txid, unit_id, 0x03, 0, 1)
            gw_sock.sendall(request)

            resp = recv_modbus_tcp_response(gw_sock, time.monotonic() + 5.0)
            assert len(resp) >= 9, f"Exception response too short: {resp.hex()!r}"

            resp_txid = struct.unpack('>H', resp[:2])[0]
            assert resp_txid == txid, f"TID mismatch: {resp_txid:#06x} != {txid:#06x}"

            pdu = resp[6:]
            assert pdu[0] == unit_id, f"Unit ID mismatch: {pdu[0]}"
            assert pdu[1] == 0x83, f"Expected exception FC 0x83, got {pdu[1]:#04x}"
            assert pdu[2] == 0x02, f"Expected exception code 0x02, got {pdu[2]:#04x}"

            print(f"✓ Exception forwarded: TID={txid:#06x} FC=0x{pdu[1]:02X} code={pdu[2]:#04x}")
        finally:
            gw_sock.close()
    finally:
        gateway_slave.exception_fc = {}


# ---------------------------------------------------------------------------
# Test #9: FC06 Write Single Register through gateway
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_gateway_fc06_write_single_register(gateway_slave):
    """FC06 (Write Single Register) request forwarded through gateway.

    The RTU slave returns exception 0x01 for unsupported FCs (FC06 is not
    implemented in the slave), so we assert on exception response: the gateway
    must correctly proxy write FCs without mangling the frame.
    """
    gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock.settimeout(5.0)
    try:
        gw_sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))

        txid = 0x0900
        unit_id = 0x01
        reg_addr = 0x0010
        reg_value = 0x00FF
        # FC06 PDU: addr(2) + value(2) = 4 bytes; MBAP length = 1 (unit_id) + 1 (FC) + 4 = 6
        pdu_bytes = struct.pack('>HH', reg_addr, reg_value)
        mbap_length = 1 + 1 + len(pdu_bytes)
        mbap = struct.pack('>HHH', txid, 0, mbap_length)
        request = mbap + bytes([unit_id, 0x06]) + pdu_bytes
        gw_sock.sendall(request)

        resp = recv_modbus_tcp_response(gw_sock, time.monotonic() + 5.0)
        assert len(resp) >= 8, f"FC06 response too short: {resp.hex()!r}"

        resp_txid = struct.unpack('>H', resp[:2])[0]
        assert resp_txid == txid, f"TID mismatch: {resp_txid:#06x}"

        pdu_resp = resp[6:]
        # Slave responds with exception 0x01 for FC06 — gateway must proxy it
        # (any response from RTU slave proves gateway forwarded and relayed correctly)
        assert pdu_resp[0] == unit_id, f"Unit ID mismatch: {pdu_resp[0]}"
        fc_resp = pdu_resp[1]
        assert fc_resp in (0x06, 0x86), \
            f"Expected FC06 echo (0x06) or exception (0x86), got {fc_resp:#04x}"

        print(f"✓ FC06 gateway round-trip: TID={txid:#06x} FC_resp={fc_resp:#04x}")
    finally:
        gw_sock.close()


# ---------------------------------------------------------------------------
# Test #10: Back-to-back requests on same TCP connection
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_gateway_back_to_back_requests(gateway_slave):
    """Five sequential FC03 requests on the same TCP connection; each TID matches.

    Tests that the gateway correctly tracks TID per request and does not mix
    responses or corrupt per-connection state between requests.
    """
    NUM_REQUESTS = 5

    gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock.settimeout(10.0)
    try:
        gw_sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))

        for i in range(NUM_REQUESTS):
            txid = 0x1000 + i
            request = make_mbap_request(txid, 1, 0x03, i, 1)
            gw_sock.sendall(request)

            resp = recv_modbus_tcp_response(gw_sock, time.monotonic() + 10.0)
            assert len(resp) >= 9, f"Request {i}: response too short: {resp.hex()!r}"

            resp_txid = struct.unpack('>H', resp[:2])[0]
            assert resp_txid == txid, \
                f"Request {i}: TID mismatch: expected {txid:#06x}, got {resp_txid:#06x}"

            pdu = resp[6:]
            assert not (pdu[1] & 0x80), \
                f"Request {i}: Modbus exception FC={pdu[1]:#04x}"

            regs = struct.unpack('>H', pdu[3:5])
            assert regs[0] == FAKE_VALUE, \
                f"Request {i}: register value {regs[0]:#06x} != {FAKE_VALUE:#06x}"

        print(f"✓ {NUM_REQUESTS} back-to-back requests all matched correct TID and value")
    finally:
        gw_sock.close()


# ---------------------------------------------------------------------------
# Test #11: TCP client disconnects during RTU wait
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_gateway_client_disconnect_during_rtu_wait(gateway_slave):
    """TCP client disconnects while gateway is waiting for RTU response.

    Uses drop_count=1 so the slave does not respond to A's request.
    The gateway waits for RTU timeout (~336 ms at 9600 baud), then moves on.
    A new client B connects afterwards and must receive SOME response (gateway alive).

    Key invariant (from test strategy report): gateway does NOT crash or deadlock
    after client A disconnects mid-RTU-wait. The gateway must remain operational.

    TID correctness after disconnect is verified by test_gateway_disconnect_tid_mismatch_regression.
    Here we only verify that the gateway is alive and sends a valid Modbus TCP response frame to B.
    """
    # Slave will drop A's request — no RTU response sent
    gateway_slave.drop_count = 1

    # Client A: send request, immediately disconnect
    gw_sock_a = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock_a.settimeout(5.0)
    try:
        gw_sock_a.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
        txid_a = 0x1100
        request_a = make_mbap_request(txid_a, 1, 0x03, 0, 1)
        gw_sock_a.sendall(request_a)
        # Immediately close — gateway is still waiting for RTU response
    finally:
        gw_sock_a.close()

    # Wait 0.5 s before resetting drop_count to guarantee:
    # 1. QEMU UART has delivered A's RTU frame to the slave (< 50 ms typically).
    # 2. The slave has processed and dropped it (drop_count consumed).
    # This ensures the gateway's RTU timeout fires, which is the condition under test.
    # B's 10 s socket timeout absorbs the remaining wait comfortably.
    time.sleep(0.5)
    gateway_slave.drop_count = 0

    # Client B: must receive a valid Modbus TCP response frame (any TID)
    gw_sock_b = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock_b.settimeout(10.0)
    try:
        gw_sock_b.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
        txid_b = 0x1101
        request_b = make_mbap_request(txid_b, 1, 0x03, 0, 1)
        gw_sock_b.sendall(request_b)

        resp_b = recv_modbus_tcp_response(gw_sock_b, time.monotonic() + 10.0)
        # Gateway alive check: we must receive at least a valid MBAP header
        assert len(resp_b) >= 8, (
            f"Client B response too short — gateway may have crashed: {resp_b.hex()!r}"
        )
        # Verify it is a valid Modbus TCP frame (protocol_id must be 0)
        _resp_proto = struct.unpack('>H', resp_b[2:4])[0]
        assert _resp_proto == 0, f"Invalid protocol_id in response: {_resp_proto}"
        # Verify unit_id and FC are present in PDU
        pdu_b = resp_b[6:]
        assert len(pdu_b) >= 2, f"PDU too short: {pdu_b.hex()!r}"
        assert not (pdu_b[1] & 0x80), f"Unexpected Modbus exception FC: {pdu_b[1]:#04x}"

        resp_txid_b = struct.unpack('>H', resp_b[:2])[0]
        print(
            f"✓ Gateway survived client disconnect during RTU wait; "
            f"B received valid response TID={resp_txid_b:#06x}"
        )
    finally:
        gw_sock_b.close()


# ---------------------------------------------------------------------------
# Test #11b: TID mismatch after client disconnect — regression test for
# known firmware bug (pending_tid not cleared after client disconnect)
# ---------------------------------------------------------------------------

@pytest.mark.qemu
# 165 s, not 120 s: an item's pytest-timeout budget covers setup + call + TEARDOWN, and
# module-scoped fixtures are torn down inside the LAST item of the module. This is that
# item, so it also pays conftest's _restore_rs485_settings teardown — up to two bounded
# POST /settings plus a settle window (2 x 20.1 s + 1 s = 41.2 s, see _RS485_HTTP_TIMEOUT).
# This module's own _baseline (:39) is setup-only and gateway_slave is function-scoped
# (built by conftest.build_gateway_fixture), so the conftest restore is the whole module
# teardown. 120 s body + 45 s teardown allowance.
@pytest.mark.timeout(165)
def test_gateway_disconnect_tid_mismatch_regression(gateway_slave):
    """Regression test: TID in B's response must equal B's request TID.

    Verifies fix for the bug where pending_tid was not cleared in
    on_tcp_conn_close(), causing B to receive a response with A's stale TID.
    Bug fix: clear ctx->pending_tid when the pending client disconnects.
    """
    gateway_slave.drop_count = 1

    gw_sock_a = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock_a.settimeout(5.0)
    try:
        gw_sock_a.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
        request_a = make_mbap_request(0x1100, 1, 0x03, 0, 1)
        gw_sock_a.sendall(request_a)
    finally:
        gw_sock_a.close()

    # Wait 0.5 s before resetting drop_count to ensure that:
    # 1. The QEMU UART slave has received and dropped A's RTU request (< 50 ms typically).
    # 2. The gateway's RTU timeout (~336 ms at 9600 baud) has fired, exercising the
    #    pending_tid cleanup path that this test validates.
    # B's 10 s socket timeout comfortably absorbs the remaining wait.
    time.sleep(0.5)
    gateway_slave.drop_count = 0

    gw_sock_b = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock_b.settimeout(10.0)
    try:
        gw_sock_b.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
        txid_b = 0x1101
        gw_sock_b.sendall(make_mbap_request(txid_b, 1, 0x03, 0, 1))

        resp_b = recv_modbus_tcp_response(gw_sock_b, time.monotonic() + 10.0)
        assert len(resp_b) >= 8, f"No response from gateway: {resp_b.hex()!r}"
        resp_txid_b = struct.unpack('>H', resp_b[:2])[0]
        # After the fix in on_tcp_conn_close(): B must receive its own TID, not A's stale TID
        assert resp_txid_b == txid_b, (
            f"TID mismatch: B received TID={resp_txid_b:#06x} instead of {txid_b:#06x}. "
            f"Regression: pending_tid not cleared after client A disconnected."
        )
    finally:
        gw_sock_b.close()
