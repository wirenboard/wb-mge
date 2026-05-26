"""Gateway E2E tests for unit-ID pass-through and protocol-ID validation (GW-05, GW-06).

Requires QEMU with UART1 exposed as TCP port 5561 and guest port 502 forwarded
to host port 50502. Uses a Python RTU slave (ModbusRtuSlaveThread) connected to
UART1 to respond to Modbus RTU requests.

Coverage:
GW-05. Non-zero unit IDs are passed through correctly on the same TCP session
       — no stale pending_slave_id between consecutive requests.
GW-06. Frames with protocol_id != 0 are silently dropped (no response, no TCP
       close); the connection remains usable for subsequent valid frames.
"""

import socket
import struct
import time

import pytest

from conftest import build_gateway_fixture
from modbus_helpers import make_mbap_request, recv_modbus_tcp_response


# ---------------------------------------------------------------------------
# Module-level constants
# ---------------------------------------------------------------------------

GATEWAY_HOST = "127.0.0.1"
GATEWAY_HOST_PORT = 50502    # QEMU hostfwd: guest port 502 → host 50502
UART1_TCP_PORT = 5561        # QEMU UART1 chardev TCP
FAKE_VALUE = 0x1234          # register value returned by the RTU slave


# ---------------------------------------------------------------------------
# Module-level baseline fixture: configure RS485-1 physical parameters
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
# Gateway fixture (module-scoped via build_gateway_fixture factory)
# ---------------------------------------------------------------------------

gateway_slave = build_gateway_fixture(
    port_num=1,
    tcp_host_port=GATEWAY_HOST_PORT,
    uart_tcp_port=UART1_TCP_PORT,
    bridge_port=502,
    modbus=True,
    fake_value=FAKE_VALUE,
)


# ---------------------------------------------------------------------------
# GW-05: Non-zero unit ID pass-through (same TCP session)
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(15)
def test_gateway_nonzero_unit_id_passthrough(gateway_slave):
    """Non-zero unit IDs are echoed back correctly on the same TCP connection.

    The RTU slave copies the request's slave_id into its response, so the
    gateway must propagate the unit_id field from each individual request and
    not carry over a stale value from the previous exchange.

    Two FC03 requests are sent on the same socket:
    - Request 1: unit_id=10 → verify response unit_id==10
    - Request 2: unit_id=20 → verify response unit_id==20
    """
    gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock.settimeout(5.0)
    try:
        gw_sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))

        for unit_id, txid in [(10, 0x0510), (20, 0x0520)]:
            request = make_mbap_request(txid, unit_id, 0x03, 0x0000, 1)
            gw_sock.sendall(request)

            response = recv_modbus_tcp_response(gw_sock, time.monotonic() + 5.0)
            assert len(response) >= 9, (
                f"unit_id={unit_id}: response too short: {response.hex()!r}"
            )

            resp_txid, resp_proto, _resp_length = struct.unpack('>HHH', response[:6])
            assert resp_txid == txid, (
                f"unit_id={unit_id}: TID mismatch: sent {txid:#06x}, got {resp_txid:#06x}"
            )
            assert resp_proto == 0, (
                f"unit_id={unit_id}: Protocol ID must be 0, got {resp_proto}"
            )

            pdu = response[6:]
            resp_unit_id = pdu[0]
            assert resp_unit_id == unit_id, (
                f"Unit ID mismatch in response: sent {unit_id}, got {resp_unit_id}. "
                "Gateway may have carried over a stale pending_slave_id."
            )
            fc_resp = pdu[1]
            assert not (fc_resp & 0x80), (
                f"unit_id={unit_id}: unexpected Modbus exception FC={fc_resp:#04x}"
            )

            print(
                f"✓ GW-05 unit_id={unit_id}: TID={txid:#06x} "
                f"resp_unit_id={resp_unit_id} FC={fc_resp:#04x}"
            )

        assert gateway_slave.request_count >= 2, (
            "RTU slave received fewer than 2 requests — gateway may not have forwarded both"
        )
    finally:
        gw_sock.close()


# ---------------------------------------------------------------------------
# GW-06: Invalid protocol_id frame is silently dropped; TCP stays open
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(30)
def test_gateway_invalid_protocol_id_drops_frame(gateway_slave):
    """Frames with protocol_id != 0 are silently dropped; TCP connection stays open.

    The Modbus TCP specification requires protocol_id == 0. The gateway firmware
    validates this field in modbus_tcp_check_request() and drops frames with
    any other value without closing the TCP connection.

    Sequence:
    1. Send an invalid frame (protocol_id=0x0001).
    2. Wait 0.5 s — no response expected.
    3. Verify no data arrives within a 0.3 s window (prove frame was dropped).
    4. Send a valid frame (protocol_id=0x0000) on the SAME socket.
    5. Verify the valid frame produces a correct Modbus TCP response.
    """
    gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock.settimeout(5.0)
    try:
        gw_sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))

        # Build an invalid Modbus TCP frame: protocol_id=0x0001 (must be 0)
        # Structure: TID(2) + proto(2) + length(2) + unit_id(1) + FC(1) + addr(2) + count(2)
        txid_invalid = 0x0601
        invalid_frame = (
            struct.pack('>HHHBB', txid_invalid, 0x0001, 6, 1, 0x03)
            + struct.pack('>HH', 0, 1)
        )
        gw_sock.sendall(invalid_frame)

        # Gateway should drop the frame without responding or closing the connection.
        # Wait 0.5 s to ensure the firmware has had time to process (and discard) the frame.
        time.sleep(0.5)

        # Verify that no response arrived for the invalid frame
        gw_sock.settimeout(0.3)
        try:
            data = gw_sock.recv(256)
            if data == b"":
                # Graceful TCP close (FIN) — connection was closed, not kept open
                pytest.fail(
                    "Gateway closed TCP connection (graceful FIN) after invalid protocol_id frame — "
                    "expected the connection to remain open"
                )
            pytest.fail(
                f"Gateway responded to invalid protocol_id frame: {data.hex()!r}"
            )
        except socket.timeout:
            # Expected: no response within 0.3 s — frame was silently dropped
            pass
        except (ConnectionResetError, OSError):
            pytest.fail(
                "Gateway closed TCP connection (RST) after invalid protocol_id frame — "
                "expected the connection to remain open"
            )

        # Restore normal socket timeout for the valid request
        gw_sock.settimeout(5.0)

        # Send a valid Modbus TCP frame on the SAME socket to prove the connection is alive
        txid_valid = 0x0602
        unit_id = 1
        valid_request = make_mbap_request(txid_valid, unit_id, 0x03, 0x0000, 1)
        gw_sock.sendall(valid_request)

        response = recv_modbus_tcp_response(gw_sock, time.monotonic() + 5.0)
        assert len(response) >= 9, (
            f"Valid frame response too short after invalid frame drop: {response.hex()!r}"
        )

        resp_txid, resp_proto, _resp_length = struct.unpack('>HHH', response[:6])
        assert resp_txid == txid_valid, (
            f"TID mismatch: sent {txid_valid:#06x}, got {resp_txid:#06x}"
        )
        assert resp_proto == 0, f"Protocol ID must be 0, got {resp_proto}"

        pdu = response[6:]
        assert pdu[0] == unit_id, f"Unit ID mismatch: expected {unit_id}, got {pdu[0]}"
        fc_resp = pdu[1]
        assert not (fc_resp & 0x80), (
            f"Unexpected Modbus exception after invalid frame drop: FC={fc_resp:#04x}"
        )

        print(
            f"✓ GW-06 protocol_id drop: invalid frame ignored, "
            f"valid TID={txid_valid:#06x} unit_id={pdu[0]} FC={fc_resp:#04x} OK"
        )
    finally:
        gw_sock.close()
