"""End-to-end test for the Modbus TCP gateway (modbus_tcp.c / tcp_bridge mode with modbus=true).

Requires QEMU launched with UART1 exposed as TCP port 5561 and port 502 forwarded to 50502.
Uses a Python RTU slave (rtu_slave_helpers.ModbusRtuSlaveThread) connected to UART1.
"""

import socket
import struct
import time

import pytest

from conftest import build_gateway_fixture


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    resp = api.update_settings({
        "rs485_1": {
            "tx_disabled": False,     # gateway must forward bytes to UART
            "baudrate": 9600,
            "stopbits": "1",
            "parity": "none",
            "databits": "8",
        }
    })
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    # bridge.* and port_mode are set by the `gateway_slave` fixture

# Gateway port forwarded from QEMU guest port 502 to host port 50502
GATEWAY_HOST_PORT = 50502
# UART1 chardev TCP socket (QEMU -serial tcp::5561,server,nowait)
UART1_TCP_PORT = 5561
# Fake register value returned by the RTU slave for any register read
FAKE_VALUE = 0x1234


def _build_modbus_tcp_request(txid: int, unit_id: int, fc: int, addr: int, count: int) -> bytes:
    """Build a complete Modbus TCP MBAP + PDU frame."""
    pdu = struct.pack('>HH', addr, count)
    # MBAP: transaction_id(2) + protocol_id(2, always 0) + length(2) + unit_id(1) + fc(1) + PDU
    mbap_length = 1 + 1 + len(pdu)   # unit_id + FC + PDU
    mbap = struct.pack('>HHH', txid, 0, mbap_length)
    return mbap + bytes([unit_id, fc]) + pdu


# Use shared gateway fixture from conftest (R5)
gateway_slave = build_gateway_fixture(
    port_num=1,
    tcp_host_port=GATEWAY_HOST_PORT,
    uart_tcp_port=UART1_TCP_PORT,
    bridge_port=502,
    modbus=True,
    fake_value=FAKE_VALUE,
)


# --------------------------------------------------------------------------- #
# Tests                                                                        #
# --------------------------------------------------------------------------- #

@pytest.mark.qemu
def test_gateway_e2e_whole_frame(gateway_slave):
    """Send a complete (unfragmented) Modbus TCP request and verify the response.

    The RTU slave is already connected to UART1 before this request is sent.
    The gateway converts the TCP frame to RTU, forwards it over UART1, receives
    the RTU response from the slave, and returns a TCP response to us.

    Verifies:
    - Response has the correct Modbus TCP structure (MBAP + PDU).
    - The transaction ID in the response matches the one we sent.
    - The unit ID in the response matches the one we sent.
    - The register values in the response match the slave's fake_value.
    """
    txid = 0x0042
    unit_id = 0x01
    fc = 0x03          # Read Holding Registers
    start_addr = 0x0000
    count = 2          # Read 2 registers

    request = _build_modbus_tcp_request(txid, unit_id, fc, start_addr, count)

    gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock.settimeout(5.0)
    try:
        gw_sock.connect(("127.0.0.1", GATEWAY_HOST_PORT))
        gw_sock.sendall(request)

        # Read Modbus TCP response (MBAP header is 6 bytes)
        response = b''
        deadline = time.monotonic() + 5.0
        while len(response) < 6 and time.monotonic() < deadline:
            chunk = gw_sock.recv(256)
            if not chunk:
                break
            response += chunk

        # Parse MBAP header
        assert len(response) >= 6, (
            f"Gateway returned too few bytes for MBAP header: {response.hex()!r}"
        )
        resp_txid, resp_proto, resp_length = struct.unpack('>HHH', response[:6])

        # Read the rest of the frame if not yet received
        total_expected = 6 + resp_length
        while len(response) < total_expected and time.monotonic() < deadline:
            chunk = gw_sock.recv(256)
            if not chunk:
                break
            response += chunk

        assert len(response) >= total_expected, (
            f"Gateway returned incomplete frame: got {len(response)} bytes, "
            f"expected {total_expected}. Raw: {response.hex()!r}"
        )

        # Validate MBAP fields
        assert resp_txid == txid, (
            f"Transaction ID mismatch: sent {txid:#06x}, got {resp_txid:#06x}"
        )
        assert resp_proto == 0, (
            f"Protocol ID must be 0, got {resp_proto}"
        )

        # PDU starts at byte 6: unit_id(1) + FC(1) + byte_count(1) + data
        pdu = response[6:]
        assert len(pdu) >= 3, f"PDU too short: {pdu.hex()!r}"
        assert pdu[0] == unit_id, (
            f"Unit ID mismatch in response: expected {unit_id}, got {pdu[0]}"
        )
        assert pdu[1] == fc, (
            f"Function code mismatch: expected {fc:#04x}, got {pdu[1]:#04x}"
        )
        byte_count = pdu[2]
        assert byte_count == count * 2, (
            f"Byte count mismatch: expected {count * 2}, got {byte_count}"
        )

        # Parse register values
        reg_data = pdu[3:3 + byte_count]
        assert len(reg_data) == byte_count, (
            f"Register data length mismatch: expected {byte_count}, got {len(reg_data)}"
        )
        registers = struct.unpack(f'>{count}H', reg_data)
        for i, val in enumerate(registers):
            assert val == FAKE_VALUE, (
                f"Register[{i}] value mismatch: expected {FAKE_VALUE:#06x}, got {val:#06x}"
            )

        assert gateway_slave.request_count >= 1, (
            "RTU slave did not receive any requests — gateway did not forward the TCP frame to UART1"
        )
        print(
            f"✓ Gateway e2e whole-frame: txid={txid:#06x} unit={unit_id} "
            f"regs={[hex(v) for v in registers]} slave_requests={gateway_slave.request_count}"
        )

    finally:
        gw_sock.close()


@pytest.mark.qemu
def test_gateway_e2e_split_frame(gateway_slave):
    """Send the SAME request split into two TCP segments (partial frame test).

    The first 6 bytes are sent, then after a 20 ms pause the remaining bytes
    follow. This specifically tests that separate_and_push_requests_from_tcp_with_client()
    in modbus_tcp.c handles partial frames correctly (reassembly across TCP segments).

    Fix applied — split frames are now correctly reassembled.
    """
    txid = 0x0099
    unit_id = 0x01
    fc = 0x03          # Read Holding Registers
    start_addr = 0x0000
    count = 2

    request = _build_modbus_tcp_request(txid, unit_id, fc, start_addr, count)

    # Split: first 6 bytes (MBAP header only), pause, rest of frame
    part1 = request[:6]
    part2 = request[6:]

    gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock.settimeout(5.0)
    try:
        gw_sock.connect(("127.0.0.1", GATEWAY_HOST_PORT))

        # Send only the MBAP header first
        gw_sock.sendall(part1)
        # Pause 20 ms to ensure the firmware processes the partial segment
        time.sleep(0.02)
        # Send the rest of the frame
        gw_sock.sendall(part2)

        # Expect a valid Modbus TCP response
        response = b''
        deadline = time.monotonic() + 5.0
        while len(response) < 6 and time.monotonic() < deadline:
            chunk = gw_sock.recv(256)
            if not chunk:
                break
            response += chunk

        assert len(response) >= 6, (
            f"Gateway returned too few bytes — split-frame was dropped: {response.hex()!r}"
        )

        resp_txid, resp_proto, resp_length = struct.unpack('>HHH', response[:6])
        total_expected = 6 + resp_length

        while len(response) < total_expected and time.monotonic() < deadline:
            chunk = gw_sock.recv(256)
            if not chunk:
                break
            response += chunk

        assert len(response) >= total_expected, (
            f"Split-frame response incomplete: got {len(response)} bytes, "
            f"expected {total_expected}. Raw: {response.hex()!r}"
        )
        assert resp_txid == txid, (
            f"Transaction ID mismatch: sent {txid:#06x}, got {resp_txid:#06x}"
        )

        pdu = response[6:]
        assert pdu[1] == fc, f"FC mismatch: {pdu[1]:#04x}"
        registers = struct.unpack(f'>{count}H', pdu[3:3 + count * 2])
        for i, val in enumerate(registers):
            assert val == FAKE_VALUE, f"Register[{i}]={val:#06x} != {FAKE_VALUE:#06x}"

        print(
            f"✓ Gateway e2e split-frame: txid={txid:#06x} regs={[hex(v) for v in registers]}"
        )

    finally:
        gw_sock.close()
