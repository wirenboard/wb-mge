"""End-to-end test for Fast Modbus probe (GW-08).

Sends FC=0x47 (unit_id=0x00, payload='WB-FAST-MODBUS?') via Modbus TCP to this slot's
gateway host port (QEMU hostfwd for guest port 502) and verifies that the firmware
intercepts the request and replies 'WB-FAST-MODBUS-OK' — without forwarding it to the RTU bus.

Requires QEMU launched with guest port 502 forwarded to the gateway host port of this
WB_MGE_PORT_SLOT (api_tests/qemu_ports.py).
"""

import qemu_ports
import socket
import struct

import pytest

# Modbus TCP constants for the Fast Modbus probe request
_PROBE_TRANSACTION_ID = 0x0123
_PROBE_PROTOCOL_ID = 0x0000
_PROBE_LENGTH = 17       # 2 bytes (unit_id + function) + 15 bytes payload
_PROBE_UNIT_ID = 0x00
_PROBE_FUNCTION_CODE = 0x47
_PROBE_PAYLOAD = b'WB-FAST-MODBUS?'
_PROBE_EXPECTED_RESPONSE = b'WB-FAST-MODBUS-OK'

# Host-side port forwarded from QEMU guest port 502
MODBUS_TCP_HOST_PORT = qemu_ports.MODBUS_TCP_HOST_PORT
GATEWAY_PORT_1 = qemu_ports.GATEWAY_HOST_PORT

_SOCKET_TIMEOUT = 5.0


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    """Configure port 1 in Modbus TCP gateway mode before probe tests.

    Fast Modbus probe detection only fires in modbus=True mode (modbus_tcp.c).
    Transparent bridge mode (modbus=False) simply forwards the frame to UART — no intercept.
    """
    resp = api.update_settings({
        "rs485_1": {
            "bridge": {"mode": "server", "port": 502, "ip": "0.0.0.0", "modbus": True},
        }
    })
    assert resp.status_code == 200, (
        f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    )
    resp = api.set_port_mode(1, "tcp_bridge")
    assert resp.status_code == 200, (
        f"_baseline: set_port_mode(1, tcp_bridge) failed: {resp.status_code} {resp.text}"
    )


def _build_probe_request(transaction_id: int = _PROBE_TRANSACTION_ID) -> bytes:
    """Build a Modbus TCP Fast Modbus probe request frame."""
    header = struct.pack(
        '>HHHBB',
        transaction_id,
        _PROBE_PROTOCOL_ID,
        _PROBE_LENGTH,
        _PROBE_UNIT_ID,
        _PROBE_FUNCTION_CODE,
    )
    return header + _PROBE_PAYLOAD


def _recv_modbus_tcp_response(sock: socket.socket) -> bytes:
    """Receive a complete Modbus TCP response (MBAP header + PDU)."""
    # Read MBAP header: transaction_id(2) + protocol_id(2) + length(2) = 6 bytes
    header = b''
    while len(header) < 6:
        chunk = sock.recv(6 - len(header))
        if not chunk:
            raise ConnectionError("Connection closed while reading MBAP header")
        header += chunk

    _, _, length = struct.unpack('>HHH', header)

    # Read PDU: length bytes follow the MBAP header
    pdu = b''
    while len(pdu) < length:
        chunk = sock.recv(length - len(pdu))
        if not chunk:
            raise ConnectionError("Connection closed while reading PDU")
        pdu += chunk

    return header + pdu


@pytest.mark.qemu
def test_fast_modbus_probe_responds(api):
    """Firmware intercepts FC=0x47 unit_id=0 and replies 'WB-FAST-MODBUS-OK'.

    The api fixture is used only to ensure QEMU is ready before the test runs.
    The actual probe is sent directly over a raw TCP socket to MODBUS_TCP_HOST_PORT.
    """
    request = _build_probe_request(_PROBE_TRANSACTION_ID)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(_SOCKET_TIMEOUT)
        sock.connect(("127.0.0.1", MODBUS_TCP_HOST_PORT))
        sock.sendall(request)
        response = _recv_modbus_tcp_response(sock)

    assert len(response) >= 8, (
        f"Response too short: {len(response)} bytes, raw: {response.hex()!r}"
    )

    tid, pid, length, unit_id, function = struct.unpack('>HHHBB', response[:8])

    assert tid == _PROBE_TRANSACTION_ID, (
        f"Transaction ID mismatch: sent {_PROBE_TRANSACTION_ID:#06x}, got {tid:#06x}"
    )
    assert pid == _PROBE_PROTOCOL_ID, (
        f"Protocol ID must be 0x0000, got {pid:#06x}"
    )
    assert unit_id == _PROBE_UNIT_ID, (
        f"Unit ID mismatch: expected {_PROBE_UNIT_ID}, got {unit_id}"
    )
    assert function == _PROBE_FUNCTION_CODE, (
        f"Function code mismatch: expected {_PROBE_FUNCTION_CODE:#04x}, got {function:#04x}"
    )

    data_payload = response[8:]
    assert data_payload == _PROBE_EXPECTED_RESPONSE, (
        f"Payload mismatch: expected {_PROBE_EXPECTED_RESPONSE!r}, got {data_payload!r}"
    )

    print(
        f"✓ Fast Modbus probe: tid={tid:#06x} fc={function:#04x} "
        f"payload={data_payload!r}"
    )


@pytest.mark.qemu
def test_fast_modbus_probe_wrong_function_ignored(api):
    """FC=0x03 with unit_id=0 must NOT be intercepted as a Fast Modbus probe.

    The firmware should forward FC=0x03 to the RTU bus, not reply with
    'WB-FAST-MODBUS-OK'. Since there is no RTU slave connected, the firmware
    will either time out or return a Modbus exception — both outcomes confirm
    the request was NOT intercepted by the probe handler (function code != 0x47).

    The api fixture is used only to ensure QEMU is ready before the test runs.
    """
    transaction_id = 0x0456
    unit_id = 0x00
    function_code = 0x03   # Read Holding Registers — must not trigger probe intercept

    # Reuse the probe payload length for the frame; the content is irrelevant
    # because we are testing the function-code dispatch, not the data.
    payload = _PROBE_PAYLOAD
    request = struct.pack(
        '>HHHBB',
        transaction_id,
        _PROBE_PROTOCOL_ID,
        len(payload) + 2,   # length field covers unit_id + FC + payload
        unit_id,
        function_code,
    ) + payload

    response_bytes = None
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(_SOCKET_TIMEOUT)
            sock.connect(("127.0.0.1", MODBUS_TCP_HOST_PORT))
            sock.sendall(request)
            response_bytes = _recv_modbus_tcp_response(sock)
    except (socket.timeout, ConnectionError):
        # Timeout means the firmware forwarded the request to the RTU bus and
        # no slave replied — correct behaviour (not intercepted).
        print("✓ FC=0x03 timed out — request was forwarded to RTU bus (not intercepted)")
        return

    # If a response arrived, the function code must NOT be 0x47 (probe reply)
    assert len(response_bytes) >= 8, (
        f"Response too short to parse: {response_bytes.hex()!r}"
    )
    _, _, _, resp_unit_id, resp_function = struct.unpack('>HHHBB', response_bytes[:8])
    assert resp_unit_id == unit_id, (
        f"Unit ID mismatch: expected {unit_id}, got {resp_unit_id}"
    )
    assert resp_function != _PROBE_FUNCTION_CODE, (
        f"FC=0x03 request was incorrectly intercepted as a Fast Modbus probe "
        f"(response function code is {resp_function:#04x})"
    )

    print(
        f"✓ FC=0x03 not intercepted: resp_fc={resp_function:#04x} "
        f"resp_unit_id={resp_unit_id}"
    )
