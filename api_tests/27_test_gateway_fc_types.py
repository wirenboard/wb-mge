"""Gateway E2E tests for various Modbus function codes (GW-01 through GW-04).

Requires QEMU with UART1 exposed as TCP port 5561 and guest port 502 forwarded
to host port 50502. Uses a Python RTU slave (ModbusRtuSlaveThread) connected to
UART1 to respond to FC01/FC02/FC03/FC04/FC16 requests.

Coverage:
GW-01. FC01 (Read Coils) forwarded through gateway; coil bits match fake_value.
GW-02. FC02 (Read Discrete Inputs) forwarded; response FC and bit data correct.
GW-03. FC04 (Read Input Registers) forwarded; register values match fake_value.
GW-04. FC16 (Write Multiple Registers) forwarded without truncation; slave
       last_write_values matches the values sent by the TCP client.
"""

import socket
import struct
import time

import pytest

from conftest import build_gateway_fixture
from modbus_helpers import (
    decode_fc01_fc02,
    decode_fc03_fc04,
    make_mbap_request,
    recv_modbus_tcp_response,
)


# ---------------------------------------------------------------------------
# Module-level constants
# ---------------------------------------------------------------------------

GATEWAY_HOST = "127.0.0.1"
GATEWAY_HOST_PORT = 50502    # QEMU hostfwd: guest port 502 → host 50502
UART1_TCP_PORT = 5561        # QEMU UART1 chardev TCP
FAKE_VALUE = 0x1234          # register / coil value returned by RTU slave


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
# GW-01: FC01 — Read Coils
# ---------------------------------------------------------------------------

@pytest.mark.qemu
# 120s: marker covers function-scoped setup+teardown whose retrying 30s HTTP calls are slow under QEMU load
@pytest.mark.timeout(120)
def test_gateway_fc01_read_coils(gateway_slave):
    """FC01 (Read Coils) forwarded through the gateway; coil byte matches fake_value.

    With fake_value=0x1234 (truthy), all coil bits are set to 1.
    8 coils packed into 1 byte = 0xFF.
    """
    txid = 0x0101
    unit_id = 1
    fc = 0x01
    start_addr = 0x0000
    count = 8   # request 8 coils → 1-byte coil field

    request = make_mbap_request(txid, unit_id, fc, start_addr, count)

    gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock.settimeout(5.0)
    try:
        gw_sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
        gw_sock.sendall(request)

        response = recv_modbus_tcp_response(gw_sock, time.monotonic() + 5.0)
        assert len(response) >= 10, f"FC01 response too short: {response.hex()!r}"

        resp_txid, resp_proto, resp_length = struct.unpack('>HHH', response[:6])
        assert resp_txid == txid, f"TID mismatch: sent {txid:#06x}, got {resp_txid:#06x}"
        assert resp_proto == 0, f"Protocol ID must be 0, got {resp_proto}"

        # PDU: unit_id(1) + FC(1) + payload
        pdu = response[6:]
        assert pdu[0] == unit_id, f"Unit ID mismatch: expected {unit_id}, got {pdu[0]}"
        assert pdu[1] == fc, f"FC mismatch: expected {fc:#04x}, got {pdu[1]:#04x}"

        # Decode coil payload and verify all bits are set (fake_value is truthy)
        payload = pdu[2:]
        coils = decode_fc01_fc02(payload, count)
        assert len(coils) == count, f"Expected {count} coils, got {len(coils)}"
        assert all(c == 1 for c in coils), (
            f"Expected all coils=1 (fake_value={FAKE_VALUE:#06x} is truthy), got {coils}"
        )

        assert gateway_slave.request_count >= 1, (
            "RTU slave did not receive any requests — gateway did not forward to UART"
        )
        print(
            f"✓ GW-01 FC01: TID={txid:#06x} coils={coils} "
            f"slave_requests={gateway_slave.request_count}"
        )
    finally:
        gw_sock.close()


# ---------------------------------------------------------------------------
# GW-02: FC02 — Read Discrete Inputs
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_gateway_fc02_read_discrete_inputs(gateway_slave):
    """FC02 (Read Discrete Inputs) forwarded through gateway; response FC and bit data correct.

    Like GW-01 but for discrete inputs.  With fake_value truthy, all bits = 1.
    """
    txid = 0x0202
    unit_id = 1
    fc = 0x02
    start_addr = 0x0000
    count = 8   # 8 discrete inputs → 1 byte

    request = make_mbap_request(txid, unit_id, fc, start_addr, count)

    gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock.settimeout(5.0)
    try:
        gw_sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
        gw_sock.sendall(request)

        response = recv_modbus_tcp_response(gw_sock, time.monotonic() + 5.0)
        assert len(response) >= 10, f"FC02 response too short: {response.hex()!r}"

        resp_txid, resp_proto, _resp_length = struct.unpack('>HHH', response[:6])
        assert resp_txid == txid, f"TID mismatch: sent {txid:#06x}, got {resp_txid:#06x}"
        assert resp_proto == 0, f"Protocol ID must be 0, got {resp_proto}"

        pdu = response[6:]
        assert pdu[0] == unit_id, f"Unit ID mismatch: expected {unit_id}, got {pdu[0]}"
        assert pdu[1] == fc, f"FC mismatch: expected {fc:#04x}, got {pdu[1]:#04x}"

        payload = pdu[2:]
        discrete = decode_fc01_fc02(payload, count)
        assert len(discrete) == count, f"Expected {count} discrete inputs, got {len(discrete)}"
        assert all(d == 1 for d in discrete), (
            f"Expected all discrete inputs=1 (fake_value={FAKE_VALUE:#06x} is truthy), got {discrete}"
        )

        print(
            f"✓ GW-02 FC02: TID={txid:#06x} discrete_inputs={discrete} "
            f"slave_requests={gateway_slave.request_count}"
        )
    finally:
        gw_sock.close()


# ---------------------------------------------------------------------------
# GW-03: FC04 — Read Input Registers
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_gateway_fc04_read_input_registers(gateway_slave):
    """FC04 (Read Input Registers) forwarded through gateway; register values match fake_value."""
    txid = 0x0403
    unit_id = 1
    fc = 0x04
    start_addr = 0x0000
    count = 2   # read 2 input registers

    request = make_mbap_request(txid, unit_id, fc, start_addr, count)

    gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock.settimeout(5.0)
    try:
        gw_sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
        gw_sock.sendall(request)

        response = recv_modbus_tcp_response(gw_sock, time.monotonic() + 5.0)
        assert len(response) >= 11, f"FC04 response too short: {response.hex()!r}"

        resp_txid, resp_proto, _resp_length = struct.unpack('>HHH', response[:6])
        assert resp_txid == txid, f"TID mismatch: sent {txid:#06x}, got {resp_txid:#06x}"
        assert resp_proto == 0, f"Protocol ID must be 0, got {resp_proto}"

        pdu = response[6:]
        assert pdu[0] == unit_id, f"Unit ID mismatch: expected {unit_id}, got {pdu[0]}"
        assert pdu[1] == fc, f"FC mismatch: expected {fc:#04x}, got {pdu[1]:#04x}"

        byte_count = pdu[2]
        assert byte_count == count * 2, (
            f"Byte count mismatch: expected {count * 2}, got {byte_count}"
        )

        payload = pdu[2:]
        registers = decode_fc03_fc04(payload, count)
        assert len(registers) == count, f"Expected {count} registers, got {len(registers)}"
        for i, val in enumerate(registers):
            assert val == FAKE_VALUE, (
                f"Register[{i}] value mismatch: expected {FAKE_VALUE:#06x}, got {val:#06x}"
            )

        print(
            f"✓ GW-03 FC04: TID={txid:#06x} regs={[hex(v) for v in registers]} "
            f"slave_requests={gateway_slave.request_count}"
        )
    finally:
        gw_sock.close()


# ---------------------------------------------------------------------------
# GW-04: FC16 — Write Multiple Registers
# ---------------------------------------------------------------------------

@pytest.mark.qemu
# 165 s, not 120 s: an item's pytest-timeout budget covers setup + call + TEARDOWN, and
# module-scoped fixtures are torn down inside the LAST item of the module. This is that
# item, so it also pays conftest's _restore_rs485_settings teardown — up to two bounded
# POST /settings plus a settle window (2 x 20.1 s + 1 s = 41.2 s, see _RS485_HTTP_TIMEOUT).
# This module's own _baseline (:44) is setup-only and gateway_slave is function-scoped
# (built by conftest.build_gateway_fixture), so the conftest restore is the whole module
# teardown. 120 s body + 45 s teardown allowance.
@pytest.mark.timeout(165)
def test_gateway_fc16_write_multiple_registers(gateway_slave):
    """FC16 (Write Multiple Registers) forwarded without truncation.

    Constructs the FC16 MBAP frame manually (make_mbap_request only handles
    fixed-size PDUs).  After the round-trip the RTU slave's last_write_values
    must match the data sent by the TCP client, proving that the gateway
    forwarded the full variable-length payload correctly.
    """
    txid = 0x1604
    unit_id = 1
    fc = 0x10                         # FC16 = Write Multiple Registers
    start_addr = 0x0000
    write_values = [0xABCD, 0xEF01]
    qty = len(write_values)
    byte_count = qty * 2

    # Build FC16 PDU: FC(1) + start_addr(2) + qty(2) + byte_count(1) + data(byte_count)
    pdu_data = struct.pack(f'>{qty}H', *write_values)
    pdu = struct.pack('>BHHB', fc, start_addr, qty, byte_count) + pdu_data

    # Build MBAP header: TID(2) + protocol_id(2=0) + length(2) + unit_id(1)
    mbap_length = 1 + len(pdu)   # unit_id byte + pdu
    request = struct.pack('>HHH', txid, 0, mbap_length) + bytes([unit_id]) + pdu

    gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock.settimeout(5.0)
    try:
        gw_sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))
        gw_sock.sendall(request)

        response = recv_modbus_tcp_response(gw_sock, time.monotonic() + 5.0)
        assert len(response) >= 8, f"FC16 response too short: {response.hex()!r}"

        resp_txid, resp_proto, _resp_length = struct.unpack('>HHH', response[:6])
        assert resp_txid == txid, f"TID mismatch: sent {txid:#06x}, got {resp_txid:#06x}"
        assert resp_proto == 0, f"Protocol ID must be 0, got {resp_proto}"

        pdu_resp = response[6:]
        assert pdu_resp[0] == unit_id, f"Unit ID mismatch: expected {unit_id}, got {pdu_resp[0]}"
        # Response FC should be 0x10 (echo) or 0x90 (exception FC)
        fc_resp = pdu_resp[1]
        assert fc_resp in (0x10, 0x90), (
            f"Expected FC16 echo (0x10) or exception (0x90), got {fc_resp:#04x}"
        )

        # KEY ASSERTION: the RTU slave must have received the full write data
        assert gateway_slave.last_write_values is not None, (
            "RTU slave did not record any FC16 write — gateway may not have forwarded FC16"
        )
        assert gateway_slave.last_write_values == write_values, (
            f"Write data mismatch: expected {[hex(v) for v in write_values]}, "
            f"got {[hex(v) for v in gateway_slave.last_write_values]}"
        )
        assert gateway_slave.last_write_addr == start_addr, (
            f"Write address mismatch: expected {start_addr:#06x}, "
            f"got {gateway_slave.last_write_addr:#06x}"
        )

        print(
            f"✓ GW-04 FC16: TID={txid:#06x} fc_resp={fc_resp:#04x} "
            f"write_values={[hex(v) for v in gateway_slave.last_write_values]}"
        )
    finally:
        gw_sock.close()
